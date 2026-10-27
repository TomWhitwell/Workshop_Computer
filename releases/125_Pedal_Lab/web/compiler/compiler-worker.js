// SPDX-License-Identifier: MPL-2.0
import {elfToUF2} from './uf2.js';
const moduleURL=import.meta.url;
const decoder=new TextDecoder();let python,clang,sources,firmware;let serial=Promise.resolve();
const progress=text=>postMessage({progress:text});
async function json(url){const r=await fetch(new URL(url,moduleURL));if(!r.ok)throw Error('Could not load '+url);let bytes=new Uint8Array(await r.arrayBuffer());if(bytes[0]===31&&bytes[1]===139)bytes=new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());return JSON.parse(decoder.decode(bytes))}
// Keep upstream runtimes opaque to development bundlers; they contain Node-only imports.
async function importRuntime(name){
 const response=await fetch(new URL('./python/'+name+'.txt',moduleURL));
 if(!response.ok)throw Error('Could not load browser runtime '+name);
 const url=URL.createObjectURL(new Blob([await response.text()],{type:'text/javascript'}));
 try{return await import(/* @vite-ignore */ url)}finally{URL.revokeObjectURL(url)}
}
async function loadPython(){
 if(python)return python;progress('Loading the code checker…');
 await importRuntime('pyodide.asm.js');
 const {loadPyodide}=await importRuntime('pyodide.mjs');python=await loadPyodide({indexURL:new URL('./python/',moduleURL).href,stdout:()=>{},stderr:()=>{}});
 sources=await json('./sources.json');
 for(const[path,text]of Object.entries(sources.python)){const name='/app/'+path;python.FS.mkdirTree(name.slice(0,name.lastIndexOf('/')));python.FS.writeFile(name,text)}
 python.runPython('exec(open("/app/assembler_api.py").read())');return python;
}
function untar(bytes){const out={};for(let p=0;p+512<=bytes.length;){const name=decoder.decode(bytes.subarray(p,p+100)).split('\0')[0];if(!name)break;const length=parseInt(decoder.decode(bytes.subarray(p+124,p+136)).replace(/\0/g,'').trim(),8)||0;if(bytes[p+156]===48||bytes[p+156]===0)out[name]=bytes.slice(p+512,p+512+length);p+=512+Math.ceil(length/512)*512}return out}
async function loadCompiler(){
 if(clang)return clang;progress('Downloading the browser compiler (24 MB, saved for next time)…');
 const url=new URL('./toolchain.tgz',moduleURL);let cache;try{cache=await caches.open('pedal-lab-toolchain-v1')}catch{}
 let response=await cache?.match(url);if(!response){response=await fetch(url);if(!response.ok)throw Error('Compiler download failed');if(cache)try{await cache.put(url,response.clone())}catch{}}
 const unpacked=new Uint8Array(await new Response(response.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer()),files=untar(unpacked);
 const urls={};for(const[name,bytes]of Object.entries(files)){const key=name.split('/').at(-1);urls[key]=URL.createObjectURL(new Blob([bytes],{type:key.endsWith('.wasm')?'application/wasm':'application/octet-stream'}))}
 let bundle=decoder.decode(files['package/gen/bundle.js']);
 bundle=bundle.replace(/new URL\("\.\/(llvm[^\"]+)", import\.meta\.url\)/g,(_,name)=>`new URL(${JSON.stringify(urls[name])})`);
 // The package's explicit module loader handles all four Wasm modules.
 progress('Starting the browser compiler…');clang=await import(/* @vite-ignore */ URL.createObjectURL(new Blob([bundle],{type:'text/javascript'})));return clang;
}
function tree(files){const root={};for(const[path,value]of Object.entries(files)){const parts=path.split('/'),name=parts.pop();let node=root;for(const p of parts)node=node[p]??={};node[name]=value}return root}
async function generate(request){await loadPython();python.globals.set('request_json',JSON.stringify(request));return JSON.parse(python.runPython('assemble_request(json.loads(request_json))'))}
async function compile(header){
 const {commands}=await loadCompiler();let errors='';const options={stderr:b=>{if(b)errors+=decoder.decode(b)},stdout:()=>{}};
 const files=tree(sources.cpp);files['bank.hpp']=header;files['main.cpp']='#include "bank.hpp"';
 const flags=['-std=c++17','--sysroot=/usr','-Iinclude','-I.','-O3','-fno-exceptions','-fno-rtti','-ffunction-sections','-fdata-sections','--target=arm-none-eabi','-mcpu=cortex-m0plus','-mthumb','-mfloat-abi=soft','-c','main.cpp','-o','bank.o'];
 progress('Compiling your effects…');
 try{
  const output=await commands['clang++'](flags,files,options);
  firmware??=await json('./firmware.json.gz');const inputs={...firmware.files};for(const[name,value]of Object.entries(inputs))inputs[name]=Uint8Array.from(atob(value),c=>c.charCodeAt(0));inputs['bank.o']=output['bank.o'];
  progress('Making your UF2…');const linked=await commands['ld.lld'](firmware.ld,tree(inputs),options);return linked['pedal-lab.elf'];
 }catch(e){throw Error(errors.trim()||e.message)}
}
function wav(pcm){const b=new Uint8Array(44+pcm.length*2),v=new DataView(b.buffer),text=(p,s)=>[...s].forEach((c,i)=>v.setUint8(p+i,c.charCodeAt(0)));text(0,'RIFF');v.setUint32(4,b.length-8,true);text(8,'WAVEfmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,2,true);v.setUint32(24,48000,true);v.setUint32(28,192000,true);v.setUint16(32,4,true);v.setUint16(34,16,true);text(36,'data');v.setUint32(40,pcm.length*2,true);for(let i=0;i<pcm.length;i++)v.setInt16(44+i*2,pcm[i],true);let binary='';for(let i=0;i<b.length;i+=32768)binary+=String.fromCharCode(...b.subarray(i,i+32768));return btoa(binary)}
let previewModule;
async function preview(request,generated){
 previewModule??=await WebAssembly.compile(await(await fetch(new URL('./preview.wasm',moduleURL))).arrayBuffer());
 const instance=await WebAssembly.instantiate(previewModule,{}),e=instance.exports;e.__wasm_call_ctors();
 new Uint32Array(e.memory.buffer,e.program_buffer(),128).set(generated.words[0]);e.program_length(generated.stats[0].instructions);
 let pcm,n;if(request.wav){const b=Uint8Array.from(atob(request.wav),c=>c.charCodeAt(0)),v=new DataView(b.buffer);let p=12;while(p+8<=b.length&&decoder.decode(b.subarray(p,p+4))!=='data')p+=8+v.getUint32(p+4,true)+(v.getUint32(p+4,true)&1);if(p+8>b.length)throw Error('Audio data missing');n=Math.min(48000*12,v.getUint32(p+4,true)/4);pcm=new Int16Array(n*2);for(let i=0;i<pcm.length;i++)pcm[i]=v.getInt16(p+8+i*2,true)}else{
  n=Math.round(48000*request.duration);pcm=new Int16Array(n*2);
  for(let i=0;i<n;i++){let t=i/48000,v=0;if(request.stimulus==='impulse')v=i===0?.12:0;else if(request.stimulus==='sine')v=t<request.duration/2?.12*Math.sin(2*Math.PI*220*t):0;else for(const[start,freq]of [[0,110],[.75,164.8138],[1.5,220],[2.25,146.8324]]){const age=t-start;if(age>=0&&age<1.5)for(let h=1;h<=6;h++)v+=.08*Math.exp(-age*6)*Math.sin(2*Math.PI*freq*h*age)/h**1.5}pcm[i*2]=pcm[i*2+1]=Math.max(-32768,Math.min(32767,Math.round(v*32767)))}
 }
 const input=new Int32Array(e.memory.buffer,e.input_buffer(),n*2);for(let i=0;i<input.length;i++)input[i]=pcm[i]*128;
 progress('Rendering audio in your browser…');e.render(n,request.rate,...request.pots.map(p=>Math.round(p*511)));
 const values=new Int32Array(e.memory.buffer,e.output_buffer(),n*2),wet=new Int16Array(n*2);let peak=0,clipped=0;
 for(let i=0;i<values.length;i++){const v=values[i];peak=Math.max(peak,Math.abs(v));if(v>4194176||v<-4194304)clipped++;wet[i]=Math.max(-32768,Math.min(32767,Math.round(v/128)))}
 return {wav:wav(wet),dry:wav(pcm),duration:n/48000,clipped_samples:clipped,peak_dbfs:peak?20*Math.log10(peak/4194304):-120};
}
async function handle(request){
 const {type}=request;const generated=await generate(type==='preview'?{type:'compile',programs:[{source:request.source}]}:request);
 if(type==='check')return generated;
 if(type==='preview')return preview(request,generated);
 const elf=await compile(generated.header),uf2=elfToUF2(elf);return {uf2:uf2.buffer,stats:generated.stats};
}
onmessage=({data})=>{serial=serial.then(async()=>{try{const result=await handle(data.request);postMessage({id:data.id,result},result.uf2?[result.uf2]:[])}catch(e){postMessage({id:data.id,error:e.message})}})};
