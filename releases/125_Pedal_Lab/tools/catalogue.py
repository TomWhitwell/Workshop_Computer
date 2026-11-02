#!/usr/bin/env python3
"""Local FV-1 catalogue lookup; no network required for selection or building."""
import json,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
CATALOGUE=ROOT/'algorithms/catalogue.json'
def load():return json.loads(CATALOGUE.read_text())
def entries():return load()['programs']
def resolve(value):
    match=next((e for e in entries() if e['id']==str(value)),None)
    if match:
        if match['assembly']['status']!='assembled':raise ValueError(f"{value}: not currently assemblable; see 'show {value}'")
        return ROOT/match['path']
    p=pathlib.Path(value)
    if not p.is_file():raise ValueError(f"Unknown algorithm ID or file: {value}. Use 'list' to browse.")
    return p.resolve()
def select(values=None,bank=None):
    if bank:
        if values:raise ValueError('Choose either a preset bank or individual algorithms')
        banks=load()['banks']
        if bank not in banks:raise ValueError(f"Unknown bank {bank}; use 'banks'")
        values=banks[bank]['programs']
    return [resolve(v) for v in values] if values else None

def describe(e):
    c=e.get('controls',[])
    return '\n'.join([f"{e['id']} — {e['name']}",f"By {e['author']} | {', '.join(e['categories'])}",e['description'],
      'Main / X / Y: '+' / '.join(c),f"Licence: {e['license']['label']}",f"Source: {e['source_url']}",
      f"Assembly: {e['assembly']['status']}"+(f" ({e['assembly']['instructions']} instructions)" if 'instructions' in e['assembly'] else ''),
      'Validation: '+e.get('validation',{}).get('status','not run'),
      'Notes: '+'; '.join(e.get('notes',[])),e['assembly'].get('error','')]).strip()
