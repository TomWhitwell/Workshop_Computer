export const HEADER = [0x7d,0x41,0x53,0x54,5];
export function message(type,seq,payload=[]){return new Uint8Array([0xf0,...HEADER,type,seq&127,...payload,0xf7]);}
export function controlsPayload({bpm10,mute,solo,delay,delayMode=0,delayTime=0}){if(!Number.isInteger(bpm10)||(bpm10!==0&&(bpm10<400||bpm10>2400))||mute<0||mute>63||solo<0||solo>63||!Number.isInteger(delay)||delay<0||delay>100||!validDelayTime(delayMode,delayTime))throw Error('Invalid controls');return [bpm10&127,bpm10>>7,mute,solo,delay,delayMode,delayTime&127,delayTime>>7];}
export function parseStatus(bytes){
 const b=Array.from(bytes);if(b.length!==125||b[0]!==240||b.at(-1)!==247||HEADER.some((v,i)=>v!==b[i+1])||b[6]!==65||b.slice(1,-1).some(v=>v>127))return null;
 let p=7;const read=n=>{let v=0,m=1;while(n--){v+=b[p++]*m;m*=128;}return v;};
 const s={seq:read(1),ack:read(1),flags:read(1),source:read(1),mute:read(1),solo:read(1),audible:read(1),bpm10:read(2),actualBpm10:read(2),step:read(4),elapsed:read(3),period:read(3),swing:read(3),generation:read(3),filter:read(2),decay:read(2),heat:read(2),delay:read(1),underruns:read(3),midiOverflows:read(3),result:read(1),delayMode:read(1),delayTime:read(2),delayUnits:read(1),tracks:[]};
 for(let i=0;i<6;i++)s.tracks.push({length:read(1),kind:read(1),pattern:read(5),pan:read(2),send:read(1),mixFlags:read(1),level:read(1)});
 if(p!==b.length-1||s.source>2||s.mute>63||s.solo>63||s.audible>63||s.delay>100||!validDelayTime(s.delayMode,s.delayTime)||![9,12,18,24].includes(s.delayUnits)||s.period<1||s.tracks.some(t=>t.length<1||t.length>32||t.kind>13||t.pan>200||t.send>100||t.mixFlags>7||t.level>100||t.pattern>0xffffffff))return null;
 s.running=!!(s.flags&1);s.busy=!!(s.flags&2);return s;
}
export const PATCH_BYTES=150;
export function checksum(bytes){let h=2166136261;for(const b of bytes)h=Math.imul(h^b,16777619)>>>0;return h;}
export function validatePatchData(data,legacy=false){
 if(!Array.isArray(data)||![117,141,PATCH_BYTES].includes(data.length)||data.some(b=>!Number.isInteger(b)||b<0||b>127))throw Error('Invalid patch data.');
 let at=0;const read=n=>{let v=0;for(let i=0;i<n;i++){const b=data[at++];if(i===4&&b>15)throw Error('Invalid patch number.');v+=b*2**(7*i);}return v;};
 if(!read(5))throw Error('Invalid patch seed.');
 for(let i=0;i<6;i++){if(!read(5)||!read(5)||!read(5))throw Error('Invalid voice seed.');if(read(1)>5||read(1)>5)throw Error('Invalid voice role.');}
 const controls={bpm10:read(2),mute:read(1),solo:read(1),delay:read(1)};controlsPayload(controls);
 const mix=[];if(data.length!==117)for(let i=0;i<6;i++){const pan=read(2),send=read(1),flags=read(1),level=data.length===PATCH_BYTES?read(1):0;if(pan>200||send>100||flags>(data.length===PATCH_BYTES?7:3)||level>100)throw Error('Invalid voice mix.');mix.push({pan,send,flags,level});}
 if(data.length===PATCH_BYTES){controls.delayMode=read(1);controls.delayTime=read(2);controlsPayload(controls);}
 const expected=checksum(data.slice(0,at));if(read(5)!==expected)throw Error('Patch checksum does not match.');
 return {data:[...data],controls,mix};
}
export function parsePatch(bytes){
 const b=Array.from(bytes);if(b.length!==PATCH_BYTES+9||b[0]!==240||b.at(-1)!==247||HEADER.some((v,i)=>v!==b[i+1])||b[6]!==66)return null;
 try{return {seq:b[7],...validatePatchData(b.slice(8,-1))};}catch{return null;}
}
export function patchDocument(data){validatePatchData(data);return {format:'asterisk-workshop-patch',version:3,engine:'asterisk-1.1',data:[...data]};}
export function readPatchDocument(text){
 if(text.length>8192)throw Error('Patch file is too large.');let file;
 try{file=JSON.parse(text);}catch{throw Error('This is not a valid patch file.');}
 if(file?.format!=='asterisk-workshop-patch')throw Error('Use an Asterisk patch file.');
 const size=file.version===1&&file.engine==='internal-03'?117:file.version===2&&file.engine==='internal-04'?141:file.version===3&&['asterisk-1.1','internal-05'].includes(file.engine)?PATCH_BYTES:0;
 if(!size||file.data?.length!==size)throw Error('Use an Asterisk 1.1 patch file.');
 return validatePatchData(file.data);
}

export function mixPayload(lane,field,value,generation){
 if(!Number.isInteger(lane)||lane<0||lane>5||!Number.isInteger(field)||field<0||field>2||!Number.isInteger(value)||value<0||value>(field===0?200:100)||!Number.isInteger(generation)||generation<0||generation>0x1fffff)throw Error('Invalid voice mix.');
 return [lane,field,value&127,value>>7,generation&127,(generation>>7)&127,(generation>>14)&127];
}

export function validDelayTime(mode,time){return Number.isInteger(time)&&(mode===0?time>=0&&time<=8:mode===1&&time>=20&&time<=1000);}
