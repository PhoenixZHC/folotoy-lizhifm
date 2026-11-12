#!/usr/bin/env python3
"""只读主机探测：输出脱敏证据，不登录、不绕过 TLS、不作为固件客户端。"""
import argparse
from datetime import datetime, timezone
import hashlib
from http.client import HTTPException
import json
from pathlib import Path
import re
import ssl
import sys
import tempfile
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit, urlunsplit
from urllib.request import (Request, HTTPRedirectHandler, HTTPSHandler,
                            ProxyHandler, build_opener)


class ProbeError(ValueError):
    pass


def check_url(url):
    try:
        parsed = urlsplit(url)
        if (parsed.scheme != 'https' or not parsed.hostname or parsed.username is not None
                or parsed.password is not None or parsed.port not in (None, 443)
                or any(ord(c) <= 32 or ord(c) == 127 for c in url)):
            raise ProbeError('只接受无凭证、标准端口的 HTTPS URL')
    except ValueError as error:
        raise ProbeError('URL 无效') from error
    return parsed


def safe_url(url):
    parsed = check_url(url)
    return urlunsplit((parsed.scheme, parsed.netloc, parsed.path, '', ''))


class SafeRedirect(HTTPRedirectHandler):
    max_redirections = 5
    max_repeats = 2

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def program_url(author_id, page, size):
    if not re.fullmatch(r'[0-9]{1,32}', author_id) or int(author_id) == 0:
        raise ProbeError('博主 ID 须为 1～32 位非零十进制字符串，不能把电台号直接当用户 ID')
    if not 1 <= page <= 10000 or not 1 <= size <= 8:
        raise ProbeError('page 范围 1～10000，page-size 范围 1～8')
    return f'https://m.lizhi.fm/vodapi/user/{author_id}?pageNo={page}&pageSize={size}'


def read_body(response, limit):
    encoding = response.headers.get('Content-Encoding', 'identity').lower()
    if encoding not in ('identity', ''):
        raise ProbeError('收到未支持的压缩编码，未将压缩字节解析成 JSON')
    length = response.headers.get('Content-Length')
    if length is not None and (not re.fullmatch(r'[0-9]+', length) or int(length) > limit):
        raise ProbeError('Content-Length 无效或正文超限')
    chunks = []
    count = 0
    while True:
        chunk = response.read(min(4096, limit + 1 - count))
        if not chunk:
            break
        count += len(chunk)
        if count > limit:
            raise ProbeError('正文超限，未解析不完整响应')
        chunks.append(chunk)
    if length is not None and count != int(length):
        raise ProbeError('正文长度不符，未解析截断响应')
    return b''.join(chunks)


def fetch(url, limit, media=False, start=0):
    check_url(url)
    if type(start) is not int or start < 0 or (start and not media):
        raise ProbeError('Range 起点须为非负整数且仅用于媒体请求')
    headers = {'Accept-Encoding': 'identity'}
    if media:
        headers['Range'] = f'bytes={start}-{start + limit - 1}'
    request = Request(url, headers=headers)
    # 显式禁用环境代理，保证本工具测试的是主机直连；TLS 使用系统 CA 校验。
    opener = build_opener(ProxyHandler({}), SafeRedirect(),
                          HTTPSHandler(context=ssl.create_default_context()))
    try:
        response = opener.open(request, timeout=20)
    except HTTPError as error:
        response = error
    except (URLError, OSError) as error:
        raise ProbeError(f'网络请求失败：{type(error).__name__}') from error
    with response:
        status = response.status
        selected = {key: response.headers[key] for key in
                    ('Content-Type', 'Content-Length', 'Content-Range', 'Content-Encoding',
                     'Accept-Ranges') if key in response.headers}
        final_url = response.geturl() if hasattr(response, 'geturl') else url
        try:
            body = read_body(response, limit)
        except (ProbeError, HTTPException, OSError) as error:
            failure = ProbeError(str(error) if isinstance(error, ProbeError)
                                 else f'正文读取失败：{type(error).__name__}')
            failure.http_status = status
            raise failure from error
    return status, selected, body, safe_url(final_url)


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProbeError('JSON 含重复键')
        result[key] = value
    return result


