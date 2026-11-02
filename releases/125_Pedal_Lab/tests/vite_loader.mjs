// SPDX-License-Identifier: MPL-2.0
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';
const require=createRequire(new URL('../../../tools/sitegen/package.json',import.meta.url));
const {createServer}=await import(pathToFileURL(require.resolve('vite')).href);
test('Vite serves the worker and opaque Python runtime without resolving Node dependencies',async()=>{
 const cacheDir=await mkdtemp(path.join(tmpdir(),'pedal-vite-test-'));
 const server=await createServer({cacheDir,configFile:false,root:fileURLToPath(new URL('../web/',import.meta.url)),logLevel:'silent',server:{host:'127.0.0.1',port:0,watch:null}});
 try{
  await server.listen();const origin='http://127.0.0.1:'+server.httpServer.address().port;
  const worker=await fetch(origin+'/compiler/compiler-worker.js');assert.equal(worker.status,200,await worker.text());
  for(const name of ['pyodide.mjs','pyodide.asm.js']){
   const r=await fetch(origin+'/compiler/python/'+name+'.txt');assert.equal(r.status,200);assert.ok(r.headers.get('content-type').startsWith('text/plain'));assert.ok((await r.text()).includes('import("ws")'));
  }
 }finally{await server.close();await rm(cacheDir,{recursive:true,force:true})}
});
