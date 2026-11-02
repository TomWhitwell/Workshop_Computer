#!/usr/bin/env python3
"""Build a bank of SpinASM programs into a UF2; render WAV files on the host."""
import argparse, array, json, math, os, pathlib, shutil, subprocess, sys, wave, re, hashlib, datetime
import catalogue
ROOT=pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PROGRAMS=[ROOT/'programs'/name for name in ['00_55_gallon_saint.spn','01_last_known_copy.spn','02_ghost_spring.spn','03_gravity_clerk.spn','04_cold_case.spn','06_reverse_witness.spn']]

def run(args,**kwargs):
    subprocess.run([str(x) for x in args],check=True,cwd=ROOT,**kwargs)

def build(host=False,programs=None,diagnostic=False,telemetry=False):
    directory=pathlib.Path(os.environ.get('PEDAL_BUILD_ROOT',ROOT/'build'))/('host' if host else 'pico')
    cmd=['cmake','-S',ROOT,'-B',directory,'-DCMAKE_BUILD_TYPE=Release',f'-DPEDAL_HOST={"ON" if host else "OFF"}']
    cmd+=['-DPEDAL_PROGRAMS='+';'.join(str(p.resolve()) for p in (programs or DEFAULT_PROGRAMS))]
    cmd += [f'-DPEDAL_DIAGNOSTIC={"ON" if diagnostic else "OFF"}',f'-DPEDAL_TELEMETRY={"ON" if telemetry else "OFF"}']
    if not host:
        # Standard Pico environment wins; discover VS Code Pico installations too.
        pico=pathlib.Path.home()/'.pico-sdk'
        sdk=os.environ.get('PICO_SDK_PATH')
        if sdk:
            version_file=pathlib.Path(sdk)/'pico_sdk_version.cmake'
            version_text=version_file.read_text() if version_file.exists() else ''
            major=re.search(r'set\(PICO_SDK_VERSION_MAJOR (\d+)\)',version_text)
            if major and int(major[1])<2:
                print(f'Ignoring old Pico SDK at {sdk}; looking for SDK 2.x',flush=True)
                sdk=None
        if not sdk:
            candidates=sorted((pico/'sdk').glob('*'),key=lambda p:tuple(int(x) if x.isdigit() else 0 for x in p.name.split('.')))
            sdk=next((str(p) for p in reversed(candidates) if (p/'pico_sdk_init.cmake').exists()),None)
        if sdk:
            cache=directory/'CMakeCache.txt'
            if cache.exists():
                old=re.search(r'^PICO_SDK_PATH:[^=]+=(.+)$',cache.read_text(),re.M)
                if old and pathlib.Path(old[1]).resolve()!=pathlib.Path(sdk).resolve():cmd.insert(1,'--fresh')
            cmd+=['-DPICO_SDK_PATH='+sdk]
            version=pathlib.Path(sdk).name
            picotool=pico/'picotool'/version/'picotool'
            if (picotool/'picotoolConfig.cmake').exists():cmd+=['-Dpicotool_DIR='+str(picotool)]
        if not os.environ.get('PICO_TOOLCHAIN_PATH'):
            toolchains=sorted((pico/'toolchain').glob('*'))
            if toolchains:cmd+=['-DPICO_TOOLCHAIN_PATH='+str(toolchains[-1])]
    run(cmd);run(['cmake','--build',directory,'--parallel','4'])
    return directory

