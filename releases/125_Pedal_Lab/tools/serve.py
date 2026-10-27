#!/usr/bin/env python3
"""Pedal Lab editor, native audio preview, bank compiler and installer."""
import argparse,array,base64,contextlib,hashlib,http.server,io,json,math,os,pathlib,re,subprocess,sys,threading,time,uuid,wave
import catalogue,compile_bank,index_catalogue
ROOT=catalogue.ROOT;WEB=ROOT/'web';JOBS={};BUILD_LOCK=threading.Lock();PREVIEW_LOCK=threading.Lock()
ALLOWED_ORIGINS={x for x in os.environ.get('PEDAL_ALLOWED_ORIGINS','').split(',') if x}
ALLOWED_HOSTS={x for x in os.environ.get('PEDAL_ALLOWED_HOSTS','').split(',') if x}

def check_source(source):
    if not isinstance(source,str) or len(source)>200000:raise ValueError('Effect code must be text below 200 kB')
    source,changes=index_catalogue.normalize(source)
    # Parsing requires a path; all temporary inputs remain under the project.
    folder=ROOT/'build/web/check';folder.mkdir(parents=True,exist_ok=True)
    path=folder/(uuid.uuid4().hex+'.spn');path.write_text(source);capture=io.StringIO()
    try:
        with contextlib.redirect_stdout(capture):words=compile_bank.assemble(path)
    except (SystemExit,ValueError) as e:raise ValueError(capture.getvalue().strip() or str(e)) from None
    finally:path.unlink(missing_ok=True)
    used=128
    while used and words[used-1]==17:used-=1
    return source,{'instructions':used,'normalizations':len(changes),'opcodes':sorted({w&31 for w in words[:used]})}

def prepare(programs,directory):
    if not isinstance(programs,list) or not 1<=len(programs)<=6:raise ValueError('Choose 1–6 programs')
    directory.mkdir(parents=True,exist_ok=True);paths=[];guides=[];known={e['id']:e for e in catalogue.entries()}
    for i,p in enumerate(programs):
        source,stats=check_source(p.get('source'));name=str(p.get('name') or f'Program {i+1}')[:100]
        path=directory/(f'{i+1:02}_'+re.sub(r'[^a-zA-Z0-9_-]+','_',name)+'.spn');path.write_text(source);paths.append(path)
        origin=known.get(p.get('id'));guide={k:origin[k] for k in ['id','author','description','controls','license','source_url'] } if origin else {'author':'User algorithm','controls':['POT0','POT1','POT2'],'license':{'label':'User supplied; user retains their rights'}}
        guide.update({'slot':i+1,'name':name,'modified':bool(origin and source!=(ROOT/origin['path']).read_text()),'hardware_tested':False,'assembly':stats});guides.append(guide)
    return paths,guides

def create_build(programs):
    key=uuid.uuid4().hex;folder=ROOT/'dist/web'/key
    paths,guides=prepare(programs,folder/'sources')
    job={'id':key,'status':'queued','message':'Waiting for compiler','guide':guides};JOBS[key]=job
    def worker():
      with BUILD_LOCK:
        job.update(status='building',message='Compiling your bank for Workshop Computer')
        env=os.environ.copy();env['PEDAL_BUILD_ROOT']=str(ROOT/'build/web/firmware')
        output=folder/'pedal-lab.uf2'
        cmd=[sys.executable,str(ROOT/'tools/pedal_lab.py'),'build',*[str(p) for p in paths],'--output',str(output)]
        with (folder/'build.log').open('w') as log:
            result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
        job['log']=(folder/'build.log').read_text()[-20000:]
        if result.returncode:job.update(status='failed',message='Build failed. See compiler details.');return
        image=output.read_bytes();guide={'slots':guides,'sha256':hashlib.sha256(image).hexdigest(),'bytes':len(image),'hardware_tested':False}
        (folder/'guide.json').write_text(json.dumps(guide,indent=2)+'\n')
        job.update(status='ready',message='Bank ready to install',download=f'/api/files/{key}/pedal-lab.uf2',guide_url=f'/api/files/{key}/guide.json',sha256=guide['sha256'],bytes=len(image))
    threading.Thread(target=worker,daemon=True).start();return job

