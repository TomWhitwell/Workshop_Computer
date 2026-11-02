#!/usr/bin/env python3
"""Compile every assemblable catalogue entry on host and compare with reference."""
import json,os,pathlib,subprocess
import catalogue,compile_bank
ROOT=catalogue.ROOT

def main():
    data=catalogue.load();ready=[e for e in data['programs'] if e['assembly']['status']=='assembled']
    out=ROOT/'build/catalogue';out.mkdir(parents=True,exist_ok=True)
    compile_bank.generate([ROOT/e['path'] for e in ready],out,limit=len(ready))
    (out/'catalogue_bank.hpp').write_bytes((out/'bank.hpp').read_bytes())
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2','-I'+str(ROOT/'src'),'-I'+str(ROOT/'generated'),'-I'+str(ROOT/'vendor/dsp-reference/include'),'-I'+str(out),str(ROOT/'tools/catalogue_validate.cpp'),str(ROOT/'vendor/dsp-reference/engine.cpp'),'-o',str(out/'check')],check=True)
    results=[]
    with subprocess.Popen([str(out/'check'),str(out)],stdout=subprocess.PIPE,text=True) as p:
        for line in p.stdout:
            v=json.loads(line);e=ready[v.pop('index')];v['status']='exact-reference-match' if v['state_mismatch_samples']==0 else 'reference-difference';v['scope']='Native 32.768 kHz; inherited 16-bit delay model; software only, no hardware timing or fidelity claim.'
            e['validation']=v;results.append({'id':e['id'],**v});print(e['id'],v['status'],flush=True)
        if p.wait():raise RuntimeError('Catalogue test process failed')
    (ROOT/'algorithms/catalogue.json').write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')
    (ROOT/'algorithms/validation.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
