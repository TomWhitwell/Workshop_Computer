#!/usr/bin/env python3
import shlex,pathlib,json,subprocess,base64,gzip,argparse,tempfile,hashlib
parser=argparse.ArgumentParser(description='Pack a PEDAL_BROWSER_BASE build for the browser editor')
parser.add_argument('build_dir',type=pathlib.Path)
parser.add_argument('toolchain',type=pathlib.Path)
opts=parser.parse_args()
root=opts.build_dir.resolve();source=pathlib.Path(__file__).resolve().parents[1];tool=opts.toolchain.resolve()
work=tempfile.TemporaryDirectory();scratch=pathlib.Path(work.name)
args=shlex.split((root/'CMakeFiles/pedal_lab.dir/link.txt').read_text());files={};ld=[];index=0
while index<len(args):
 a=args[index];index+=1
 if a.endswith('.o'):
  if 'browser_bank_stub.cpp.o' in a:continue
  path=root/a if not pathlib.Path(a).is_absolute() else pathlib.Path(a)
  name='objects/'+str(len(files))+'.o';tmp=scratch/'stripped.o';subprocess.run([str(tool/'bin/arm-none-eabi-objcopy'),'--strip-debug',str(path),str(tmp)],check=True)
  files[name]=base64.b64encode(tmp.read_bytes()).decode();ld.append(name)
 elif a.startswith('-Wl,'):
  parts=a[4:].split(',');i=0
  while i<len(parts):
   x=parts[i];i+=1
   if x in ['-Map','--print-memory-usage','--no-warn-rwx-segments']:i+=int(x=='-Map');continue
   if x in ['-T','--script']:
    script=pathlib.Path(parts[i]);i+=1;files['memory.ld']=base64.b64encode(script.read_bytes()).decode();ld+=['-T','memory.ld'];continue
   if x.startswith('--script='):
    files['memory.ld']=base64.b64encode(pathlib.Path(x.split('=',1)[1]).read_bytes()).decode();ld+=['-T','memory.ld'];continue
   if x.startswith('-Map=') or x.startswith('-L'):continue
   ld.append(x)
 elif a=='-o':index+=1
for name in ['libgcc','libc','libm','libnosys','libstdc++']:
 candidates=list(tool.glob(('lib/gcc/arm-none-eabi/*/thumb/v6-m/nofp/' if name=='libgcc' else 'arm-none-eabi/lib/thumb/v6-m/nofp/')+name+'.a'))
 p=candidates[0];tmp=scratch/(name+'.a');subprocess.run([str(tool/'bin/arm-none-eabi-objcopy'),'--strip-debug',str(p),str(tmp)],check=True)
 files[name+'.a']=base64.b64encode(tmp.read_bytes()).decode()
files['pico_flash_region.ld']=base64.b64encode(b'FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2048k\n').decode()
ld+=['--start-group','libgcc.a','libc.a','libm.a','libnosys.a','libstdc++.a','--end-group']
# Bank functions are added by the browser; every other object is prebuilt.
ld+=['bank.o','-o','pedal-lab.elf']
(source/'web/compiler').mkdir(exist_ok=True)
(source/'web/compiler/firmware.json.gz').write_bytes(gzip.compress(json.dumps({'files':files,'ld':ld,'source_sha256':{str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source/'src').glob('*.hpp')}},separators=(',',':')).encode(),mtime=0))
print('Firmware pack',sum(len(v)*3//4 for v in files.values()),'bytes',len(files),'files')

python={str(p.relative_to(source)):p.read_text() for p in [source/'tools/compile_bank.py',source/'tools/index_catalogue.py',source/'vendor/assembler/assembler.py']}
python['assembler_api.py']=(source/'web/compiler/assembler_api.py').read_text()
cpp={}
for p in [source/'src/machine.hpp',source/'src/audio.hpp',source/'generated/math_tables.hpp',source/'generated/fir_tables.hpp']:cpp['include/'+p.name]=p.read_text()
cpp['include/processor/processor.h']=(source/'vendor/dsp-reference/include/processor/processor.h').read_text()
(source/'web/compiler/sources.json').write_text(json.dumps({'python':python,'cpp':cpp},separators=(',',':')))
