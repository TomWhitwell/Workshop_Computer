#!/usr/bin/env python3
"""Refresh distribution notices or package a UF2 with its license materials."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

RELEASES = Path(__file__).resolve().parents[1]


def cards():
    result = [d for d in sorted(RELEASES.iterdir())
              if (d / 'info.yaml').is_file()
              and re.search(r'Creator:\s*Adrian Vos', (d / 'info.yaml').read_text())]
    return result + [RELEASES / '433_sense_of_space/mayakovsky_cc0']


def owner(path):
    for parent in (path.parent, *path.parents):
        if (parent / 'info.yaml').is_file() and (parent / 'LICENSE').is_file():
            return parent
    raise ValueError(f'No release license found for {path}')


def notice_text(card):
    parts = ['Firmware distribution notices\n=============================\n',
             (card / 'LICENSE').read_text(),
             (card / 'THIRD_PARTY_NOTICES.md').read_text(),
             (card / 'licenses/TOOLCHAIN_NOTICES.txt').read_text()]
    buddies = card / 'licenses/BUDDIES-LICENSE.md'
    if buddies.exists():
        parts.append(buddies.read_text())
    return '\n\n'.join(parts).rstrip() + '\n'


def refresh():
    written = set()
    for card in cards():
        (card / 'NOTICE.txt').write_text(notice_text(card))
        written.add(card / 'NOTICE.txt')
        for uf2 in card.rglob('*.uf2'):
            if any(p.startswith('build') for p in uf2.relative_to(card).parts):
                continue
            target = uf2.parent / 'NOTICE.txt'
            # A nested audio variant has its own grant and asset provenance.
            target.write_text(notice_text(owner(uf2)))
            written.add(target)
    print(f'Refreshed {len(written)} consolidated NOTICE.txt files')


def package(uf2, output):
    uf2 = uf2.resolve()
    if not uf2.is_file() or uf2.suffix.lower() != '.uf2':
        raise ValueError('Input must be an existing UF2 file')
    card = owner(uf2)
    # Merely supplying a notice cannot grant BBC redistribution permission.
    if card.name == '433_sense_of_space':
        raise ValueError('BBC audio redistribution permission is not established. '
                         'Package the mayakovsky_cc0 variant instead.')
    notice = notice_text(card)
    meta = {
        'firmware': uf2.name,
        'sha256': hashlib.sha256(uf2.read_bytes()).hexdigest(),
        'sdk_version_used_for_this_binary': 'not established',
        'toolchain_used_for_this_binary': 'not established',
        'source_revision_used_for_this_binary': 'not established',
        'notice_packaging_revision': subprocess.check_output(
            ['git', '-C', str(RELEASES), 'rev-parse', 'HEAD'], text=True).strip(),
        'notice_packaging_worktree_has_changes': bool(subprocess.check_output(
            ['git', '-C', str(RELEASES), 'status', '--porcelain'], text=True).strip()),
        'source_note': 'Included GPL sources are the current release sources; '
                       'verify that they correspond to the binary before publishing.',
    }
    output = output or uf2.with_suffix('.zip')
    if output.resolve() == uf2:
        raise ValueError('Output must not overwrite the input firmware')
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as archive:
        archive.write(uf2, uf2.name)
        archive.writestr('NOTICE.txt', notice)
        archive.writestr('BUILD_PROVENANCE.json', json.dumps(meta, indent=2) + '\n')
        if 'GPL-3.0' in (card / 'LICENSE').read_text():
            # Include all local build inputs, docs and notices, excluding firmware,
            # generated build directories and pre-existing package archives.
            for path in sorted(card.rglob('*')):
                rel = path.relative_to(card)
                if not path.is_file() or any(p.startswith('build') or p in
                        ('.git', 'node_modules', '__pycache__') for p in rel.parts):
                    continue
                if path.suffix.lower() in ('.uf2', '.zip', '.elf', '.o', '.map') or path.name == '.DS_Store':
                    continue
                archive.write(path, Path('source') / rel)
    print(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='refresh notices beside UF2s')
    parser.add_argument('uf2', nargs='?', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.refresh:
        refresh()
    if args.uf2:
        package(args.uf2, args.output)
    if not args.refresh and not args.uf2:
        parser.error('provide --refresh or a UF2 path')


if __name__ == '__main__':
    main()
