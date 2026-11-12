"""协议探测工具的离线边界测试；合成输入仅用于本测试，不进入固件。"""
import io
from http.client import IncompleteRead
import json
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout

import probe_lizhi as probe


class Response(io.BytesIO):
    def __init__(self, body, headers=None, status=200):
        super().__init__(body)
        self.headers = headers or {}
        self.status = status


class ProbeTests(unittest.TestCase):
    def test_large_numeric_id_is_not_rounded(self):
        body = b'{"code":0,"data":[{"voiceInfo":{"voiceId":9007199254740993}}]}'
        result = probe.summarize_page(body, 8)
        self.assertEqual(result['count'], 1)
        self.assertEqual(result['voice_ids'], ['9007199254740993'])

    def test_empty_page_is_valid_but_does_not_claim_pagination_end(self):
        result = probe.summarize_page(b'{"code":0,"data":[]}', 8)
        self.assertEqual(result['count'], 0)
        self.assertNotIn('has_next', result)

    def test_invalid_pages_fail_instead_of_becoming_empty_lists(self):
        cases = [b'<html>denied</html>', b'{"code":403,"data":[]}',
                 b'{"code":false,"data":[]}', b'{"data":[]}',
                 b'{"code":0,"data":{}}', b'{"code":0,"data":[null]}',
                 b'{"code":0,"data":[]} trailing', b'{"code":0,"data":[],"x":NaN}',
                 b'{"code":0,"code":403,"data":[]}']
        for body in cases:
            with self.subTest(body=body), self.assertRaises(probe.ProbeError):
                probe.summarize_page(body, 8)

    def test_duplicate_ids_are_reported_without_losing_rows(self):
        row = {'voiceInfo': {'voiceId': '123'}}
        result = probe.summarize_page(json.dumps({'code': 0, 'data': [row, row]}).encode(), 8)
        self.assertEqual(result['count'], 2)
        self.assertEqual(result['duplicate_voice_ids'], ['123'])

    def test_unknown_episode_schema_is_not_reported_as_success(self):
        for row in [{}, {'voiceInfo': {}}, {'voiceInfo': {'voiceId': 1.5}},
                    {'voiceInfo': {'voiceId': True}}, {'voiceInfo': {'voiceId': '0'}}]:
            with self.subTest(row=row), self.assertRaises(probe.ProbeError):
                probe.summarize_page(json.dumps({'code': 0, 'data': [row]}).encode(), 8)

    def test_cli_http_error_has_failure_exit_and_saved_summary(self):
        output = io.StringIO()
        with patch.object(probe, 'fetch', return_value=(403, {}, b'denied', 'https://m.lizhi.fm/a')):
            with redirect_stdout(output):
                result = probe.main(['--author-id', '1'])
        self.assertEqual(result, 1)
        report = json.loads(output.getvalue())
        self.assertFalse(report['ok'])
        self.assertEqual(report['http_status'], 403)
        artifact = probe.Path(report['artifact_dir'])
        self.assertEqual((artifact / 'body.raw').read_bytes(), b'denied')
        self.assertEqual(json.loads((artifact / 'summary.json').read_text()), report)

    def test_cli_network_failure_keeps_evidence_without_exception_details(self):
        output = io.StringIO()
        with patch.object(probe, 'fetch', side_effect=OSError('secret-credential')):
            with redirect_stdout(output):
                result = probe.main(['--author-id', '1'])
        self.assertEqual(result, 1)
        self.assertNotIn('secret-credential', output.getvalue())
        report = json.loads(output.getvalue())
        self.assertTrue((probe.Path(report['artifact_dir']) / 'summary.json').is_file())

    def test_page_size_and_id_boundaries(self):
        with self.assertRaises(probe.ProbeError):
            probe.summarize_page(b'{"code":0,"data":[{},{}]}', 1)
        for value in ['0', '-1', '1/../../', '1?x=2', '1' * 33, '１２３']:
            with self.subTest(value=value), self.assertRaises(probe.ProbeError):
                probe.program_url(value, 1, 8)
        self.assertEqual(probe.program_url('9007199254740993', 2, 8),
                         'https://m.lizhi.fm/vodapi/user/9007199254740993?pageNo=2&pageSize=8')

    def test_read_rejects_oversize_truncation_and_encoding(self):
        for response in [Response(b'12345'), Response(b'12', {'Content-Length': '3'}),
                         Response(b'12', {'Content-Encoding': 'gzip'}),
                         Response(b'12', {'Content-Length': 'invalid'})]:
            with self.subTest(headers=response.headers), self.assertRaises(probe.ProbeError):
                probe.read_body(response, 4)
        self.assertEqual(probe.read_body(Response(b'1234'), 4), b'1234')

    def test_redirect_rejects_cleartext_and_credentials(self):
        handler = probe.SafeRedirect()
        request = probe.Request('https://m.lizhi.fm/a')
        for url in ['http://m.lizhi.fm/a', 'https://user:secret@example.com/a']:
            with self.subTest(url=url), self.assertRaises(probe.ProbeError):
                handler.redirect_request(request, None, 302, 'redirect', {}, url)

    def test_media_query_never_appears_in_summary(self):
        self.assertEqual(probe.safe_url('https://example.com/a.mp3?token=secret#x'),
                         'https://example.com/a.mp3')

    def test_media_range_requires_matching_206_not_200(self):
        header = {'Content-Range': 'bytes 0-3/100', 'Content-Type': 'audio/mpeg'}
        result = probe.summarize_media(206, header, b'ID3x', 4)
        self.assertTrue(result['range_verified'])
        self.assertEqual(result['format_hint'], 'id3')
        self.assertFalse(probe.summarize_media(200, {}, b'ID3x', 4)['range_verified'])
        for content_range in ['bytes 1-4/100', 'bytes 0-9/100', 'bytes 0-3/*', 'bytes 0-3/2']:
            with self.subTest(content_range=content_range):
                result = probe.summarize_media(206, {'Content-Range': content_range}, b'ID3x', 4)
                self.assertFalse(result['range_verified'])

    def test_nonzero_range_requires_exact_start_and_reports_resource_size(self):
        result = probe.summarize_media(206, {'Content-Range': 'bytes 1048576-1048579/40964473'},
                                       b'1234', 4, start=1048576)
        self.assertTrue(result['range_verified'])
        self.assertEqual(result['resource_bytes'], 40964473)
        result = probe.summarize_media(206, {'Content-Range': 'bytes 0-3/40964473'},
                                       b'1234', 4, start=1048576)
        self.assertFalse(result['range_verified'])

    def test_transport_sends_nonzero_range_and_rejects_invalid_offset(self):
        class Opener:
            def open(self, request, timeout):
                self.request = request
                return Response(b'1234')
        opener = Opener()
        with patch.object(probe, 'build_opener', return_value=opener):
            probe.fetch('https://example.com/a', 4, media=True, start=1048576)
        self.assertEqual(opener.request.get_header('Range'), 'bytes=1048576-1048579')
        for offset in [-1, True, 1.5]:
            with self.subTest(offset=offset), self.assertRaises(probe.ProbeError):
                probe.fetch('https://example.com/a', 4, media=True, start=offset)

    def test_transport_keeps_tls_and_bounded_range_request(self):
        class Opener:
            def open(self, request, timeout):
                self.request, self.timeout = request, timeout
                return Response(b'ID3x', {'Content-Length': '4'})
        opener = Opener()
        with patch.object(probe, 'build_opener', return_value=opener):
            status, headers, body, final = probe.fetch('https://example.com/a', 4, media=True)
        self.assertEqual(opener.request.get_header('Range'), 'bytes=0-3')
        self.assertEqual(opener.request.get_header('Accept-encoding'), 'identity')
        self.assertEqual(opener.timeout, 20)
        self.assertEqual(body, b'ID3x')

    def test_incomplete_transport_preserves_http_status(self):
        class BrokenResponse(Response):
            def read(self, size):
                raise IncompleteRead(b'partial', 100)
        class Opener:
            def open(self, request, timeout):
                return BrokenResponse(b'', status=403)
        with patch.object(probe, 'build_opener', return_value=Opener()):
            with self.assertRaises(probe.ProbeError) as caught:
                probe.fetch('https://example.com/a', 16)
        self.assertEqual(caught.exception.http_status, 403)


if __name__ == '__main__':
    unittest.main()
