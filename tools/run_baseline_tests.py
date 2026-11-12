#!/usr/bin/env python3
"""运行已复制基线的 14 个主机程序和打包回归，不启动设备或访问业务账号。"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
fw = root / 'firmware/ai-passport'
idf = Path(os.environ.get('IDF_PATH', '/nonexistent'))
cc = os.environ.get('CC', 'cc')
if not shutil.which(cc):
    sys.exit(f'缺少主机 C 编译器：{cc}；请设置 CC 为可执行文件路径。')
cjson = idf / 'components/json/cJSON'
if not (cjson / 'cJSON.c').is_file():
    sys.exit('缺少 ESP-IDF cJSON；请先激活 ESP-IDF 5.5.4，或设置 IDF_PATH。')
artifact_root = root / '.local-backup'
artifact_root.mkdir(exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='baseline-', dir=artifact_root))
print(f'主机产物保留于：{out}', flush=True)
passed = 0

def run(args):
    subprocess.run([str(x) for x in args], cwd=fw, check=True)

def compile_run(name, sources, includes=('main',), defines=()):
    global passed
    exe = out / name
    run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
         *shlex.split(os.environ.get('BASELINE_CFLAGS', '')),
         *['-I' + str(x) for x in includes], *['-D' + x for x in defines],
         *sources, '-o', exe])
    run([exe])
    passed += 1

try:
    for name in ('ui_pixel_math', 'netease_app_logic', 'display_policy', 'music_lyrics',
                 'wifi_provisioning_logic', 'device_music_ui_spec', 'wifi_provisioning_portal'):
        compile_run(name, [f'tests/test_{name}.c', f'main/{name}.c'])
    compile_run('wifi_provisioning_portal_direct',
                ['tests/test_wifi_provisioning_portal.c', 'main/wifi_provisioning_portal.c'],
                defines=('CONFIG_NETEASE_DIRECT=1',))
    compile_run('audio_stream', ['tests/test_audio_stream.c', '../components/audio_stream/audio_stream.c',
                '../components/audio_stream/audio_stream_policy.c'],
                includes=('../components/audio_stream/include', '../components/audio_stream'))
    compile_run('netease_client', ['tests/test_netease_client.c', '../components/netease_client/netease_client.c',
                cjson / 'cJSON.c'], includes=('../components/netease_client/include', cjson),
                defines=('NETEASE_CLIENT_PARSER_ONLY',))
    env = dict(os.environ, DIRECT_TEST_OUT=str(out / 'direct'), AUDIO_DOWNLOAD_TEST_OUT=str(out / 'audio_download'))
    subprocess.run(['bash', 'tools/run_direct_host_tests.sh'], cwd=fw, env=env, check=True)
    passed += 3
    subprocess.run(['bash', 'tools/run_audio_download_tests.sh'], cwd=fw, env=env, check=True)
    passed += 1
    run([sys.executable, 'tests/test_package_release.py'])
except subprocess.CalledProcessError as error:
    print(f'验证失败，退出码 {error.returncode}；已通过主机程序 {passed}/14。', file=sys.stderr)
    sys.exit(error.returncode)
print(f'主机程序：{passed} 通过，0 失败；打包回归结果见上方。此结果不代表荔枝接口或设备验收。')
