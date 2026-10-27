// SPDX-License-Identifier: MPL-2.0
// Execute the exact firmware DSP entry point in rp2040js and compare full state.
// Usage: node arm_audio_check.cjs RP2040JS ELF AUDIO_VECTORS ARM_NM
const fs=require('fs'),cp=require('child_process');
const cpuMHz=Number(process.env.PEDAL_CPU_MHZ||133);
const [pkg,elfPath,vectorsPath,nm,mode]=process.argv.slice(2);if(!nm)process.exit(2);
const {RP2040}=require(pkg),rp=new RP2040(),core=rp.core;
const elf=fs.readFileSync(elfPath),vectors=fs.readFileSync(vectorsPath);
const shoff=elf.readUInt32LE(32),shsize=elf.readUInt16LE(46),shnum=elf.readUInt16LE(48);let end=0x20000000;
for(let i=0;i<shnum;i++){
 const p=shoff+i*shsize,type=elf.readUInt32LE(p+4),flags=elf.readUInt32LE(p+8),addr=elf.readUInt32LE(p+12),off=elf.readUInt32LE(p+16),size=elf.readUInt32LE(p+20);
 if(type===8||!(flags&2)||!size)continue;
 if(addr>=0x20000000&&addr+size<=0x20042000){rp.sram.set(elf.subarray(off,off+size),addr-0x20000000);end=Math.max(end,addr+size)}
 else if(addr>=0x10000000&&addr+size<=0x11000000)rp.flash.set(elf.subarray(off,off+size),addr-0x10000000);
}
const syms=cp.execFileSync(nm,['-C',elfPath],{encoding:'utf8'}),funcs={};let entry;
for(const line of syms.split('\n')){let m=line.match(/^([\da-f]+)\s+\w\s+spin::program_(\d+)\(/);if(m)funcs[+m[2]]=parseInt(m[1],16);m=line.match(new RegExp('^([\\da-f]+)\\s+\\w\\s+'+(mode==='virtual'?'spin_virtual_frame':mode==='resample'?'spin_resample_frame':'spin_process_frame')+'$'));if(m)entry=parseInt(m[1],16)}
if(!entry)throw Error('No spin_process_frame entry point');
const size=vectors.readUInt32LE(0),banks=vectors.readUInt32LE(4),cases=vectors.readUInt32LE(8),inputSize=vectors.readUInt32LE(12);
const addr=(end+255)&~255,inputAddr=addr+size+32,outputAddr=inputAddr+16,ret=0x20041ff0;
if(outputAddr+1024>=ret)throw Error('Insufficient emulator workspace');let pos=16;const profile=new Map();
for(let b=0;b<banks;++b){let maxCycles=0,maxInstructions=0;
 for(let c=0;c<cases;++c){
  const initial=vectors.subarray(pos,pos+size),input=vectors.subarray(pos+size,pos+size+inputSize);pos+=size+inputSize;
  const expected=vectors.subarray(pos,pos+size),output=vectors.subarray(pos+size,pos+size+8);pos+=size+8;
  rp.sram.set(initial,addr-0x20000000);rp.sram.set(input,inputAddr-0x20000000);core.registers.fill(0);
  core.registers[0]=addr;core.registers[1]=inputAddr;core.registers[2]=outputAddr;core.registers[3]=mode==='resample'?inputAddr:funcs[b]|1;
  core.SP=ret-256;core.LR=ret|1;core.PC=entry;let steps=0,start=core.cycles;
  while(core.PC!==ret){if(++steps>100000)throw Error('Stalled at '+core.PC.toString(16));if(process.env.PROFILE)profile.set(core.PC,(profile.get(core.PC)||0)+1);core.executeInstruction()}
  const actual=Buffer.from(rp.sram.subarray(addr-0x20000000,addr-0x20000000+size));
  const actualOutput=Buffer.from(rp.sram.subarray(outputAddr-0x20000000,outputAddr-0x20000000+8));
  if(!actual.equals(expected)||!actualOutput.equals(output)){const offset=actual.findIndex((v,i)=>v!==expected[i]);throw Error(`Audio state mismatch slot ${b+1} case ${c} byte ${offset}: output ${[...actualOutput]} vs ${[...output]}`)}
  if(process.env.DETAIL)console.log(JSON.stringify({slot:b+1,case:c,firstRun:initial[65832],cycles:core.cycles-start,us:(core.cycles-start)/cpuMHz}));
  maxCycles=Math.max(maxCycles,core.cycles-start);maxInstructions=Math.max(maxInstructions,steps);
 }
 console.log(JSON.stringify({slot:b+1,cases,maxInstructions,estimatedMaxCycles:maxCycles,cpuMHz,estimatedMaxUs:maxCycles/cpuMHz}));
}
console.log(`PASS ${banks*cases} full DSP-path ARM/host comparisons; excludes ComputerCard peripherals and control task`);

if(process.env.PROFILE){const ranges=cp.execFileSync(nm,["-S","-C",elfPath],{encoding:"utf8"}).split("\n").map(l=>l.match(/^([\da-f]+)\s+([\da-f]+)\s+\w\s+(.+)$/)).filter(Boolean).map(m=>({start:parseInt(m[1],16),end:parseInt(m[1],16)+parseInt(m[2],16),name:m[3],count:0}));for(const [pc,count]of profile){const r=ranges.find(r=>r.start<=pc&&pc<r.end);if(r)r.count+=count;}console.log(ranges.filter(r=>r.count).sort((a,b)=>b.count-a.count).slice(0,15));}
