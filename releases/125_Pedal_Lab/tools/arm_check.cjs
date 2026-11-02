// SPDX-License-Identifier: MPL-2.0
// Optional: npm install --prefix /tmp/spin-arm-check rp2040js@1.4.1
// node tools/arm_check.cjs /tmp/spin-arm-check/node_modules/rp2040js ELF VECTORS ARM_NM
// Executes the ACTUAL compiled RP2040 program functions in a Cortex-M0 interpreter.
// Cycle counts are emulator estimates, not hardware deadline measurements.
const fs=require('fs'),cp=require('child_process');
const [pkg,elfFile,vectorsFile,nm]=process.argv.slice(2);
if(!nm){console.error('usage: arm_check.cjs rp2040js-path firmware.elf vectors.bin arm-none-eabi-nm');process.exit(2);}
const {RP2040}=require(pkg);
const rp=new RP2040();const core=rp.core;
const elf=fs.readFileSync(elfFile);const vectors=fs.readFileSync(vectorsFile);
const shoff=elf.readUInt32LE(32),shsize=elf.readUInt16LE(46),shnum=elf.readUInt16LE(48);
for(let i=0;i<shnum;++i) {
  const p=shoff+i*shsize,type=elf.readUInt32LE(p+4),flags=elf.readUInt32LE(p+8),addr=elf.readUInt32LE(p+12),off=elf.readUInt32LE(p+16),size=elf.readUInt32LE(p+20);
  if(type===8||!(flags&2)||!size)continue;
  if(addr>=0x20000000&&addr+size<=0x20042000)rp.sram.set(elf.subarray(off,off+size),addr-0x20000000);
  else if(addr>=0x10000000&&addr+size<=0x11000000)rp.flash.set(elf.subarray(off,off+size),addr-0x10000000);
}
const syms=cp.execFileSync(nm,['-C',elfFile],{encoding:'utf8'});
const funcs={};for(const line of syms.split('\n')) {
  const m=line.match(/^([\da-f]+)\s+\w\s+spin::program_(\d+)\(/);
  if(m)funcs[+m[2]]=parseInt(m[1],16);
}
const size=vectors.readUInt32LE(0),banks=vectors.readUInt32LE(4),cases=vectors.readUInt32LE(8);
const addr=0x20030000,ret=0x20041f00;let pos=12;
for(let b=0;b<banks;++b) {
  let maxCycles=0,maxInstructions=0;
  if(!funcs[b])throw Error('Missing ELF function '+b);
  for(let c=0;c<cases;++c) {
    const input=vectors.subarray(pos,pos+size),expected=vectors.subarray(pos+size,pos+size*2);pos+=size*2;
    rp.sram.set(input,addr-0x20000000);core.registers.fill(0);
    core.registers[0]=addr;core.SP=0x20041e00;core.LR=ret|1;core.PC=funcs[b];
    let instructions=0,start=core.cycles;
    while(core.PC!==ret) {
      if(++instructions>100000)throw Error('Instruction budget exceeded at '+core.PC.toString(16));
      core.executeInstruction();
    }
    const actual=Buffer.from(rp.sram.subarray(addr-0x20000000,addr-0x20000000+size));
    if(!actual.equals(expected)) {
      const offset=actual.findIndex((v,i)=>v!==expected[i]);
      throw Error(`ARM state mismatch bank ${b+1} case ${c} byte ${offset}: ${actual[offset]} vs ${expected[offset]}`);
    }
    maxCycles=Math.max(maxCycles,core.cycles-start);maxInstructions=Math.max(maxInstructions,instructions);
  }
  console.log(JSON.stringify({slot:b+1,cases,maxInstructions,estimatedMaxCycles:maxCycles,estimatedMaxUsAt144MHz:Math.round(maxCycles/144*100)/100}));
}
console.log(`PASS ${banks*cases} full-state ARM vs host comparisons`);
