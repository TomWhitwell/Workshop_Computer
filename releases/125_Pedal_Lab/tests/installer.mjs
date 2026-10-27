// SPDX-License-Identifier: MPL-2.0
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {webcrypto} from 'node:crypto';
import {installImage,validateImage} from '../web/installer.js';
globalThis.crypto=webcrypto;
const image=new Uint8Array(fs.readFileSync(new URL('../Pedal_Lab.uf2',import.meta.url)));
const parsed=validateImage(image);assert.equal(parsed.address,0x10000000);
const hash=Buffer.from(await webcrypto.subtle.digest('SHA-256',image)).toString('hex');let writes=0,reboots=0;
const pb={async flashEraseAndWrite(address,data,progress){assert.equal(address,parsed.address);writes++;progress(data.length,data.length)},async flashRead(address,length,progress){progress(length,length);return parsed.data.slice()},getConnection(){return {async reboot(){reboots++}}}};
await assert.rejects(()=>installImage(pb,image,'wrong'),/does not match/);assert.equal(writes,0);
const result=await installImage(pb,image,hash);assert.equal(result.rebooted,true);assert.equal(writes,1);assert.equal(reboots,1);
const corrupt=image.slice();new DataView(corrupt.buffer).setUint32(28,0xe48bff59,true);assert.throws(()=>validateImage(corrupt),/not an RP2040/);
assert.throws(()=>validateImage(image.subarray(0,image.length-1)),/Incomplete/);
const wrongRead={...pb,async flashRead(){const b=parsed.data.slice();b[10]^=1;return b}};
await assert.rejects(()=>installImage(wrongRead,image,hash),/verification failed/);
assert.equal(reboots,1);
console.log('PASS RP2040-only UF2 validation, hash binding, write/readback/reboot and failed verification handling');
