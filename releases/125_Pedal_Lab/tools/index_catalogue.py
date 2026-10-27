#!/usr/bin/env python3
"""Reindex downloaded sources; preserve originals and document normalization."""
import contextlib,hashlib,html,io,json,pathlib,re,sys,collections,struct
from decimal import Decimal
import compile_bank
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE=ROOT/'algorithms'
def slug(s):return re.sub(r'[^a-z0-9]+','-',s.lower()).strip('-')
def normalize(text):
    edits=[];lines=[]
    for line in text.splitlines():
        code,sep,comment=line.partition(';');before=code
        # SpinASM COS0/COS1 and numeric 8/9 shorthand for CHO RDAL cosine.
        code=re.sub(r'(?i)\b(cho\s+rdal\s*,\s*)(cos([01])|([89]))\b',lambda m:m[1]+'SIN'+(m[3] if m[3] else str(int(m[4])-8))+',COS',code)
        code=re.sub(r'(?<![\w.])\.(\d)',r'0.\1',code)
        code=re.sub(r'(?<![\w.])(\d+)\.(?![\w.])',r'\1.0',code)
        code=re.sub(r'(?<!<)<(?!<)','<<',code)
        code=re.sub(r'(?<!>)>(?!>)','>>',code)
        # Printed coefficient rounded just above 32767/16384: preserve its
        # nearest encoded value. 2.0 remains rejected, never silently clamped.
        def coefficient(m):
            x=Decimal(m[0]);limit=Decimal(32767)/16384
            return str(limit) if limit<x<2 and round(x*16384)==32767 else m[0]
        code=re.sub(r'(?<![\w.])1\.\d+',coefficient,code)
        if code!=before:edits.append({'before':before.strip(),'after':code.strip()})
        lines.append(code+sep+comment)
    return '\n'.join(lines)+'\n',edits

