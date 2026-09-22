export const HEADER = [0x7d,0x41,0x53,0x54,2];
export function message(type,seq,payload=[]){return new Uint8Array([0xf0,...HEADER,type,seq&127,...payload,0xf7]);}
export function controlsPayload({bpm10,mute,solo,delay}){if(!Number.isInteger(bpm10)||(bpm10!==0&&(bpm10<400||bpm10>2400))||mute<0||mute>63||solo<0||solo>63||!Number.isInteger(delay)||delay<0||delay>100)throw Error('Invalid controls');return [bpm10&127,bpm10>>7,mute,solo,delay];}
export function parseStatus(bytes){
 const b=Array.from(bytes);if(b.length!==91||b[0]!==240||b.at(-1)!==247||HEADER.some((v,i)=>v!==b[i+1])||b[6]!==65||b.slice(1,-1).some(v=>v>127))return null;
 let p=7;const read=n=>{let v=0,m=1;while(n--){v+=b[p++]*m;m*=128;}return v;};
 const s={seq:read(1),ack:read(1),flags:read(1),source:read(1),mute:read(1),solo:read(1),audible:read(1),bpm10:read(2),actualBpm10:read(2),step:read(4),elapsed:read(3),period:read(3),swing:read(3),generation:read(3),filter:read(2),decay:read(2),heat:read(2),delay:read(1),underruns:read(3),midiOverflows:read(3),result:read(1),tracks:[]};
 for(let i=0;i<6;i++)s.tracks.push({length:read(1),kind:read(1),pattern:read(5)});
 if(p!==b.length-1||s.source>2||s.mute>63||s.solo>63||s.audible>63||s.delay>100||s.period<1||s.tracks.some(t=>t.length<1||t.length>32||t.kind>13||t.pattern>0xffffffff))return null;
 s.running=!!(s.flags&1);s.busy=!!(s.flags&2);return s;
}
