'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const dgram=require('node:dgram');const fs=require('node:fs');const path=require('node:path');
const {once}=require('node:events');const {UdpReceiver}=require('../src/udp.cjs');const {readClip,crc32}=require('../src/protocol.cjs');const {packets}=require('./helpers.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function boundSocket(t){
 const s=dgram.createSocket('udp4');t.after(()=>{try{s.close();}catch{}});
 const listening=once(s,'listening');s.bind(0,'127.0.0.1');await listening;return s;
}
async function startReceiver(t,config={}){
 const reservation=await boundSocket(t),port=reservation.address().port;
 await new Promise(resolve=>reservation.close(resolve));
 const receiver=new UdpReceiver();t.after(()=>receiver.stop());await receiver.start({bind:'127.0.0.1',source:'127.0.0.1',port,...config});
 return {receiver,port};
}
const send=(socket,b,port)=>new Promise((resolve,reject)=>socket.send(b,port,'127.0.0.1',e=>e?reject(e):resolve()));
async function waitFor(predicate){for(let i=0;i<100;i++){if(predicate())return;await delay(10);}assert.fail('localhost UDP condition timed out');}
const nextStats=r=>once(r,'stats',{signal:AbortSignal.timeout(2500)}).then(([stats])=>stats);
function checkFeedback(b,{stream,highest,received,lost,goodput}){
 assert.equal(b.length,40);assert.equal(b.toString('ascii',0,4),'NRFB');assert.equal(b[4],2);
 assert.equal(b.readUIntBE(5,3),crc32(b.subarray(8,40))&0xffffff);assert.equal(b.readUInt32BE(36),stream);
 assert.equal(b.readUInt32BE(12),highest);assert.equal(b.readUInt32BE(16),received);assert.equal(b.readUInt32BE(20),lost);
 assert.equal(b.readUInt16BE(24),Math.floor(1000*lost/(received+lost)));assert.equal(b.readUInt32BE(28),goodput);
}
test('localhost UDP reconstructs all real H264 access units byte for byte',async t=>{
 const clip=readClip(fs.readFileSync(path.join(__dirname,'../../assets/test_h264.bin')));
 const {receiver,port}=await startReceiver(t),sender=await boundSocket(t),got=[];
 receiver.on('frame',f=>got.push(f));let seq=0;
 for(let i=0;i<clip.frames.length;i++){
  const f=clip.frames[i],ps=packets(f.data,{seq,frame:i,codec:f.codec,key:f.key,pts:f.pts});seq+=ps.length;
  for(const b of ps)await send(sender,b,port);await delay(3);
 }
 await waitFor(()=>got.length===clip.frames.length);
 assert.deepEqual(Buffer.concat(got.map(f=>f.data)),Buffer.concat(clip.frames.map(f=>f.data)));
});
test('unexpected source IP is ignored',async t=>{
 const {receiver:r,port}=await startReceiver(t,{source:'192.0.2.2'}),s=await boundSocket(t);let frames=0;r.on('frame',()=>frames++);
 await send(s,packets(Buffer.alloc(20))[0],port);await delay(40);
 assert.equal(frames,0);assert.equal(r.remote,null);assert.equal(r.lastRx,null);
});
test('invalid listening configuration is refused',async()=>{
 const r=new UdpReceiver();await assert.rejects(()=>r.start({source:'',port:70000}));
});
test('default feedback follows the current stream and peer, rejecting invalid and retired traffic',async t=>{
 const {receiver:r,port}=await startReceiver(t),oldPeer=await boundSocket(t),currentPeer=await boundSocket(t),strayPeer=await boundSocket(t);
 const oldFeedback=[],currentFeedback=[],strayFeedback=[];
 oldPeer.on('message',b=>oldFeedback.push(b));currentPeer.on('message',b=>currentFeedback.push(b));strayPeer.on('message',b=>strayFeedback.push(b));
 const packet=(stream,frame,seq)=>packets(Buffer.alloc(20,frame),{stream,frame,seq})[0];
 let tick=nextStats(r);
 await send(oldPeer,packet(0x10203040,0,10),port);await send(oldPeer,packet(0x10203040,1,11),port);
 await tick;await delay(30);assert.equal(oldFeedback.length,0,'first stream interval establishes its baseline');
 tick=nextStats(r);
 await send(oldPeer,packet(0x10203040,2,12),port);await send(oldPeer,packet(0x10203040,3,14),port);
 const firstStats=await tick;await waitFor(()=>oldFeedback.length===1);
 checkFeedback(oldFeedback[0],{stream:0x10203040,highest:14,received:2,lost:1,goodput:firstStats.goodputBps});

 tick=nextStats(r);await send(currentPeer,packet(0x50607080,0,4000),port);
 await waitFor(()=>r.reassembler.stream===0x50607080);
 const lastRx=r.lastRx,invalidBefore=r.reassembler.metrics.invalid;
 await send(strayPeer,packet(0x10203040,4,15),port);await send(strayPeer,Buffer.from('invalid NRCV'),port);
 const corrupt=packet(0x50607080,1,4001);corrupt[7]=35;await send(strayPeer,corrupt,port);
 await waitFor(()=>r.reassembler.metrics.invalid>=invalidBefore+2);await delay(20);
 assert.equal(r.remote.port,currentPeer.address().port);assert.equal(r.lastRx,lastRx,'ignored traffic must not revive reception age');
 await tick;await delay(30);
 assert.equal(oldFeedback.length,1);assert.equal(currentFeedback.length,0,'stream change suppresses mixed aggregate feedback');assert.equal(strayFeedback.length,0);

 tick=nextStats(r);
 await send(currentPeer,packet(0x50607080,1,4001),port);await send(currentPeer,packet(0x50607080,2,4003),port);
 const currentStats=await tick;await waitFor(()=>currentFeedback.length===1);
 checkFeedback(currentFeedback[0],{stream:0x50607080,highest:4003,received:2,lost:1,goodput:currentStats.goodputBps});
 assert.equal(currentStats.lossEstimate,1/3);assert.equal(oldFeedback.length,1);assert.equal(strayFeedback.length,0);
});
test('localhost receiver accepts NRCS v2 without retargeting the video peer',async t=>{
 const {receiver:r,port}=await startReceiver(t),videoPeer=await boundSocket(t),radioPeer=await boundSocket(t);
 await send(videoPeer,packets(Buffer.alloc(20),{stream:7})[0],port);await waitFor(()=>r.remote!==null);
 const lastRx=r.lastRx,b=Buffer.alloc(48);b.write('NRCS');b[4]=2;b.writeUInt16BE(48,6);b.writeInt8(-62,12);
 b.writeUInt32BE(125000,36);b.writeUInt32BE(0xffffffff,40);b.writeUInt32BE(crc32(b.subarray(0,44)),44);
 await send(radioPeer,b,port);await waitFor(()=>r.radio!==null);
 assert.equal(r.radio.rssi,-62);assert.equal(r.radio.adaptivePaceBps,125000);assert.equal(r.radio.feedbackAgeMs,0xffffffff);
 assert.equal(r.remote.port,videoPeer.address().port);assert.equal(r.lastRx,lastRx);
});
