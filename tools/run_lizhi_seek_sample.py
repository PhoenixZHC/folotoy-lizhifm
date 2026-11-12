#!/usr/bin/env python3
"""用本机真实MP3片段验证Helix帧锚点预热，不下载音频或访问设备。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sample', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
cc = os.environ.get('CC', 'cc')
helix = root / 'firmware/lizhi-player/managed_components/chmorgan__esp-libhelix-mp3/libhelix-mp3'
if not shutil.which(cc) or not (helix / 'mp3dec.c').is_file() or not args.sample.is_file():
    parser.error('需要已有主机编译器、已解析的Helix依赖和真实MP3片段；不自动安装。')
sources = sorted(helix.glob('*.c')) + sorted((helix / 'real').glob('*.c'))
out = Path(tempfile.mkdtemp(prefix='lizhi-seek-', dir=root / '.local-backup')) / 'test'
tests = root / 'firmware/ai-passport/tests'
audio = root / 'firmware/components/audio_stream'
subprocess.run([cc, '-std=c11', '-O2', '-fwrapv', '-include', str(tests / 'helix_host_math.h'),
                '-I' + str(helix / 'pub'), '-I' + str(helix / 'real'), '-I' + str(audio / 'include'),
                str(tests / 'test_audio_seek_sample.c'), str(audio / 'audio_position.c'),
                *map(str, sources), '-o', str(out)], check=True)
subprocess.run([str(out), str(args.sample.resolve())], check=True)
