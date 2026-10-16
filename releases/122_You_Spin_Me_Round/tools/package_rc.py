#!/usr/bin/env python3
# Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
"""Stage a self-contained card folder and ZIP without touching Workshop_Computer."""
from pathlib import Path
import hashlib
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
version = '0.1.0-rc1'
card = '122_You_Spin_Me_Round'
firmware = f'You_Spin_Me_Round_{version}.uf2'
expected = '678ff40523d110a466d91bd1bf5e8671b69ac3ca8562f02f22cdf838826ec249'
if hashlib.sha256((root/'uf2'/firmware).read_bytes()).hexdigest() != expected:
    raise SystemExit('Firmware does not match tested alpha3 / RC1; refusing to package')
output = root/'dist'/f'You_Spin_Me_Round_{version}'
staged = output/card
# Refuse overwriting an existing bundle, so a previous review copy stays intact.
staged.mkdir(parents=True, exist_ok=False)
for name in ['README.md', 'info.yaml', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
             'CMakeLists.txt', 'pico_sdk_import.cmake', '.gitignore']:
    shutil.copy2(root/name, staged/name)
for name in ['src', 'vendor', 'tests', 'tools', 'docs', 'previews']:
    shutil.copytree(root/name, staged/name, ignore=shutil.ignore_patterns('__pycache__'))
(staged/'uf2').mkdir()
for name in [firmware, 'SHA256SUMS.txt']:
    shutil.copy2(root/'uf2'/name, staged/'uf2'/name)
archive = root/'dist'/f'You_Spin_Me_Round_{version}.zip'
with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(staged.rglob('*')):
        if path.is_file():
            z.write(path, path.relative_to(output))
print(staged)
print(archive)
