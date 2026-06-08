"""Resolve web editor URLs from info.yaml Editor field (mirrors webEditor.js)."""
import os
import re

DEFAULT_PAGES_BASE = 'https://tomwhitwell.github.io/Workshop_Computer/'


def github_pages_base(repo_slug=None):
    if not repo_slug or '/' not in repo_slug:
        return DEFAULT_PAGES_BASE
    owner, name = repo_slug.split('/', 1)
    return f'https://{owner.lower()}.github.io/{name}/'


def slugify(name):
    s = re.sub(r'[^a-z0-9]+', '-', name.lower())
    return s.strip('-')


def _key(k):
    return re.sub(r'\s+', '', str(k or '').lower())


def _normalize_info(data):
    if not data:
        return {}
    return {_key(k): v for k, v in data.items()}


def _is_url(s):
    return bool(re.match(r'^https?://', str(s or '').strip(), re.I))


def _resolve_entry(src_dir, web_entry):
    if web_entry:
        p = os.path.join(src_dir, web_entry)
        if os.path.isfile(p):
            return web_entry
    if os.path.isfile(os.path.join(src_dir, 'index.html')):
        return 'index.html'
    return web_entry or ''


def _local_url(card_path, slug, rel_folder, web_entry, pages_base):
    rel = (rel_folder or 'web').strip('/\\') or 'web'
    src = os.path.join(card_path, rel)
    if not os.path.isdir(src):
        return ''
    entry = _resolve_entry(src, web_entry)
    return f'{pages_base}programs/{slug}/web/{entry}' if entry else ''


def _editor_from_data(data):
    """Raw Editor value from info.yaml (any key casing)."""
    if not data:
        return ''
    for key, val in data.items():
        if _key(key) == 'editor':
            return str(val or '').strip()
    return ''


def resolve_editor_url(folder_name, card_path, data, pages_base=None):
    """Return editor URL for sitegen (external, local path, or auto web/)."""
    pages_base = pages_base or DEFAULT_PAGES_BASE
    info = _normalize_info(data)
    slug = slugify(folder_name)
    editor = str(info.get('editor') or '').strip()
    web_entry = str(info.get('webentry') or '').strip()

    if editor.lower() == 'none':
        return ''
    if editor and _is_url(editor):
        return editor
    if editor and not _is_url(editor):
        return _local_url(card_path, slug, editor, web_entry, pages_base)

    default_web = os.path.join(card_path, 'web')
    if os.path.isdir(default_web):
        return _local_url(card_path, slug, 'web', web_entry, pages_base)
    return ''


def readme_editor_url(folder_name, card_path, data, pages_base=None):
    """For releases/README.md: never replace an external Editor URL with GitHub Pages."""
    editor = _editor_from_data(data)
    if editor.lower() == 'none':
        return ''
    if editor and _is_url(editor):
        return editor
    return resolve_editor_url(folder_name, card_path, data, pages_base)
