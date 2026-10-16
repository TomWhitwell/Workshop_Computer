// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Actual editor script with simulated MIDI, including serialized Blob downloads.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('web/index.html','utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const defaults=[2048,1200,2048,2048,4095,4],placement=[2048,0,4095,0,0,4095];
const firmwareDefaults=fs.readFileSync('src/config.h','utf8').match(/value\[kFields\]\s*=\s*\{([^}]+)\}/)[1].split(',').map(Number);assert.deepEqual(defaults,firmwareDefaults);
const elements=new Map(),downloads=[],urls=new Map();let urlId=0;
function element(){return {disabled:false,value:'',textContent:'',hidden:false,options:[],files:[],click(){if(this.download)downloads.push({name:this.download,blob:urls.get(this.href)});},setAttribute(){},append(o){this.options.push(o);if(!this.value)this.value=o.value;},replaceChildren(){this.options=[];this.value='';}};}
const fields=[...defaults,...placement,0].map((v,i)=>({...element(),value:String(v),dataset:{id:String(i+1)},parentElement:{hidden:false,querySelector:()=>element()}}));
const document={getElementById:id=>{if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelectorAll:()=>fields,createElement:()=>element()};
let activeMode=0,banks=[[...defaults],[...defaults,...placement],[...defaults,0]],saved=null,sent=[],oldFirmware=false;
const input={id:'in',name:'Spatial Disorientation',state:'connected',open:async()=>{},close:async()=>{}};
const output={id:'out',name:'Spatial Disorientation',state:'connected',open:async()=>{},close:async()=>{},send(msg){
 sent.push(msg);assert.equal(msg[4],3);const cmd=msg[5],seq=msg[6];let body,reply;
 if(oldFirmware){queueMicrotask(()=>input.onmidimessage?.({data:[240,125,83,68,1,66,seq,cmd,2,247]}));return;}
 if(cmd===1){reply=65;body=[activeMode,...banks[activeMode].flatMap((v,i)=>[activeMode===2&&i===6?13:i+1,v&127,v>>7])];}
 else{reply=66;body=[cmd,0];if(cmd===2){assert.equal(msg[7],activeMode);banks[activeMode]=Array.from({length:activeMode===1?12:activeMode===2?7:6},(_,i)=>msg[9+i*3]|msg[10+i*3]<<7);}if(cmd===3)saved=banks.map(a=>[...a]);}
 queueMicrotask(()=>input.onmidimessage?.({data:[240,125,83,68,3,reply,seq,...body,247]}));
}};
const midi={inputs:new Map([['in',input]]),outputs:new Map([['out',output]])};
const ctx=vm.createContext({document,navigator:{requestMIDIAccess:async()=>midi},Blob,URL:{createObjectURL(blob){const u='blob:'+ ++urlId;urls.set(u,blob);return u;},revokeObjectURL(u){urls.delete(u);}},setTimeout,clearTimeout,console});vm.runInContext(source,ctx);
(async()=>{
 const get=id=>document.getElementById(id),values=n=>fields.slice(0,n).map(f=>Number(f.value));
 const load=async(p,size)=>{const text=typeof p==='string'?p:JSON.stringify(p);get('preset-file').files=[{size:size??Buffer.byteLength(text),text:async()=>text}];await get('preset-file').onchange();};
 get('preset-name').value='Near room';await get('export').onclick();const initial=JSON.parse(await downloads[0].blob.text());
 assert.equal(initial.version,3);assert.equal(initial.function,'twin-orbits');assert.deepEqual(Object.values(initial.parameters),defaults);assert.equal(sent.length,0);assert(get('apply').disabled);
 await get('access').onclick();await get('connect').onclick();assert(!get('apply').disabled);assert(fields[6].parentElement.hidden);assert(fields[12].parentElement.hidden);
 fields[1].value=2000;fields[1].oninput();assert(get('save').disabled);await get('apply').onclick();assert.equal(banks[0][1],2000);await get('save').onclick();assert.equal(saved[0][1],2000);
 // Reset only this mode, without an automatic flash write.
 const saveCount=sent.filter(m=>m[5]===3).length;await get('init').onclick();assert.deepEqual(banks[0],defaults);assert.equal(saved[0][1],2000);assert.equal(sent.filter(m=>m[5]===3).length,saveCount);
 // Stage offline v1 Twin Orbits; reconnect retains edits and requires Apply/Save.
 await get('disconnect').onclick();const legacy={...initial,version:1,name:'Old orbit',parameters:{...initial.parameters,'2':2300}};await load(legacy);assert.equal(fields[1].value,2300);assert(get('save').disabled);
 await get('connect').onclick();assert.equal(fields[1].value,2300);assert.deepEqual(banks[0],defaults);await get('apply').onclick();assert.equal(banks[0][1],2300);
 // Malformed files fail atomically and send no commands.
 const stable=values(12),name=get('preset-name').value;
 for(const p of ['{',null,{...initial,version:4},{...initial,version:1,function:'disorientation'},{...initial,name:'bad\nname'},{...initial,parameters:{...initial.parameters,'7':1}},{...initial,parameters:{...initial.parameters,'6':3}},{...initial,parameters:{...initial.parameters,'2':4096}},{...initial,parameters:{...initial.parameters,'2':'1'}}]){
  const n=sent.length;await load(typeof p==='string'?p:JSON.stringify(p));assert.deepEqual(values(12),stable);assert.equal(get('preset-name').value,name);assert.equal(sent.length,n);
 }
 await load(initial,16385);assert(get('status').textContent.includes('too large'));
 // Simulate reset into Mixer, then Read includes BOTH live placements.
 activeMode=1;banks[1]=[...defaults,3072,1000,2200,1024,2800,1800];await get('read').onclick();assert(!fields[6].parentElement.hidden);assert.deepEqual(values(12),banks[1]);assert(fields[0].disabled&&fields[5].disabled);
 await get('export').onclick();const mix=JSON.parse(await downloads.at(-1).blob.text());assert.equal(mix.version,3);assert.equal(mix.function,'spatial-mixer');assert.deepEqual(Object.values(mix.parameters),banks[1]);
 const orbitBefore=[...banks[0]];fields[1].value=3100;fields[6].value=3500;fields[6].oninput();await get('apply').onclick();assert.equal(banks[1][1],3100);assert.equal(banks[1][6],3500);assert.deepEqual(banks[0],orbitBefore);
 // Save captures live panel placements even when the displayed values are older.
 banks[1][7]=1700;await get('save').onclick();assert.equal(saved[1][7],1700);assert.equal(fields[7].value,1700);assert.deepEqual(saved[0],orbitBefore);
 await load(initial);assert(get('apply').disabled);const mismatch=sent.length;await get('apply').onclick();assert.equal(sent.length,mismatch);await get('read').onclick();assert(!get('apply').disabled);
 // v1 Mixer has no placements: explicitly migrate to defined defaults.
 const oldMix={...legacy,function:'spatial-mixer'};await load(oldMix);assert.deepEqual(values(12).slice(6),placement);assert(get('status').textContent.includes('default placements'));await get('apply').onclick();assert.deepEqual(banks[1].slice(6),placement);
 // Invalid v2 Mixer placement and missing IDs are rejected without changes.
 const keep=values(12);await load({...mix,parameters:{...mix.parameters,'9':5000}});assert.deepEqual(values(12),keep);const missing={...mix.parameters};delete missing['12'];await load({...mix,parameters:missing});assert.deepEqual(values(12),keep);
 await load(mix);await get('export').onclick();assert.deepEqual(JSON.parse(await downloads.at(-1).blob.text()),mix);
 // Timing/source telemetry remains compatible inside version-3 transport.
 input.onmidimessage({data:[240,125,83,68,3,68,0,8,0,125,7,32,247]});assert(get('timing').textContent.includes('1021 µs'));
 input.onmidimessage({data:[240,125,83,68,3,67,0,0,0,0,16,0,0,0,1,100,10,15,247]});assert(get('live').textContent.includes('Editing B'));
 input.onmidimessage({data:[240,125,83,68,3,67,0,0,0,0,16,0,0,0,2,0,0,0,247]});activeMode=2;await get('read').onclick();assert(!get('apply').disabled);assert(fields[6].parentElement.hidden);assert(!fields[12].parentElement.hidden);
 input.onmidimessage({data:[240,125,83,68,3,67,0,0,0,0,16,0,0,3,2,0,0,0,1,247]});assert(get('live').textContent.includes('Frozen'));
 input.onmidimessage({data:[240,125,83,68,3,67,0,0,0,0,16,0,0,3,2,0,0,0,0,247]});assert(get('live').textContent.includes('Clock locked'));
 const previousBanks=banks.slice(0,2).map(a=>[...a]);fields[1].value=700;fields[12].value=2;fields[1].oninput();await get('apply').onclick();await get('save').onclick();assert.equal(saved[2][1],700);assert.equal(saved[2][6],2);assert.deepEqual(banks.slice(0,2),previousBanks);
 await get('export').onclick();const fig=JSON.parse(await downloads.at(-1).blob.text());assert.equal(fig.function,'disorientation');assert.equal(Object.keys(fig.parameters).length,7);await load(fig);assert(!get('apply').disabled);
 const oldFig={...fig,version:2,parameters:{...fig.parameters}};delete oldFig.parameters['13'];await load(oldFig);assert.equal(fields[12].value,0);await load(fig);
 const keepFig=fields[12].value;await load({...fig,parameters:{...fig.parameters,'13':3}});assert.equal(fields[12].value,keepFig);
 input.onmidimessage({data:[240,125,83,68,3,67,0,0,0,0,16,0,0,3,2,0,0,0,0,2,247]});assert(get('live').textContent.includes('Wander'));
 input.state='disconnected';await midi.onstatechange();assert(get('save').disabled);
 input.state='connected';oldFirmware=true;await get('connect').onclick();assert(get('status').textContent.includes('Older firmware'));assert(get('apply').disabled);
 assert.throws(()=>vm.runInContext('decode([0,1,0,0])',ctx));assert.throws(()=>vm.runInContext('encode([4096,0,0,0,0,4],0)',ctx));
 for(const msg of sent)assert(msg.slice(1,-1).every(b=>b>=0&&b<=127));
 console.log('PASS: actual editor lifecycle, mode-tagged snapshots, independent banks, live-placement save/read/export, offline staging, v1/v2 migration, v3 roundtrip and atomic malformed rejection');
})().catch(e=>{console.error(e);process.exitCode=1;});
