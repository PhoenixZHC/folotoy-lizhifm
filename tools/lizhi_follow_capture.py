"""临时mitmproxy插件：只记录荔枝JSON接口的脱敏结构，不保存原始流或凭证。

只在用户明确同意手机临时代理/证书后用于接口调查；不编入固件，不代理日常播放。
"""
import json
import os
from pathlib import Path
import re
from urllib.parse import parse_qsl, urlsplit

MAX_JSON_BYTES = 1024 * 1024
DOMAINS = ("lizhi.fm", "lizhifm.com", "lz310.com")


def allowed_host(host):
    host = host.lower().rstrip(".")
    return any(host == domain or host.endswith("." + domain) for domain in DOMAINS)


def field_name(key):
    return key if isinstance(key, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.$-]{0,63}", key) else "[key]"


def schema(value, depth=0):
    if depth >= 5:
        return "depth_limit"
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return {"type": "array", "length": len(value),
                "sample_schema": [schema(x, depth + 1) for x in value[:1]]}
    if isinstance(value, dict):
        items = list(value.items())[:40]
        return {"type": "object", "fields": {field_name(k): schema(v, depth + 1) for k, v in items},
                "field_count": len(value)}
    return "unsupported"


def path_template(path):
    return "/".join(":id" if re.fullmatch(r"[0-9]{10,}", part) else
                    ":opaque" if len(part) > 40 or not re.fullmatch(r"[A-Za-z0-9_.~-]*", part) else part
                    for part in path.split("/"))


def summarize(method, url, request_headers, response_status, response_type, response_body, request_body=b"", request_type=""):
    parsed = urlsplit(url)
    if not allowed_host(parsed.hostname or "") or "json" not in response_type.lower():
        return None
    result = {"method": method, "host": parsed.hostname, "path_template": path_template(parsed.path),
              "query_fields": sorted({field_name(k) for k, _ in parse_qsl(parsed.query)})[:40],
              "auth_headers_present": [h for h in ("cookie", "authorization", "x-token", "x-auth-token")
                                       if any(k.lower() == h for k in request_headers)],
              "http_status": response_status, "response_bytes": len(response_body)}
    if len(request_body) <= MAX_JSON_BYTES:
        if "json" in request_type.lower():
            try:
                result["request_schema"] = schema(json.loads(request_body))
            except (ValueError, UnicodeError, RecursionError):
                result["request_json_invalid"] = True
        elif "application/x-www-form-urlencoded" in request_type.lower():
            result["request_fields"] = sorted({field_name(k) for k, _ in parse_qsl(request_body.decode("utf-8", "replace"))})[:40]
    if len(response_body) > MAX_JSON_BYTES:
        result["response_skipped"] = "too_large"
        return result
    try:
        result["response_schema"] = schema(json.loads(response_body))
    except (ValueError, UnicodeError, RecursionError):
        result["response_json_invalid"] = True
    return result


class FollowCapture:
    def response(self, flow):
        if not flow.response or not allowed_host(flow.request.host):
            return
        result = summarize(flow.request.method, flow.request.pretty_url, flow.request.headers.keys(),
                           flow.response.status_code, flow.response.headers.get("content-type", ""),
                           flow.response.content or b"", flow.request.content or b"",
                           flow.request.headers.get("content-type", ""))
        if result is None:
            return
        root = Path(__file__).resolve().parent.parent
        target = root / ".local-backup/lizhi-auth-research/follow-capture-summary.jsonl"
        target.parent.mkdir(parents=True, exist_ok=True)
        fd = os.open(target, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
        with os.fdopen(fd, "a", encoding="utf-8") as output:
            output.write(json.dumps(result, ensure_ascii=False) + "\n")


addons = [FollowCapture()]
