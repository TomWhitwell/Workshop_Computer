// SPDX-License-Identifier: MPL-2.0
export function elfToUF2(bytes){
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 if(v.getUint32(0,true)!==0x464c457f||v.getUint8(4)!==1||v.getUint8(5)!==1||v.getUint16(18,true)!==40)throw Error('Compiler did not produce an ARM ELF');
 const start=v.getUint32(28,true),size=v.getUint16(42,true),count=v.getUint16(44,true),segments=[];let end=0x10000000;
 for(let i=0;i<count;i++){const p=start+i*size;if(v.getUint32(p,true)!==1)continue;const offset=v.getUint32(p+4,true),address=v.getUint32(p+12,true),length=v.getUint32(p+16,true);if(!length)continue;if(address<0x10000000||address+length>0x10200000)continue;if(offset+length>bytes.length)throw Error('Truncated firmware segment');segments.push({offset,address,length});end=Math.max(end,address+length)}
 if(!segments.some(s=>s.address===0x10000000)||end<=0x10000100)throw Error('Firmware boot block is missing');
 const blocks=Math.ceil((end-0x10000000)/256),flash=new Uint8Array(blocks*256),uf2=new Uint8Array(blocks*512);
 for(const s of segments)flash.set(bytes.subarray(s.offset,s.offset+s.length),s.address-0x10000000);
 for(let i=0;i<blocks;i++){const b=new DataView(uf2.buffer,i*512,512);[0x0a324655,0x9e5d5157,0x2000,0x10000000+i*256,256,i,blocks,0xe48bff56].forEach((n,k)=>b.setUint32(k*4,n,true));uf2.set(flash.subarray(i*256,i*256+256),i*512+32);b.setUint32(508,0x0ab16f30,true)}
 return uf2;
}