def summarize_page(body, page_size):
    try:
        payload = json.loads(body.decode('utf-8'), object_pairs_hook=strict_object,
                             parse_constant=lambda value: (_ for _ in ()).throw(ProbeError('非标准 JSON 数值')))
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ProbeError('响应不是完整的标准 UTF-8 JSON 对象') from error
    if not isinstance(payload, dict) or type(payload.get('code')) is not int:
        raise ProbeError('响应缺少整数业务状态码，协议待核实')
    if payload['code'] != 0:
        raise ProbeError(f'业务接口失败，code={payload["code"]}')
    rows = payload.get('data')
    if not isinstance(rows, list) or len(rows) > page_size or any(not isinstance(x, dict) for x in rows):
        raise ProbeError('data 类型或条数不符合当前公开接口线索，协议待核实')
    ids, duplicates, fields = [], [], set()
    for row in rows:
        for key, value in row.items():
            fields.add(f'{key}:{type(value).__name__}')
        info = row.get('voiceInfo')
        if not isinstance(info, dict):
            raise ProbeError('节目条目缺少 voiceInfo，协议待核实')
        value = info.get('voiceId')
        if type(value) is not int and not isinstance(value, str):
            raise ProbeError('节目 ID 类型不符合当前接口线索，协议待核实')
        value = str(value)
        if not re.fullmatch(r'[0-9]{1,32}', value) or int(value) == 0:
            raise ProbeError('节目 ID 无效或超限，协议待核实')
        if value in ids and value not in duplicates:
            duplicates.append(value)
        ids.append(value)
    return {'count': len(rows), 'voice_ids': ids, 'duplicate_voice_ids': duplicates,
            'row_fields': sorted(fields),
            'note': '仅响应证据；空页不证明博主有效，分页末尾和字段语义须另行对照'}


def summarize_media(status, headers, body, requested, start=0):
    match = re.fullmatch(r'bytes ([0-9]+)-([0-9]+)/([0-9]+)', headers.get('Content-Range', ''))
    verified = bool(status == 206 and match and int(match[1]) == start
                    and int(match[2]) - start + 1 == len(body) and 0 < len(body) <= requested
                    and int(match[3]) > int(match[2]))
    hint = 'unknown'
    if body.startswith(b'ID3'):
        hint = 'id3'
    elif len(body) >= 2 and body[0] == 255 and body[1] & 0xe0 == 0xe0:
        hint = 'mpeg-sync-candidate'
    return {'range_verified': verified, 'range_start': start,
            'resource_bytes': int(match[3]) if verified else None, 'format_hint': hint,
            'note': '首段探测不证明 MP3 可解码、非零偏移 seek、URL 有效期或设备播放成功'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--author-id', help='经分享链接核实的用户 ID，不是电台号')
    mode.add_argument('--media-url-file', type=Path, help='仅本机保存的 HTTPS 媒体 URL 文本文件，避免 URL 进入命令历史')
    parser.add_argument('--page', type=int, default=1)
    parser.add_argument('--page-size', type=int, default=2, help='默认每页2条，避免真实节目简介导致正文超限；上限8条')
    parser.add_argument('--range-start', type=int, default=0, help='媒体字节探测起点；不表示播放时间或 seek 已实现')
    args = parser.parse_args(argv)
    media = args.media_url_file is not None
    report = {'time_utc': datetime.now(timezone.utc).isoformat(), 'device': 'Ubuntu 开发电脑',
              'mode': 'media' if media else 'programs', 'ok': False}
    try:
        if args.range_start < 0 or (args.range_start and not media):
            raise ProbeError('Range 起点须为非负整数且仅用于媒体请求')
        if media:
            if args.media_url_file.stat().st_size > 4096:
                raise ProbeError('媒体 URL 文件超过 4096 字节')
            url = args.media_url_file.read_text(encoding='utf-8').strip()
            check_url(url)
            if len(url.encode('utf-8')) > 2048:
                raise ProbeError('媒体 URL 超过 2048 字节')
        else:
            url = program_url(args.author_id, args.page, args.page_size)
            report.update(author_id=args.author_id, page=args.page, page_size=args.page_size)
        report['url'] = ('https://' + check_url(url).netloc) if media else safe_url(url)
        root = Path(__file__).resolve().parent.parent / '.local-backup' / 'lizhi-probes'
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        destination = Path(tempfile.mkdtemp(prefix='probe-', dir=root))
        report['artifact_dir'] = str(destination)
        if media:
            status, headers, body, final = fetch(url, 4096, media=True, start=args.range_start)
        else:
            status, headers, body, final = fetch(url, 16384)
        report.update(http_status=status, headers=headers,
                      final_url=('https://' + check_url(final).netloc) if media else final,
                      bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
        raw = destination / 'body.raw'
        raw.touch(mode=0o600)
        raw.write_bytes(body)
        if media:
            report.update(summarize_media(status, headers, body, 4096, args.range_start))
            report['ok'] = report['range_verified']
        elif status == 200:
            report.update(summarize_page(body, args.page_size))
            report['ok'] = True
        else:
            raise ProbeError(f'HTTP {status}，未当作节目空列表')
    except (ProbeError, OSError, UnicodeError) as error:
        # 不输出底层异常文本，避免带签名 URL、会话或文件内容进入日志。
        report['error'] = str(error) if isinstance(error, ProbeError) else type(error).__name__
        if hasattr(error, 'http_status'):
            report['http_status'] = error.http_status
    output = json.dumps(report, ensure_ascii=False, indent=2)
    if 'artifact_dir' in report:
        target = Path(report['artifact_dir']) / 'summary.json'
        try:
            target.touch(mode=0o600)
            target.write_text(output + '\n', encoding='utf-8')
        except OSError:
            report.update(ok=False, error='验证摘要写入失败')
            output = json.dumps(report, ensure_ascii=False, indent=2)
    print(output)
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