def preview(request):
    source,stats=check_source(request.get('source'));pots=request.get('pots',[.5,.5,.5]);rate=int(request.get('rate',32768));duration=float(request.get('duration',8))
    if len(pots)!=3 or not all(isinstance(p,(int,float)) and math.isfinite(p) and 0<=p<=1 for p in pots):raise ValueError('Three pot values from 0 to 1 required')
    if not 8000<=rate<=40000 or not 1<=duration<=12:raise ValueError('Clock 8–40 kHz; preview duration 1–12 seconds')
    digest=hashlib.sha256(source.encode())
    for name in ['src/audio.hpp','src/machine.hpp','generated/math_tables.hpp','generated/fir_tables.hpp','tools/compile_bank.py','tools/render.cpp']:digest.update((ROOT/name).read_bytes())
    folder=ROOT/'build/web/preview'/digest.hexdigest();folder.mkdir(parents=True,exist_ok=True);exe=folder/'render'
    with PREVIEW_LOCK:
        if not exe.exists():
            path=folder/'program.spn';path.write_text(source);compile_bank.generate([path],folder)
            cmd=[os.environ.get('CXX','c++'),'-std=c++17','-O2',*[f'-I{ROOT/p}' for p in ['src','generated','vendor/dsp-reference/include']],'-I'+str(folder),str(ROOT/'tools/render.cpp'),'-o',str(exe)]
            result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            if result.returncode:raise ValueError('Native preview compiler failed:\n'+result.stdout[-4000:])
    if request.get('wav'):
        try:raw=base64.b64decode(request['wav'],validate=True)
        except Exception:raise ValueError('Invalid WAV upload') from None
        with wave.open(io.BytesIO(raw),'rb') as f:
            if f.getframerate()!=48000 or f.getnchannels()!=2 or f.getsampwidth()!=2:raise ValueError('Preview WAV must be 48 kHz stereo PCM16')
            pcm=array.array('h',f.readframes(48000*12))
        if sys.byteorder!='little':pcm.byteswap()
    else:
        pcm=array.array('h');stimulus=request.get('stimulus','plucks')
        for i in range(int(48000*duration)):
            t=i/48000
            if stimulus=='impulse':v=.12 if i==0 else 0
            elif stimulus=='sine':v=.12*math.sin(2*math.pi*220*t) if t<duration/2 else 0
            else:
                v=0
                for start,freq in [(0,110),(.75,164.8138),(1.5,220),(2.25,146.8324)]:
                    age=t-start
                    if 0<=age<1.5:v+=.08*math.exp(-age*6)*sum(math.sin(2*math.pi*freq*h*age)/h**1.5 for h in range(1,7))
            pcm.extend([round(32767*v)]*2)
    q=array.array('i',(v*128 for v in pcm))
    result=subprocess.run([str(exe),'0',str(rate),*[str(round(p*511)) for p in pots],'hardware'],input=q.tobytes(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20,check=True)
    rendered=array.array('i');rendered.frombytes(result.stdout);clipped=sum(v>4194176 or v<-4194304 for v in rendered)
    out=array.array('h',(max(-32768,min(32767,round(v/128))) for v in rendered))
    def encode(values):
        if sys.byteorder!='little':values=values[:];values.byteswap()
        b=io.BytesIO()
        with wave.open(b,'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000);f.writeframes(values.tobytes())
        return base64.b64encode(b.getvalue()).decode()
    peak=max((abs(v) for v in out),default=0)
    return {'wav':encode(out),'dry':encode(pcm),'clipped_samples':clipped,'peak_dbfs':20*math.log10(max(peak,1)/32768),'duration':len(out)/96000,'assembly':stats,'engine':'Native C++ — same fixed-point engine and resampler as firmware; not a hardware recording'}

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,format,*args):pass
    def send_json(self,value,status=200):self.send_bytes(json.dumps(value,ensure_ascii=False).encode(),'application/json',status)
    def send_bytes(self,b,mime,status=200):
        self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(b)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.cors();self.end_headers();self.wfile.write(b)
    def permitted_origins(self):
        local={f'http://127.0.0.1:{self.server.server_port}',f'http://localhost:{self.server.server_port}'}
        return local|ALLOWED_ORIGINS
    def cors(self):
        origin=self.headers.get('Origin')
        if origin in self.permitted_origins():
            self.send_header('Access-Control-Allow-Origin',origin)
            self.send_header('Vary','Origin')
            self.send_header('Access-Control-Allow-Methods','GET, POST, OPTIONS')
            self.send_header('Access-Control-Allow-Headers','Content-Type')
            if self.headers.get('Access-Control-Request-Private-Network')=='true':self.send_header('Access-Control-Allow-Private-Network','true')
    def allowed(self):
        hosts={f'127.0.0.1:{self.server.server_port}',f'localhost:{self.server.server_port}'}|ALLOWED_HOSTS
        origin=self.headers.get('Origin')
        return self.headers.get('Host') in hosts and (origin is None or origin in self.permitted_origins())
    def do_OPTIONS(self):
        if not self.allowed():self.send_json({'error':'Address not allowed'},403);return
        self.send_bytes(b'','text/plain',204)
    def do_GET(self):
        import urllib.parse,mimetypes
        if not self.allowed():self.send_json({'error':'Use the local app address'},403);return
        url=urllib.parse.urlsplit(self.path);path=url.path
        try:
            if path=='/api/health':self.send_json({'service':'pedal-lab'});return
            if path=='/api/catalogue':self.send_json(catalogue.load());return
            if path=='/api/source':
                identifier=urllib.parse.parse_qs(url.query).get('id',[''])[0];e=next((e for e in catalogue.entries() if e['id']==identifier),None)
                if not e:raise ValueError('Unknown algorithm')
                self.send_json({'source':(ROOT/e['path']).read_text(),'metadata':e});return
            if path.startswith('/api/jobs/'):
                job=JOBS.get(path.rsplit('/',1)[-1]);self.send_json(job or {'error':'Unknown build'},200 if job else 404);return
            if path.startswith('/api/files/'):
                match=re.fullmatch(r'/api/files/([0-9a-f]{32})/(pedal-lab.uf2|guide.json)',path)
                if not match:raise ValueError('Unknown download')
                file=ROOT/'dist/web'/match[1]/match[2]
            else:
                file=(WEB/(path.lstrip('/') or 'index.html')).resolve()
                if not file.is_relative_to(WEB.resolve()):raise ValueError('Unknown file')
            if not file.is_file():self.send_json({'error':'Not found'},404);return
            self.send_bytes(file.read_bytes(),mimetypes.guess_type(file)[0] or 'application/octet-stream')
        except (ValueError,OSError) as e:self.send_json({'error':str(e)},400)
    def do_POST(self):
        if not self.allowed():self.send_json({'error':'Use the local app address'},403);return
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=10000000:raise ValueError('Request is empty or too large')
            if self.headers.get_content_type()!='application/json':raise ValueError('JSON request required')
            request=json.loads(self.rfile.read(size))
            if self.path=='/api/check':_,stats=check_source(request.get('source'));result=stats
            elif self.path=='/api/build':result=create_build(request.get('programs'))
            elif self.path=='/api/preview':result=preview(request)
            else:self.send_json({'error':'Not found'},404);return
            self.send_json(result)
        except (ValueError,TypeError,KeyError,wave.Error,subprocess.SubprocessError,OSError) as e:self.send_json({'error':str(e)},400)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=8766);a=p.parse_args()
    server=http.server.ThreadingHTTPServer((a.host,a.port),Handler)
    print(f'Pedal Lab: http://127.0.0.1:{a.port}/ — Chrome or Edge for USB installation',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:server.server_close()
if __name__=='__main__':main()
