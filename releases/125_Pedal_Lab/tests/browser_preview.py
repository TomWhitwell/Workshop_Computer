#!/usr/bin/env python3
"""Compare browser audio with native C++ across complete machine state."""
import sys,pathlib,tempfile,json,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import compile_bank
exe=pathlib.Path(sys.argv[1]);bank=pathlib.Path(sys.argv[2]);node=sys.argv[3]
with tempfile.TemporaryDirectory() as tmp:
    tmp=pathlib.Path(tmp);vectors=tmp/'vectors.bin';words=tmp/'words.json'
    subprocess.run([str(exe),str(vectors)],check=True)
    rows=json.loads(bank.read_text());words.write_text(json.dumps([compile_bank.assemble(ROOT/r['source']) for r in rows]))
    subprocess.run([node,str(ROOT/'tests/browser_preview.mjs'),str(vectors),str(words)],check=True)
