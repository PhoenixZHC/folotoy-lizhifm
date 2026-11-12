import json
import unittest
from lizhi_follow_capture import allowed_host, path_template, summarize


class CaptureTests(unittest.TestCase):
    def test_domain_boundary(self):
        self.assertTrue(allowed_host("api.lizhi.fm"))
        self.assertFalse(allowed_host("notlizhi.fm"))
        self.assertFalse(allowed_host("lizhi.fm.other.example"))

    def test_redacts_auth_query_json_and_account_values(self):
        secret = "DO_NOT_WRITE_THIS_CREDENTIAL"
        result = summarize("POST", "https://api.lizhi.fm/observed/path?token=" + secret,
                           ["Cookie", "Authorization"], 200, "application/json",
                           json.dumps({"data": [{"userId": "5036275272706175532", "name": "private-name", "token": secret}]}).encode(),
                           json.dumps({"accessToken": secret}).encode(), "application/json")
        output = json.dumps(result)
        for value in (secret, "5036275272706175532", "private-name"):
            self.assertNotIn(value, output)
        self.assertEqual(result["query_fields"], ["token"])
        self.assertEqual(result["auth_headers_present"], ["cookie", "authorization"])
        self.assertIn("accessToken", result["request_schema"]["fields"])
        self.assertEqual(result["response_schema"]["fields"]["data"]["length"], 1)

    def test_skip_other_sites_and_media(self):
        self.assertIsNone(summarize("GET", "https://example.com/x", [], 200, "application/json", b"{}"))
        self.assertIsNone(summarize("GET", "https://cdn.lizhi.fm/x", [], 200, "audio/mpeg", b"binary"))

    def test_bad_json_is_not_empty_success(self):
        result = summarize("GET", "https://m.lizhi.fm/x", [], 200, "application/json", b"broken")
        self.assertTrue(result["response_json_invalid"])
        self.assertNotIn("response_schema", result)

    def test_dynamic_paths_redacted(self):
        self.assertEqual(path_template("/user/5036275272706175532/" + "a" * 50), "/user/:id/:opaque")

    def test_form_values_not_recorded(self):
        result = summarize("POST", "https://m.lizhi.fm/x", [], 200, "application/json", b"{}",
                           b"token=SECRET&cursor=20", "application/x-www-form-urlencoded")
        self.assertEqual(result["request_fields"], ["cursor", "token"])
        self.assertNotIn("SECRET", json.dumps(result))


if __name__ == "__main__":
    unittest.main()
