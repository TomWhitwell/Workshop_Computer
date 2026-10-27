#!/usr/bin/env python3
"""Make a deterministic plucked-tone stimulus and render the default six slots.
Run tools/pedal_lab.py test first. Uses only the Python standard library.
"""
import array,json,math,pathlib,subprocess,sys,wave
root=pathlib.Path(__file__).resolve().parents[1]
out=root/'dist'/'audio';out.mkdir(parents=True,exist_ok=True)
rate=48000;pcm=array.array('h')
for i in range(rate*6):
    t=i/rate;v=0
    for start,freq in [(0,110),(.75,164.8138),(1.5,220),(2.25,146.8324)]:
        age=t-start
        if 0<=age<1.5:
            v+=.1*math.exp(-age*6)*sum(math.sin(2*math.pi*freq*h*age)/h**1.5 for h in range(1,9))
    pcm.extend([round(32767*v)]*2)
def wav(path,data):
    if sys.byteorder!='little':data.byteswap()
    with wave.open(str(path),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate);f.writeframes(data.tobytes())
wav(out/'dry.wav',pcm)
q=array.array('i',(v*128 for v in pcm))
manifest=json.loads((root/'build/host/bank/bank.json').read_text());report=[]
for row in manifest:
    result=subprocess.run([str(root/'build/host/render'),str(row['slot']),'32768','256','256','256','hardware'],input=q.tobytes(),stdout=subprocess.PIPE,check=True)
    rendered=array.array('i');rendered.frombytes(result.stdout)
    clips=sum(v>4194176 or v<-4194304 for v in rendered)
    data=array.array('h',(max(-32768,min(32767,round(v/128))) for v in rendered))
    wav(out/(row['name']+'.wav'),data)
    report.append({'program':row['name'],'clipped_samples':clips,'peak_dbfs':20*math.log10(max(abs(v) for v in data)/32768+1e-30)})
(out/'levels.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
