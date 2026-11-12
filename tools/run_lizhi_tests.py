#!/usr/bin/env python3
"""编译荔枝解析、HTTP及分页逻辑主机测试，不联网、不读取设备或写入数据库。"""
import os
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
cc = os.environ.get('CC', 'cc')
idf = Path(os.environ.get('IDF_PATH', '/nonexistent'))
cjson = idf / 'components/json/cJSON'
if not shutil.which(cc) or not (cjson / 'cJSON.c').is_file():
    sys.exit('缺少主机编译器或 ESP-IDF cJSON；请设置 CC 和 IDF_PATH，不自动安装。')
out_root = root / '.local-backup'
out_root.mkdir(exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='lizhi-host-', dir=out_root))
component = root / 'firmware/components/lizhi_direct_client'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('detail_file', nargs='?', help='本机详情响应，可选，不打印内容')
parser.add_argument('--page-files', nargs=4, help='依次为真实第1/2/227/228页，每页2条；仅作样本验证')
args = parser.parse_args()
print(f'主机产物：{out}', flush=True)
for mode, files in [('detail', [args.detail_file] if args.detail_file else []),
                    ('page', args.page_files or []), ('http', [])]:
    http_args = []
    if mode == 'http':
        http_args = ['-I' + str(root / 'firmware/ai-passport/tests/lizhi_stubs'),
                     '-I' + str(root / 'firmware/ai-passport/tests/direct_stubs'),
                     str(component / 'lizhi_http.c'), str(component / 'lizhi_follow.c')]
    command = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-DCJSON_NESTING_LIMIT=24',
               '-I' + str(component / 'include'), '-I' + str(cjson),
               str(root / f'firmware/ai-passport/tests/test_lizhi_{mode}.c'),
               str(component / 'lizhi_detail.c'), *http_args, str(cjson / 'cJSON.c'), '-lm', '-o', str(out / ('test-' + mode))]
    subprocess.run(command, check=True)
    subprocess.run([str(out / ('test-' + mode)), *files], check=True)

app = root / 'firmware/ai-passport/main'
for name, sources in [('auth', [component / 'lizhi_auth.c', cjson / 'cJSON.c']),
                      ('auth_http', [component / 'lizhi_auth.c', component / 'lizhi_auth_http.c', cjson / 'cJSON.c']),
                      ('settings', [app / 'lizhi_settings.c']),
                      ('library', [app / 'lizhi_library.c'])]:
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-DCJSON_NESTING_LIMIT=24',
                    '-I'+str(component / 'include'), '-I'+str(app), '-I'+str(cjson),
                    '-I'+str(root / 'firmware/ai-passport/tests/lizhi_stubs'),
                    '-I'+str(root / 'firmware/ai-passport/tests/direct_stubs'),
                    str(root / f'firmware/ai-passport/tests/test_lizhi_{name}.c'),
                    *map(str, sources), '-lm', '-o', str(out / ('test-'+name))], check=True)
    subprocess.run([str(out / ('test-'+name))], check=True)
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(component / 'include'), str(component / 'lizhi_follow.c'),
                str(root / 'firmware/ai-passport/tests/test_lizhi_follow.c'),
                '-o', str(out / 'test-follow')], check=True)
subprocess.run([str(out / 'test-follow')], check=True)
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(app), '-I' + str(component / 'include'),
                str(root / 'firmware/ai-passport/tests/test_lizhi_app_logic.c'),
                str(app / 'lizhi_app_logic.c'), '-o', str(out / 'test-app-logic')], check=True)
subprocess.run([str(out / 'test-app-logic')], check=True)
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(app), '-I' + str(component / 'include'),
                str(root / 'firmware/ai-passport/tests/test_lizhi_follows_browser.c'),
                str(app / 'lizhi_app_logic.c'), '-o', str(out / 'test-follows-browser')], check=True)
subprocess.run([str(out / 'test-follows-browser')], check=True)
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(app), '-I' + str(component / 'include'),
                str(root / 'firmware/ai-passport/tests/test_lizhi_list_loader.c'),
                str(app / 'lizhi_list_loader.c'), '-o', str(out / 'test-list-loader')], check=True)
subprocess.run([str(out / 'test-list-loader')], check=True)

audio = root / 'firmware/components/audio_stream'
for name, sources in [('audio-position', [audio / 'audio_position.c', root / 'firmware/ai-passport/tests/test_audio_position.c']),
                      ('lizhi-progress', [audio / 'audio_position.c', app / 'lizhi_progress.c', root / 'firmware/ai-passport/tests/test_lizhi_progress.c'])]:
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(audio / 'include'),
                    '-I' + str(app), '-I' + str(component / 'include'),
                    *map(str, sources), '-o', str(out / name)], check=True)
    subprocess.run([str(out / name)], check=True)

subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-DESP_PLATFORM',
                '-I'+str(root / 'firmware/ai-passport/tests/direct_stubs'),
                '-I'+str(app), '-I'+str(component / 'include'), '-I'+str(audio / 'include'),
                str(app / 'lizhi_settings.c'), str(app / 'lizhi_library.c'), str(app / 'lizhi_progress.c'),
                str(audio / 'audio_position.c'), str(root / 'firmware/ai-passport/tests/test_lizhi_storage.c'),
                '-o', str(out / 'storage')], check=True)
subprocess.run([str(out / 'storage')], check=True)

# 编译真实adapter；替身只在本机测试产物目录生成，不进入设备工程。
adapter_stubs = out / 'adapter-stubs'
for header in ('esp_event.h', 'esp_netif.h', 'esp_sntp.h', 'esp_timer.h',
               'esp_wifi.h', 'esp_wifi_default.h', 'nvs_flash.h',
               'freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/queue.h'):
    destination = adapter_stubs / header
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('#include "adapter_platform.h"\n')
subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-ffunction-sections', '-fdata-sections', '-DESP_PLATFORM',
                '-I'+str(adapter_stubs), '-I'+str(root / 'firmware/ai-passport/tests/adapter_stubs'),
                '-I'+str(root / 'firmware/ai-passport/tests/direct_stubs'),
                '-I'+str(app), '-I'+str(component / 'include'),
                str(root / 'firmware/ai-passport/tests/test_lizhi_adapter.c'),
                str(app / 'lizhi_settings.c'), str(app / 'wifi_provisioning_logic.c'),
                '-Wl,--gc-sections', '-o', str(out / 'adapter')], check=True)
subprocess.run([str(out / 'adapter')], check=True)
