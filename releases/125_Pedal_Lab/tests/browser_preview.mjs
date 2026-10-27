import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url));
const {instance}=await WebAssembly.instantiate(fs.readFileSync(root+'web/compiler/preview.wasm'),{}),e=instance.exports;e.__wasm_call_ctors();
const words=JSON.parse(fs.readFileSync(process.argv[3],'utf8')),v=fs.readFileSync(process.argv[2]),size=v.readUInt32LE(0),banks=v.readUInt32LE(4),cases=v.readUInt32LE(8),inputSize=v.readUInt32LE(12);let p=16;
if(e.audio_size()!==size)throw Error('Audio state layout mismatch');
for(let b=0;b<banks;b++){
 new Uint32Array(e.memory.buffer,e.program_buffer(),128).set(words[b]);e.program_length(128);
 for(let c=0;c<cases;c++){
  new Uint8Array(e.memory.buffer,e.audio_state(),size).set(v.subarray(p,p+size));p+=size;
  new Uint8Array(e.memory.buffer,e.input_buffer(),inputSize).set(v.subarray(p,p+inputSize));p+=inputSize;e.process_frame();
  const state=Buffer.from(e.memory.buffer,e.audio_state(),size),out=Buffer.from(e.memory.buffer,e.output_buffer(),8);
  if(!state.equals(v.subarray(p,p+size))||!out.equals(v.subarray(p+size,p+size+8)))throw Error(`Mismatch slot ${b+1} case ${c}`);p+=size+8;
 }
}
console.log(`PASS ${banks*cases} browser WebAssembly / native full-state audio comparisons`);