def main():
    rows=[];download={f"{x['collection']}/{x['path']}":x for x in json.loads((BASE/'downloads.json').read_text())}
    meta=json.loads((BASE/'upstream/directory/programs.json').read_text())
    directory={e.get('download',{}).get('spn',{}).get('file'):e for e in meta}
    other=json.loads((BASE/'upstream/otherworld/bank.json').read_text())
    categories={e['id']:e['category'] for e in other['slots']}
    spin_path=BASE/'upstream/spinsemi/programs.html'
    official={}
    if spin_path.exists():
        for row in re.findall(r'<tr class="main">(.*?)</tr>',spin_path.read_text(),re.S):
            cells=re.findall(r'<td[^>]*>(.*?)</td>',row,re.S)
            link=re.search(r'spn=([^&"<>]+)',row)
            if link and len(cells)>2:official[link[1]]=html.unescape(re.sub('<[^>]+>','',cells[2])).strip()
    (BASE/'ready').mkdir(exist_ok=True)
    for collection in ['directory','audiofab','otherworld']:
      for p in sorted((BASE/'upstream'/collection).rglob('*.spn')):
        rel=p.relative_to(BASE/'upstream'/collection).as_posix();src=download[collection+'/'+rel]
        raw=p.read_bytes()
        try:text=raw.decode('utf-8-sig');encoding='utf-8'
        except UnicodeDecodeError:text=raw.decode('cp1252');encoding='cp1252'
        controls=['Not documented']*3;notes=[]
        if collection=='directory':
            key=rel.removeprefix('public/files/');m=directory.get(key,{})
            name=m.get('name',p.stem);author=m.get('author') or 'Unknown (not credited in directory)'
            description=m.get('description') or ''
            cats=m.get('categories',['Uncategorized']);controls=(m.get('controls',[])+controls)[:3]
            source=m.get('source_url') or src['url'];lic={'label':'Public source; no explicit licence identified','status':'unspecified','evidence':src['url']}
            # Spin's permission statement applies only to files actually listed
            # on the Free DSP Programs page, not every Spin-associated source.
            spin=BASE/'upstream/spinsemi/programs.html'
            if spin.exists() and re.search(r'spn='+re.escape(p.name)+r'(?:&|\")',spin.read_text()):
                lic={'label':'Spin Semiconductor Open Reverb License (see publisher statement)','status':'publisher-permission','evidence':'http://www.spinsemi.com/programs.php'}
            if m.get('special_pcb'):notes.append('Upstream marks this as requiring a special circuit/PCB; card behaviour may differ.')
            if m.get('application'):notes.append('Original application: '+m['application'])
            if p.name in official:author=official[p.name]+' (Spin Semiconductor)'
            if 'coded by mdroberts1243' in text.lower():
                author='mdroberts1243 (FV-1 implementation); Jon Dattorro (reverb design)'
                if '/dervish/' in '/'+rel:notes.append('Dervish variant; base implementation credited in header, variant author not established.')
            for i in range(3):
                header=re.search(r'(?im)^;[ \t]*(?:pot|p)[ \t]*'+str(i)+r'[ \t]*[:=][ \t]*([^\r\n;]+)',text)
                if header and header[1].strip():controls[i]=header[1].strip()
        elif collection=='audiofab':
            m=json.loads(p.with_suffix('.json').read_text());name=m['name'];author=m.get('author','Audiofab');description=m.get('description','');cats=[m.get('category','Uncategorized')]
            for c in m.get('controls',[]):controls[c['pot']]=c['name']+(': '+c['description'] if c.get('description') else '')
            source='https://github.com/'+src['repo']+'/blob/'+src['commit']+'/'+rel
            lic={'label':m.get('license') or 'MIT (repository; effect-specific notices take precedence)','status':'effect-specific' if m.get('license') else 'MIT','evidence':'algorithms/upstream/audiofab/LICENSE'}
        else:
            m={};fields=collections.defaultdict(list)
            for key,value in re.findall(r'^;\s*@([a-z]+)\s+(.*)$',text,re.M):fields[key].append(value)
            name=' '.join(fields['name']) or p.stem;description=' '.join(fields['description']) or ' '.join(fields['tagline']);author='Andreas Jansson';cats=[categories.get(p.stem,'Experimental')]
            if fields['pots']:controls=[s.strip() for s in fields['pots'][0].split('|')]
            source='https://github.com/'+src['repo']+'/blob/'+src['commit']+'/'+rel
            lic={'label':'MIT — Andreas Jansson','status':'MIT','evidence':'algorithms/upstream/otherworld/LICENSE'}
            notes.append('Designed for FX AID; converter assumptions and clock variants may differ on Workshop Computer.')
        if re.search(r'(?i)not for commercial use|non[- ]commercial',text):
            lic={'label':'Non-commercial only — source header','status':'non-commercial','evidence':str(p.relative_to(ROOT))}
        if not description:
            # Honest editorial explanation, distinguished from an author quote.
            description=f"{', '.join(cats)} program for the FV-1. Explore the documented controls below; the source header contains implementation notes."
        if p.name=='dattorro.spn':description="Jon Dattorro's plate-reverb topology adapted to FV-1 by mdroberts1243, using input diffusion, a recirculating tank and modulated taps. Decay can approach infinite sustain."
        if p.name=='freeverb.spn':description="FV-1 implementation of the Freeverb approach: parallel filtered comb delays followed by allpass diffusion, with a stereo spread. POT0 sets comb feedback; other pots are not documented."
        identifier=collection+'-'+slug(rel.removeprefix('public/files/').removeprefix('effects/').removesuffix('.spn'))
        normalized,edits=normalize(text);dest=BASE/'ready'/(identifier+'.spn');dest.write_text(normalized)
        assembly={};buf=io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):words=compile_bank.assemble(dest)
            used=128
            while used and words[used-1]==17:used-=1
            assembly={'status':'assembled','instructions':used}
            assembly['bytecode_sha256']=hashlib.sha256(struct.pack('>128I',*words)).hexdigest()
        except (SystemExit,ValueError,OSError) as e:assembly={'status':'failed','error':buf.getvalue()+str(e)}
        rows.append({'id':identifier,'name':name,'author':author,'categories':cats,'description':description,'controls':controls,'license':lic,'source_url':source,'download_url':src['url'],'collection':collection,'upstream_commit':src['commit'],'original_path':str(p.relative_to(ROOT)),'path':str(dest.relative_to(ROOT)),'original_sha256':hashlib.sha256(raw).hexdigest(),'encoding':encoding,'normalizations':edits,'assembly':assembly,'notes':notes,'upstream_metadata':m})
    # Keep earlier test results only while the compiled source hash is identical.
    previous=json.loads((BASE/'catalogue.json').read_text()) if (BASE/'catalogue.json').exists() else {}
    old={e['id']:e for e in previous.get('programs',[])}
    for e in rows:
        e['sha256']=hashlib.sha256((ROOT/e['path']).read_bytes()).hexdigest()
        if old.get(e['id'],{}).get('sha256')==e['sha256'] and 'validation' in old[e['id']]:e['validation']=old[e['id']]['validation']
    seen={}
    for e in rows:
        code=e['assembly'].get('bytecode_sha256')
        if code:
            if code in seen:e['duplicate_of']=seen[code]
            else:seen[code]=e['id']
    result={'version':1,'collected':'2026-10-27','programs':rows,'banks':previous.get('banks',{})}
    (BASE/'catalogue.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    print(collections.Counter(e['assembly']['status'] for e in rows))
if __name__=='__main__':main()
