#!/usr/bin/env python3
"""核对复制时的参考基线；后续有意修改后，差异不代表产品回归失败。"""
import hashlib
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'docs/source-manifest.json').read_text(encoding='utf-8'))
failed = []
for item in manifest['files']:
    path = root / item['destination']
    if not path.is_file():
        failed.append(f"缺失：{item['destination']}")
    elif hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
        failed.append(f"与复制基线不同：{item['destination']}")
for message in failed:
    print(message, file=sys.stderr)
print(f"复制一致性：{len(manifest['files']) - len(failed)} 通过，{len(failed)} 失败")
sys.exit(bool(failed))
