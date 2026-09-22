import {message,controlsPayload,parseStatus} from './protocol.js';
const $=id=>document.getElementById(id);
const roles=['LOW','SNAP','TICK','BASS','CHIME','GHOST'];
const engines=['SUB KICK','SUB SNARE','NOISE','FM','ADDITIVE','WAVEFOLD','FOLD KICK','BASS','KICK','FM KICK','SNARE','CLAP','HAT','TOM'];
const glyphs=['M3 8H10C15 8 12 19 21 19','M3 5H7V10H12V15H17V20H22','M5 4v2m8-3v2m7 3v2M4 13v2m8-5v2m7 4v2M8 20v2m8-1v2','M14 12a6 6 0 1 0-12 0a6 6 0 1 0 12 0M23 12a6 6 0 1 0-12 0a6 6 0 1 0 12 0','M4 3V22M10 8V22M16 12V22M22 17V22','M2 19L7 5L12 19L17 5L22 19','M2 19L7 5L12 19L17 5L22 19','M2 13C5 0 9 0 12 13S20 26 23 13','M2 13H7L10 3L13 23L16 13H23','M14 12a6 6 0 1 0-12 0a6 6 0 1 0 12 0M23 12a6 6 0 1 0-12 0a6 6 0 1 0 12 0','M3 5H7V10H12V15H17V20H22','M3 6H7M10 10H14M17 6H21M3 20H21','M3 9H22M3 15H22M12 3V22','M3 5H9V12H15V19H22'];
let state=null,desired={bpm10:0,mute:0,solo:0,delay:100},mode='offline',midi=null,input=null,output=null,seq=0,pendingSeq=null,pendingSince=0,waitingOp=null;
let lastReply=0,receivedAt=0,baseTime=0,sendTimer=0,pollTimer=0,connectTimeout=0,dragSource=-1,selectedLane=-1,lastDraw=0,previewClock=0;
let previewSeed=369,previewGeneration=1,lastScene=0,flashUntil=0,diagnosticBase=null;
const rows=roles.map((role,i)=>{
 const row=document.createElement('div');row.className='voice';row.dataset.lane=i;
 row.innerHTML=`<span class="voice-number">0${i+1}</span><button class="voice-identity" data-lane="${i}" aria-label="Select ${role}" aria-describedby="drag-hint" aria-pressed="false" disabled><svg viewBox="0 0 25 25" aria-hidden="true"><path d="${glyphs[i]}"/></svg><span class="voice-label"><span class="voice-name">${role}</span><span class="engine-name">—</span></span></button><button class="voice-toggle mute" aria-label="Mute ${role}" aria-pressed="false" disabled>M</button><button class="voice-toggle solo" aria-label="Solo ${role}" aria-pressed="false" disabled>S</button>`;
 $('voices').append(row);
 row.querySelector('.mute').onclick=()=>{desired.mute^=1<<i;queueControls();};
 row.querySelector('.solo').onclick=()=>{desired.solo^=1<<i;queueControls();};
 const identity=row.querySelector('.voice-identity');
 let gesture=null,suppressClick=false;
 identity.addEventListener('pointerdown',e=>{if(e.button!==0||!canEdit()||state.busy)return;gesture={id:e.pointerId,x:e.clientX,y:e.clientY,active:false};suppressClick=false;identity.setPointerCapture(e.pointerId);});
 identity.addEventListener('pointermove',e=>{if(!gesture||e.pointerId!==gesture.id)return;
  if(!gesture.active&&Math.hypot(e.clientX-gesture.x,e.clientY-gesture.y)>7){gesture.active=true;dragSource=i;selectVoice(i);row.classList.add('drag-source');}
  if(!gesture.active)return;
  const target=document.elementFromPoint(e.clientX,e.clientY)?.closest('.voice');
  rows.forEach(r=>r.classList.toggle('drag-over',r===target&&r!==row));
 });
 identity.addEventListener('pointerup',e=>{if(!gesture||e.pointerId!==gesture.id)return;
  const moved=gesture.active,target=document.elementFromPoint(e.clientX,e.clientY)?.closest('.voice');
  gesture=null;identity.releasePointerCapture(e.pointerId);
  if(moved){suppressClick=true;if(target&&target!==row)swap(i,Number(target.dataset.lane));}clearDrag();
 });
 identity.addEventListener('pointercancel',()=>{gesture=null;clearDrag();});
 identity.addEventListener('lostpointercapture',()=>{gesture=null;clearDrag();});
 identity.addEventListener('click',()=>{if(suppressClick){suppressClick=false;return;}selectVoice(i);});
 return row;
});
function selectVoice(lane){selectedLane=lane;rows.forEach((r,i)=>{r.classList.toggle('selected',i===lane);r.querySelector('.voice-identity').setAttribute('aria-pressed',i===lane);});}
function clearDrag(){dragSource=-1;rows.forEach(r=>r.classList.remove('drag-over','drag-source'));}
function notice(text){$('notice').textContent=text;}
function canEdit(){return !!state&&(mode==='preview'||mode==='connected');}
function nextSeq(){seq=(seq+1)&127;return seq;}
function send(type,payload=[]){if(!output||output.state==='disconnected')throw Error('MIDI device disconnected.');const token=nextSeq();output.send(message(type,token,payload));return token;}
function requestStatus(){if(mode==='connecting'||mode==='connected'){try{send(1);}catch(e){disconnect(e.message);}}}
function queueControls(){if(mode==='preview'){state={...state,...desired,audible:(desired.solo||63)&~desired.mute&63};state.actualBpm10=desired.bpm10||state.generatedBpm10;state.period=7200000/state.actualBpm10;update();return;}
 update();clearTimeout(sendTimer);sendTimer=setTimeout(flushControls,40);
}
function flushControls(){clearTimeout(sendTimer);sendTimer=0;if(mode!=='connected')return;try{pendingSeq=send(2,controlsPayload(desired));pendingSince=performance.now();}catch(e){disconnect(e.message);}}
function setTempo(value){if(!canEdit()||state.source!==0)return;const n=Number(value);if(!Number.isFinite(n))return;desired.bpm10=Math.round(Math.min(240,Math.max(40,n))*10);queueControls();}
$('bpm').addEventListener('input',()=>{const value=Number($('bpm').value);if(value>=40&&value<=240)setTempo(value);});
$('bpm').addEventListener('change',()=>setTempo($('bpm').value));
$('bpm').addEventListener('blur',()=>{if($('bpm').value)setTempo($('bpm').value);});
$('bpm').addEventListener('keydown',e=>{if(e.key==='Enter'){setTempo($('bpm').value);$('bpm').blur();}});
$('tempo-down').onclick=()=>setTempo((desired.bpm10||state.actualBpm10)/10-1);
$('tempo-up').onclick=()=>setTempo((desired.bpm10||state.actualBpm10)/10+1);
$('auto-tempo').onclick=()=>{desired.bpm10=0;queueControls();};
$('all-on').onclick=()=>{desired.mute=desired.solo=0;queueControls();};
$('delay').addEventListener('input',()=>{if(!canEdit())return;desired.delay=Number($('delay').value);queueControls();});
$('clear-solo').onclick=()=>{desired.solo=0;queueControls();};
function operation(type,payload){if(!canEdit()||state.busy)return;
 if(mode==='preview'){
  if(type===3){const manual=desired.bpm10;state=makePreview();state.bpm10=manual;state.mute=desired.mute;state.solo=desired.solo;state.delay=desired.delay;state.audible=(desired.solo||63)&~desired.mute&63;state.actualBpm10=manual||state.generatedBpm10;state.period=7200000/state.actualBpm10;previewClock=performance.now();}
  else{const [a,b]=payload;[state.tracks[a].kind,state.tracks[b].kind]=[state.tracks[b].kind,state.tracks[a].kind];state.generation++;previewGeneration=Math.max(previewGeneration,state.generation+1);}
  flashUntil=performance.now()+140;notice('Preview · no audio.');update();return;
 }
 flushControls();try{pendingSeq=send(type,payload);pendingSince=performance.now();waitingOp={seq:pendingSeq,generation:state.generation,since:pendingSince};state.busy=true;update();notice(type===3?'Preparing a new scene…':'Preparing the sound swap for the next clock step…');}catch(e){disconnect(e.message);}
}
function swap(a,b){operation(4,[a,b]);}
$('randomize').onclick=()=>operation(3,[]);
function onMidi(e){const incoming=parseStatus(e.data);if(!incoming)return;
 if(mode!=='connecting'&&mode!=='connected')return;
 clearTimeout(connectTimeout);lastReply=receivedAt=performance.now();
 if(mode==='connecting'){mode='connected';diagnosticBase=incoming.underruns;notice('Connected to Asterisk Workshop.');}
 if((pendingSeq===null&&!sendTimer)||incoming.ack===pendingSeq){desired={bpm10:incoming.bpm10,mute:incoming.mute,solo:incoming.solo,delay:incoming.delay};if(incoming.ack===pendingSeq){pendingSeq=null;if(incoming.result===1)notice('The card is preparing another change. Try the swap again when it is ready.');}}
 if(waitingOp){if(incoming.generation!==waitingOp.generation||(incoming.ack===waitingOp.seq&&incoming.result===1))waitingOp=null;else if(incoming.busy||performance.now()-waitingOp.since<1600)incoming.busy=true;else{waitingOp=null;notice('The last sound change was not confirmed. Try again.');}}
 if(lastScene&&incoming.generation!==lastScene){flashUntil=performance.now()+140;notice('Scene updated.');}lastScene=incoming.generation;
 state=incoming;baseTime=incoming.elapsed/48;update();
}
async function bindPorts(inPort,outPort){
 if(input)input.onmidimessage=null;input=inPort;output=outPort;await Promise.all([input.open(),output.open()]);
 input.onmidimessage=onMidi;mode='connecting';state=null;desired={bpm10:0,mute:0,solo:0,delay:100};pendingSeq=null;lastScene=0;
 notice('Looking for Asterisk Workshop…');update();requestStatus();
 clearInterval(pollTimer);pollTimer=setInterval(()=>{if(document.hidden)return;requestStatus();
  const now=performance.now();if(mode==='connected'&&now-lastReply>3000)disconnect('Connection lost. Check the USB cable, then reconnect.');
  if(pendingSeq!==null&&now-pendingSince>1600){pendingSeq=null;notice('The last change was not confirmed. The next update will restore the device values.');}
 },200);
 connectTimeout=setTimeout(()=>{if(mode==='connecting')disconnect('No editor response. Install Asterisk 1.0 firmware, then connect again.');},5000);
}
function disconnect(text='Disconnected. Hardware keeps its current settings.'){
 clearTimeout(connectTimeout);clearTimeout(sendTimer);clearInterval(pollTimer);sendTimer=0;
 if(input){input.onmidimessage=null;input.close().catch(()=>{});}if(output)output.close().catch(()=>{});
 input=output=null;mode='offline';state=null;pendingSeq=null;waitingOp=null;diagnosticBase=null;clearDrag();selectVoice(-1);notice(text);update();
}
$('connect').onclick=async()=>{
 if(mode==='connected'||mode==='connecting'){disconnect();return;}
 if(!navigator.requestMIDIAccess){notice('Web MIDI is unavailable. Open the online editor in Chrome or Edge.');return;}
 if(mode==='preview'){mode='offline';state=null;update();}
 try{
  midi=await navigator.requestMIDIAccess({sysex:true});
  const ins=[...midi.inputs.values()].filter(p=>p.state==='connected');const outs=[...midi.outputs.values()].filter(p=>p.state==='connected');
  midi.onstatechange=()=>{if(input?.state==='disconnected'||output?.state==='disconnected')disconnect('Asterisk was unplugged. Reconnect when ready.');};
  const goodIn=ins.filter(p=>/asterisk/i.test(p.name));const goodOut=outs.filter(p=>/asterisk/i.test(p.name));
  if(goodIn.length===1&&goodOut.length===1){await bindPorts(goodIn[0],goodOut[0]);return;}
  if(!ins.length||!outs.length){notice('No MIDI device found. Check the data cable and power-cycle the Workshop Computer.');return;}
  for(const [id,ports] of [['input-port',ins],['output-port',outs]]){$(id).replaceChildren(...ports.map(p=>new Option(p.name,p.id)));}
  $('ports-dialog').showModal();
 }catch(e){disconnect(e.name==='NotAllowedError'?'MIDI permission was not granted. Allow MIDI and SysEx access to connect.':`Could not connect: ${e.message}`);}
};
$('use-ports').onclick=async()=>{$('ports-dialog').close();try{await bindPorts(midi.inputs.get($('input-port').value),midi.outputs.get($('output-port').value));}catch(e){disconnect(e.message);}};
function rand(){previewSeed^=previewSeed<<13;previewSeed^=previewSeed>>>17;previewSeed^=previewSeed<<5;return (previewSeed>>>0)/4294967296;}
function makePreview(){const lengths=[16,12,16,16,11,15],counts=[4,3,9,5,4,6];return {bpm10:0,generatedBpm10:Math.round(95+rand()*55)*10,actualBpm10:1200,source:0,step:1,elapsed:0,period:6000,swing:2300,generation:previewGeneration++,running:true,busy:false,mute:0,solo:0,delay:100,audible:63,filter:2048,decay:2048,heat:1100,underruns:0,midiOverflows:0,tracks:lengths.map((n,i)=>{let pattern=0;const rotation=Math.floor(rand()*n);for(let j=0;j<n;j++)if((j*counts[i])%n<counts[i])pattern+=(2**((j+rotation)%n));return {length:n,kind:([8,10,12,7,4,3][i]+(rand()>.8?1:0))%14,pattern};})};}
$('preview').onclick=()=>{if(mode==='connected'||mode==='connecting')disconnect();if(mode==='preview'){mode='offline';state=null;notice('Disconnected.');}else{mode='preview';state=makePreview();state.actualBpm10=state.generatedBpm10;state.period=7200000/state.actualBpm10;desired={bpm10:0,mute:0,solo:0,delay:100};previewClock=performance.now();notice('Preview · no audio.');}update();};
$('help').onclick=()=>$('help-dialog').showModal();
for(const b of document.querySelectorAll('[data-close]'))b.onclick=()=>$(b.dataset.close).close();
document.addEventListener('keydown',e=>{if(e.key==='Escape'){clearDrag();selectVoice(-1);}});
document.addEventListener('visibilitychange',()=>{if(!document.hidden){lastReply=performance.now();requestStatus();}});
window.addEventListener('pagehide',()=>{clearInterval(pollTimer);if(input)input.onmidimessage=null;});
function update(){
 const on=canEdit(),external=on&&state.source!==0;
 $('connect').innerHTML=(mode==='connected'?'DISCONNECT':mode==='connecting'?'CONNECTING…':'CONNECT')+' <span>↗</span>';
 $('preview').classList.toggle('selected',mode==='preview');$('preview').textContent=mode==='preview'?'EXIT PREVIEW':'PREVIEW';
 $('clock-source').textContent=on?['INTERNAL CLOCK','CV CLOCK','USB MIDI CLOCK'][state.source]:'NO CLOCK';
 if(document.activeElement!==$('bpm'))$('bpm').value=on?((external?state.actualBpm10:desired.bpm10||state.actualBpm10)/10).toFixed(1).replace(/\.0$/,''):'120';
 for(const id of ['bpm','tempo-down','tempo-up','auto-tempo'])$(id).disabled=!on||external;
 $('auto-tempo').classList.toggle('active',on&&!desired.bpm10);
 $('tempo-note').textContent=external?'EXTERNAL':on&&desired.bpm10?'MANUAL':'AUTO';
 $('delay').disabled=!on;$('delay').value=desired.delay;$('delay-value').textContent=on?desired.delay+'%':'—';$('delay').style.setProperty('--amount',desired.delay+'%');
 for(const id of ['all-on','clear-solo'])$(id).disabled=!on;
 $('randomize').disabled=!on||state.busy;$('randomize').classList.toggle('busy',on&&state.busy);
 rows.forEach((r,i)=>{const silent=on&&!(((desired.solo||63)&~desired.mute)&(1<<i));r.classList.toggle('silent',silent);r.classList.toggle('soloed',!!(desired.solo&(1<<i)));
  const m=r.querySelector('.mute'),s=r.querySelector('.solo'),v=r.querySelector('.voice-identity');m.disabled=s.disabled=!on;v.disabled=!on||state.busy;
  m.setAttribute('aria-pressed',!!(desired.mute&(1<<i)));s.setAttribute('aria-pressed',!!(desired.solo&(1<<i)));
  r.querySelector('.engine-name').textContent=on?engines[state.tracks[i].kind]:'AWAITING SIGNAL';r.querySelector('path').setAttribute('d',glyphs[on?state.tracks[i].kind:i]);
 });
 $('diagnostics').textContent=mode==='connected'?`Audio underruns since power-on: ${state.underruns}. Since connection: ${Math.max(0,state.underruns-diagnosticBase)}. MIDI input overflows: ${state.midiOverflows}.`:'Connect hardware to read its counters.';
}
const canvas=$('radar'),ctx=canvas.getContext('2d');let width=0,height=0;
new ResizeObserver(entries=>{const r=entries[0].contentRect;width=r.width;height=r.height;const dpr=Math.min(devicePixelRatio||1,2);canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);}).observe(canvas);
const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
function playPosition(now){if(!state)return 0;if(mode==='preview'){return (now-previewClock)/(state.period/48);}
 let position=Math.max(0,state.step-1),elapsed=baseTime+(state.running?Math.min(now-receivedAt,600):0);const period=state.period/48,sw=state.source===0?state.swing/32768:0;
 if(state.running){for(let n=0;n<12;n++){const duration=period*(position%2?-sw+1:sw+1);if(elapsed<duration)return position+elapsed/duration;elapsed-=duration;position++;}}return position;
}
function draw(now){requestAnimationFrame(draw);if(document.hidden||now-lastDraw<(reduced?100:32))return;lastDraw=now;
 ctx.clearRect(0,0,width,height);if(!width)return;
 const cx=width/2,cy=height/2,rMax=Math.min(width*.43,height*.44),rMin=Math.min(86,rMax*.42),pos=playPosition(now);
 for(let i=0;i<6;i++){
  const r=rMin+(rMax-rMin)*i/5,t=state?.tracks[i]??{length:[16,12,16,16,11,15][i],pattern:0},n=t.length;
  const audible=state&&!!(((desired.solo||63)&~desired.mute)&(1<<i)),at=Math.floor(pos)%n;
  ctx.strokeStyle=i===selectedLane?'#697554':'#252c1e';ctx.lineWidth=1;ctx.beginPath();ctx.arc(cx,cy,r,0,Math.PI*2);ctx.stroke();
  for(let j=0;j<n;j++){
   const a=-Math.PI/2+j/n*Math.PI*2,hit=(t.pattern>>>j)&1;
   if(hit){const half=Math.PI/n*.47;ctx.beginPath();ctx.arc(cx,cy,r,a-half,a+half);ctx.strokeStyle=!audible?'#48523b':now<flashUntil&&!reduced?'#7f5dff':j===at&&state.running?'#efffca':'#c2ff3d';ctx.lineWidth=audible?4:2;ctx.stroke();}
   else{ctx.beginPath();ctx.moveTo(cx+Math.cos(a)*(r-2),cy+Math.sin(a)*(r-2));ctx.lineTo(cx+Math.cos(a)*(r+2),cy+Math.sin(a)*(r+2));ctx.strokeStyle='#3c472f';ctx.lineWidth=1;ctx.stroke();}
  }
  if(state){const a=-Math.PI/2+(pos%n)/n*Math.PI*2;ctx.beginPath();ctx.arc(cx+Math.cos(a)*(r+6),cy+Math.sin(a)*(r+6),2,0,Math.PI*2);ctx.fillStyle=audible?'#ff4d2e':'#624732';ctx.fill();}
  rows[i].classList.toggle('hit',!!state&&audible&&!!((t.pattern>>>at)&1)&&(pos%1)<.35&&state.running);
 }
}
update();requestAnimationFrame(draw);
