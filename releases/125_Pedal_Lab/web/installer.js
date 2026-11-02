// SPDX-License-Identifier: MPL-2.0
// Transport: Piers Finlayson's MIT Picoboot, as used by computer.musicthing.co.uk.
import {uf2ToFlashBuffer} from './vendor/picoboot/uf2.js';
export function validateImage(bytes){
 if(!(bytes instanceof Uint8Array)||!bytes.length||bytes.length%512)throw Error('Incomplete UF2 file');
 const total=bytes.length/512,indices=new Set(),addresses=new Set();let last=0;
 for(let n=0;n<total;n++){
  const v=new DataView(bytes.buffer,bytes.byteOffset+n*512,512),flags=v.getUint32(8,true),addr=v.getUint32(12,true),size=v.getUint32(16,true),index=v.getUint32(20,true);
  if(v.getUint32(0,true)!==0x0a324655||v.getUint32(4,true)!==0x9e5d5157||v.getUint32(508,true)!==0x0ab16f30)throw Error('Corrupted UF2 block');
  if(!(flags&0x2000)||(flags&1)||v.getUint32(28,true)!==0xe48bff56)throw Error('This firmware is not an RP2040 flash image');
  if(size!==256||addr%256||addr<0x10000000||addr+size>0x10200000)throw Error('UF2 address is outside the supported 2 MB card flash');
  if(v.getUint32(24,true)!==total||index>=total||indices.has(index)||addresses.has(addr))throw Error('UF2 contains duplicate or missing blocks');
  indices.add(index);addresses.add(addr);last=Math.max(last,addr+size);
 }
 if(addresses.size!==(last-0x10000000)/256)throw Error('UF2 has holes or does not start at the boot block');
 return uf2ToFlashBuffer(bytes);
}
export async function installImage(pb,bytes,expectedHash,onStatus=()=>{}){
 const parsed=validateImage(bytes);
 const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
 const hash=[...digest].map(v=>v.toString(16).padStart(2,'0')).join('');
 if(hash!==expectedHash)throw Error('Downloaded firmware does not match this build');
 onStatus('Writing',0);await pb.flashEraseAndWrite(parsed.address,parsed.data,(done,total)=>onStatus('Writing',done/total));
 onStatus('Verifying',0);const readback=await pb.flashRead(parsed.address,parsed.data.length,(done,total)=>onStatus('Verifying',done/total));
 if(readback.length!==parsed.data.length)throw Error('Incomplete readback: leave the card in BOOTSEL and try again');
 for(let i=0;i<readback.length;i++)if(readback[i]!==parsed.data[i])throw Error('Readback verification failed at byte '+i+'; try installation again');
 onStatus('Verified',1);
 try{await pb.getConnection().reboot(500);return {rebooted:true}}catch{return {rebooted:false}}
}
