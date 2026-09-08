'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const dgram=require('node:dgram');const fs=require('node:fs');const path=require('node:path');
const {UdpReceiver}=require('../src/udp.cjs');const {readClip}=require('../src/protocol.cjs');const {packets}=require('./helpers.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
test('localhost UDP reconstructs all real H264 access units byte for byte',async()=>{
 const clip=readClip(fs.readFileSync(path.join(__dirname,'../../assets/test_h264.bin')));
 const receiver=new UdpReceiver(),sender=dgram.createSocket('udp4'),got=[];
 receiver.on('frame',f=>got.push(f));await receiver.start({bind:'127.0.0.1',source:'127.0.0.1',port:51541});
 try{
  let seq=0;
  for(let i=0;i<clip.frames.length;i++){
   const f=clip.frames[i],ps=packets(f.data,{seq,frame:i,codec:f.codec,key:f.key,pts:f.pts});seq+=ps.length;
   for(const b of ps)await new Promise((resolve,reject)=>sender.send(b,51541,'127.0.0.1',e=>e?reject(e):resolve()));
   await delay(3);
  }
  for(let i=0;i<100&&got.length<clip.frames.length;i++)await delay(10);
  assert.equal(got.length,clip.frames.length);
  assert.deepEqual(Buffer.concat(got.map(f=>f.data)),Buffer.concat(clip.frames.map(f=>f.data)));
 }finally{receiver.stop();sender.close();}
});
test('unexpected source IP is ignored',async()=>{
 const r=new UdpReceiver(),s=dgram.createSocket('udp4');let frames=0;r.on('frame',()=>frames++);
 await r.start({bind:'127.0.0.1',source:'192.0.2.2',port:51542});
 try{s.send(packets(Buffer.alloc(20))[0],51542,'127.0.0.1');await delay(40);assert.equal(frames,0);}finally{r.stop();s.close();}
});
test('invalid listening configuration is refused',async()=>{
 const r=new UdpReceiver();await assert.rejects(()=>r.start({source:'',port:70000}));
});
