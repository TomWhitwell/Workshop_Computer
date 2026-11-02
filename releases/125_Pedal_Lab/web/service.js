// SPDX-License-Identifier: MPL-2.0
let catalogue;const sources=new Map(),pending=new Map();let worker,sequence=0;
function run(request){worker??=new Worker(new URL('./compiler/compiler-worker.js',import.meta.url),{type:'module'});worker.onmessage=({data})=>{if(data.progress){document.querySelector('#compiler-status').textContent=data.progress;return}const job=pending.get(data.id);if(!job)return;pending.delete(data.id);data.error?job.reject(Error(data.error)):job.resolve(data.result);if(!pending.size)document.querySelector('#compiler-status').textContent='Runs in your browser · no compiler server needed'};worker.onerror=e=>{for(const job of pending.values())job.reject(Error(e.message||'Browser compiler stopped'));pending.clear();worker.terminate();worker=null};const id=++sequence;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});worker.postMessage({id,request})})}
export async function initService(){return true}
export async function api(path,value){
 if(path==='/api/catalogue'){catalogue=await(await fetch('./catalogue.json')).json();return catalogue}
 if(path.startsWith('/api/source?')){const id=new URLSearchParams(path.split('?')[1]).get('id'),metadata=catalogue.programs.find(e=>e.id===id);if(!metadata)throw Error('Unknown algorithm');let source=sources.get(id);if(source===undefined){const r=await fetch('./sources/'+encodeURIComponent(id)+'.spn');if(!r.ok)throw Error('Source is unavailable');source=await r.text();sources.set(id,source)}return {source,metadata}}
 if(path==='/api/check')return run({type:'check',programs:[{source:value.source}]});
 if(path==='/api/preview')return run({type:'preview',...value});
 if(path==='/api/build'){
  const entry=Object.entries(catalogue.banks).find(([,b])=>b.programs.length===value.programs.length&&b.programs.every((id,i)=>id===value.programs[i].id&&sources.get(id)===value.programs[i].source));
  let bytes,download,guide_url;
  if(entry){const name=entry[0];download=new URL('./banks/'+name+'.uf2',location.href).href;const r=await fetch(download);if(!r.ok)throw Error('Bank is unavailable');bytes=new Uint8Array(await r.arrayBuffer());guide_url=new URL('./banks/'+name+'.json',location.href).href}
  else{const result=await run({type:'build',programs:value.programs});bytes=new Uint8Array(result.uf2);download=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'}));const slots=value.programs.map((p,i)=>{const original=catalogue.programs.find(e=>e.id===p.id);return {...original,name:p.name,slot:i+1,modified:!original||sources.get(p.id)!==p.source,assembly:result.stats[i]}});guide_url=URL.createObjectURL(new Blob([JSON.stringify({slots},null,2)],{type:'application/json'}))}
  const sha256=[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(v=>v.toString(16).padStart(2,'0')).join('');return {status:'ready',download,guide_url,bytes:bytes.length,sha256,message:'Bank ready'};
 }
 throw Error('Unknown operation');
}