def render(args):
    directory=build(True,args.programs)
    with wave.open(str(args.input),'rb') as f:
        if f.getsampwidth()!=2 or f.getnchannels() not in (1,2) or f.getcomptype()!='NONE':
            raise ValueError('Input must be mono/stereo, 16-bit PCM WAV')
        expected=args.rate if args.native else 48000
        if f.getframerate()!=expected:raise ValueError(f'Input must be {expected} Hz')
        pcm=array.array('h',f.readframes(f.getnframes()));channels=f.getnchannels()
        if sys.byteorder!='little':pcm.byteswap()
    q=array.array('i')
    # Match hardware: -6dB input headroom and +6dB output recovery.
    for i in range(0,len(pcm),channels):
        q.extend((pcm[i]*128,pcm[i+(channels-1)]*128))
    q.extend([0]*int(args.tail*expected)*2)
    result=subprocess.run([str(directory/'render'),str(args.slot-1),str(args.rate),*[str(round(p*511)) for p in args.pots],'native' if args.native else 'hardware'],input=q.tobytes(),stdout=subprocess.PIPE,check=True)
    rendered=array.array('i');rendered.frombytes(result.stdout)
    out=array.array('h');clipped=0
    for v in rendered:
        v=round(v/128)
        clipped+=not -32768<=v<=32767
        out.append(max(-32768,min(32767,v)))
    if sys.byteorder!='little':out.byteswap()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(args.output),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(expected);f.writeframes(out.tobytes())
    print(f'{args.output}: {clipped} clipped channel samples / {len(out)}')

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    b=sub.add_parser('build',help='Build UF2 from 1–6 catalogue IDs or local files');b.add_argument('programs',nargs='*')
    b.add_argument('--bank',help='Named preset from the banks command')
    b.add_argument('--output',type=pathlib.Path,help='Output UF2 path; a matching JSON guide is also written')
    b.add_argument('--telemetry',action='store_true')
    diag=sub.add_parser('diagnose',help='Direct stereo-through UF2 with USB timing and signal telemetry')
    diag.add_argument('--output',type=pathlib.Path,default=ROOT/'dist/spin-diagnostic.uf2')
    batch=sub.add_parser('build-all',help='Build every selected preset into separately named UF2s')
    batch.add_argument('--banks',nargs='+',help='Preset names; default: all numbered catalogue pages')
    batch.add_argument('--output-dir',type=pathlib.Path,default=ROOT/'dist/banks')
    listing=sub.add_parser('list',help='Search local algorithms by name, author, controls or category')
    listing.add_argument('query',nargs='?',default='');listing.add_argument('--ready',action='store_true');listing.add_argument('--json',action='store_true')
    show=sub.add_parser('show',help='Credits, controls, source, licence and test status');show.add_argument('id')
    sub.add_parser('banks',help='List preset banks and six-slot catalogue pages')
    sub.add_parser('test-catalogue',help='Compile all compatible algorithms on host and compare native DSP with reference')
    t=sub.add_parser('test',help='Build and run host validation')
    t.add_argument('--programs',nargs='+');t.add_argument('--bank')
    r=sub.add_parser('render',help='Process a 16-bit PCM WAV with the compiled bank')
    r.add_argument('input',type=pathlib.Path);r.add_argument('output',type=pathlib.Path)
    r.add_argument('--slot',type=int,default=1);r.add_argument('--rate',type=int,default=32768)
    r.add_argument('--pots',type=float,nargs=3,default=[.5,.5,.5]);r.add_argument('--tail',type=float,default=3)
    r.add_argument('--native',action='store_true',help='Bypass 48kHz conversion; WAV rate must equal virtual rate')
    r.add_argument('--programs',nargs='+');r.add_argument('--bank')
    a=p.parse_args()
    try:
        if a.command=='diagnose':
            d=build(False,diagnostic=True,telemetry=True);save_firmware(d,a.output)
        elif a.command=='list':
            rows=[e for e in catalogue.entries() if a.query.lower() in json.dumps(e,ensure_ascii=False).lower() and (not a.ready or e['assembly']['status']=='assembled')]
            if a.json:print(json.dumps(rows,indent=2,ensure_ascii=False))
            else:
                for e in rows:print(f"{e['id']:65} {e['assembly']['status']:9} {e['name']} — {e['author']}")
                print(f'{len(rows)} programs. Use show ID for controls, credits and licence.')
        elif a.command=='show':
            e=next((e for e in catalogue.entries() if e['id']==a.id),None)
            if not e:raise ValueError('Unknown algorithm: '+a.id)
            print(catalogue.describe(e))
        elif a.command=='banks':
            for name,bank in catalogue.load()['banks'].items():print(f"{name:35} {len(bank['programs'])} slots — {bank['description']}")
        elif a.command=='test-catalogue':run([sys.executable,ROOT/'tools/validate_catalogue.py'])
        elif a.command=='build-all':
            banks=catalogue.load()['banks'];names=a.banks or [n for n in banks if n.startswith('page-')]
            if not names or any(n not in banks for n in names):raise ValueError('No matching preset banks')
            outcomes=[]
            for name in names:
                try:
                    paths=catalogue.select(bank=name);d=build(False,paths)
                    destination=a.output_dir/(name+'.uf2');save_firmware(d,destination)
                    outcomes.append({'bank':name,'status':'built','file':str(destination)})
                except (ValueError,OSError,subprocess.CalledProcessError) as error:
                    outcomes.append({'bank':name,'status':'failed','error':str(error)})
                a.output_dir.mkdir(parents=True,exist_ok=True)
                (a.output_dir/'build-results.json').write_text(json.dumps(outcomes,indent=2)+'\n')
            if any(o['status']=='failed' for o in outcomes):raise ValueError('Some banks failed; see build-results.json')
        elif a.command=='build':
            paths=catalogue.select(a.programs,a.bank)
            d=build(False,paths,telemetry=a.telemetry)
            destination=a.output or (ROOT/'dist/banks'/(a.bank+'.uf2') if a.bank else ROOT/'dist/pedal_lab.uf2')
            save_firmware(d,destination)
        elif a.command=='test':
            d=build(True,catalogue.select(a.programs,a.bank));run(['ctest','--test-dir',d,'--output-on-failure'])
        else:
            if not 1<=a.slot<=6 or not 8000<=a.rate<=40000 or not all(0<=v<=1 for v in a.pots) or not 0<=a.tail<=120:
                raise ValueError('slot 1–6, rate 8000–40000 Hz, pots 0–1, tail 0–120 seconds')
            a.programs=catalogue.select(a.programs,a.bank);render(a)
    except (ValueError,OSError,subprocess.CalledProcessError) as e:
        p.exit(1,str(e)+'\n')
def save_firmware(directory,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(directory/'pedal_lab.uf2',destination)
    shutil.copy2(directory/'bank/bank.json',destination.with_suffix('.json'))
    if destination==ROOT/'dist/pedal_lab.uf2':shutil.copy2(directory/'bank/bank.json',ROOT/'dist/bank.json')
    sources=['src/main.cpp','src/worker.hpp','src/audio.hpp','src/machine.hpp','src/runtime.hpp','vendor/ComputerCard/ComputerCard.h','CMakeLists.txt','tools/compile_bank.py']
    record={'architecture':'ComputerCard I/O + full DSP on core 1','cpu_hz':133000000,'hardware_rate':48000,'pipeline_frames':16,'built_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'uf2_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},'specific_bank_hardware_tested':False}
    destination.with_suffix('.build.json').write_text(json.dumps(record,indent=2)+'\n')
    print(destination,flush=True)
if __name__=='__main__':main()
