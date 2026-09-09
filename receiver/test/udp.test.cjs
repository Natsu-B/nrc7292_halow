'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const dgram=require('node:dgram');const fs=require('node:fs');const path=require('node:path');
const {once}=require('node:events');const {UdpReceiver}=require('../src/udp.cjs');const {readClip,crc32}=require('../src/protocol.cjs');const {packets}=require('./helpers.cjs');
const {performance}=require('node:perf_hooks');
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
function checkFeedback(b,{stream,highest,received,lost,completed},stats){
 assert.equal(b.length,44);assert.equal(b.toString('ascii',0,4),'NRFB');assert.equal(b[4],3);
 assert.equal(b.readUIntBE(5,3),crc32(b.subarray(8,44))&0xffffff);assert.equal(b.readUInt32BE(36),stream);
 assert.equal(b.readUInt32BE(12),highest);assert.equal(b.readUInt32BE(16),received);assert.equal(b.readUInt32BE(20),lost);
 assert.equal(b.readUInt16BE(24),Math.floor(1000*lost/Math.max(1,received+lost)));assert.equal(b.readUInt32BE(40),completed);
 assert(stats,'feedback must match an emitted interval');assert.equal(stats.stream,stream);assert.equal(stats.receivedInInterval,received);
 assert.equal(stats.lostInInterval,lost);assert.equal(stats.completedInInterval,completed);
 assert.equal(b.readUInt32BE(28),stats.goodputBps);assert.equal(b.readUInt32BE(8),stats.intervalMs);assert(stats.intervalMs>=100);
}
function matchingStats(all,b){return all.findLast(s=>s.stream===b.readUInt32BE(36)&&s.intervalMs===b.readUInt32BE(8)&&s.receivedInInterval===b.readUInt32BE(16)&&s.completedInInterval===b.readUInt32BE(40));}
function radioPacket(version=4,{stream=0,sampleMs=0}={}){
 const b=Buffer.alloc([0,40,48,56,60][version]);b.write('NRCS');b[4]=version;b.writeUInt16BE(b.length,6);b.writeUInt32BE(sampleMs,8);
 if(version===4)b.writeUInt32BE(stream,52);b.writeUInt32BE(crc32(b.subarray(0,b.length-4)),b.length-4);return b;
}
function checkAck(b,{stream,sampleMs}){
 assert.equal(b.length,20);assert.equal(b.toString('ascii',0,4),'NRPA');assert.equal(b[4],1);assert.equal(b[5],0);assert.equal(b.readUInt16BE(6),20);
 assert.equal(b.readUInt32BE(8),stream);assert.equal(b.readUInt32BE(12),sampleMs);assert.equal(b.readUInt32BE(16),crc32(b.subarray(0,16)));
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
test('200 ms feedback counts the first frame, stream-local delivery and idle intervals without fabrication',async t=>{
 const {receiver:r,port}=await startReceiver(t),oldPeer=await boundSocket(t),currentPeer=await boundSocket(t),strayPeer=await boundSocket(t);
 const oldFeedback=[],currentFeedback=[],strayFeedback=[],stats=[],times=[];
 r.on('stats',s=>stats.push(s));oldPeer.on('message',b=>oldFeedback.push(b));currentPeer.on('message',b=>{currentFeedback.push(b);times.push(performance.now());});strayPeer.on('message',b=>strayFeedback.push(b));
 const packet=(stream,frame,seq)=>packets(Buffer.alloc(20,frame),{stream,frame,seq})[0];
 await send(oldPeer,packet(0x10203040,0,10),port);await send(oldPeer,packet(0x10203040,1,11),port);
 await waitFor(()=>oldFeedback.length>=1);
 checkFeedback(oldFeedback[0],{stream:0x10203040,highest:11,received:2,lost:0,completed:2},matchingStats(stats,oldFeedback[0]));
 await send(oldPeer,packet(0x10203040,2,12),port);await send(oldPeer,packet(0x10203040,3,14),port);
 await waitFor(()=>oldFeedback.length>=2);
 checkFeedback(oldFeedback[1],{stream:0x10203040,highest:14,received:2,lost:1,completed:2},matchingStats(stats,oldFeedback[1]));

 // Unreported old-stream frames must not leak into the new stream's first window.
 await send(oldPeer,packet(0x10203040,4,15),port);await send(oldPeer,packet(0x10203040,5,16),port);
 await send(currentPeer,packet(0x50607080,0,4000),port);
 await waitFor(()=>r.reassembler.stream===0x50607080);
 const lastRx=r.lastRx,invalidBefore=r.reassembler.metrics.invalid;
 await send(strayPeer,packet(0x10203040,6,17),port);await send(strayPeer,Buffer.from('invalid NRCV'),port);
 const corrupt=packet(0x50607080,1,4001);corrupt[7]=35;await send(strayPeer,corrupt,port);
 await waitFor(()=>r.reassembler.metrics.invalid>=invalidBefore+2);await delay(20);
 assert.equal(r.remote.port,currentPeer.address().port);assert.equal(r.lastRx,lastRx,'ignored traffic must not revive reception age');
 await waitFor(()=>currentFeedback.length>=1);
 checkFeedback(currentFeedback[0],{stream:0x50607080,highest:4000,received:1,lost:0,completed:1},matchingStats(stats,currentFeedback[0]));
 assert.equal(oldFeedback.length,2);assert.equal(strayFeedback.length,0);

 await send(currentPeer,packet(0x50607080,1,4001),port);await send(currentPeer,packet(0x50607080,2,4003),port);
 await send(currentPeer,packet(0x50607080,2,4003),port);
 await waitFor(()=>currentFeedback.length>=2);const currentStats=matchingStats(stats,currentFeedback[1]);
 checkFeedback(currentFeedback[1],{stream:0x50607080,highest:4003,received:2,lost:1,completed:2},currentStats);
 assert.equal(currentStats.lossEstimate,1/3);assert.equal(currentStats.duplicate,1);
 await waitFor(()=>currentFeedback.length>=3);const idleStats=matchingStats(stats,currentFeedback[2]);
 checkFeedback(currentFeedback[2],{stream:0x50607080,highest:4003,received:0,lost:0,completed:0},idleStats);
 assert.equal(idleStats.goodputBps,0);assert.equal(oldFeedback.length,2);assert.equal(strayFeedback.length,0);
 for(let i=1;i<3;i++){const elapsed=times[i]-times[i-1];assert(elapsed>=100&&elapsed<600,`expected 200 ms cadence, observed ${elapsed} ms`);}
});
test('a tick within 100 ms preserves the first packet baseline for the next valid interval',async t=>{
 let now=1000,tick;const nativeInterval=globalThis.setInterval;
 t.mock.method(performance,'now',()=>now);
 t.mock.method(globalThis,'setInterval',(callback,ms)=>{assert.equal(ms,200);tick=callback;return nativeInterval(callback,60000);});
 const {receiver:r,port}=await startReceiver(t),peer=await boundSocket(t),replies=[],stats=[];
 peer.on('message',b=>replies.push(b));r.on('stats',s=>stats.push(s));
 await send(peer,packets(Buffer.alloc(20),{stream:19,seq:0xffffffff})[0],port);await waitFor(()=>r.reassembler.metrics.completed===1);
 const baseline=r.previous;now=1050;tick();await delay(10);
 assert.equal(r.previous,baseline);assert.equal(stats.length,0);assert.equal(replies.length,0);
 now=1200;tick();await waitFor(()=>replies.length===1);
 checkFeedback(replies[0],{stream:19,highest:0xffffffff,received:1,lost:0,completed:1},stats[0]);assert.equal(stats[0].intervalMs,200);
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
test('NRCS v4 probes receive immediate ACK before video and for a new stream without retargeting video',async t=>{
 const {receiver:r,port}=await startReceiver(t),probePeer=await boundSocket(t),videoPeer=await boundSocket(t),replies=[],probes=[];
 probePeer.on('message',b=>replies.push(b));r.on('probe',p=>probes.push(p));
 await send(probePeer,radioPacket(4,{stream:0,sampleMs:0xffffffff}),port);await waitFor(()=>replies.length===1);
 checkAck(replies[0],{stream:0,sampleMs:0xffffffff});assert.equal(probes.length,1);assert.equal(probes[0].stream,0);assert.equal(probes[0].sampleMs,0xffffffff);
 assert.equal(probes[0].peer.address,'127.0.0.1');assert.equal(probes[0].peer.port,probePeer.address().port);
 assert.equal(r.remote,null);assert.equal(r.lastRx,null);assert.equal(r.reassembler.stream,null);assert.equal(r.reassembler.metrics.packets,0);assert.equal(r.reassembler.metrics.completed,0);
 await send(videoPeer,packets(Buffer.alloc(20),{stream:7})[0],port);await waitFor(()=>r.remote!==null);
 const lastRx=r.lastRx,received=r.reassembler.metrics.packets,completed=r.reassembler.metrics.completed;
 await send(probePeer,radioPacket(4,{stream:8,sampleMs:0}),port);await waitFor(()=>replies.length===2);
 checkAck(replies[1],{stream:8,sampleMs:0});assert.equal(probes.length,2);assert.equal(r.radio.stream,8);
 assert.equal(r.remote.port,videoPeer.address().port);assert.equal(r.remote.stream,7);assert.equal(r.reassembler.stream,7);assert.equal(r.lastRx,lastRx);
 assert.equal(r.reassembler.metrics.packets,received);assert.equal(r.reassembler.metrics.completed,completed);
});
test('disabled automatic feedback still emits a validated probe but sends no ACK',async t=>{
 const {receiver:r,port}=await startReceiver(t,{feedbackEnabled:false}),peer=await boundSocket(t),replies=[],probes=[];
 peer.on('message',b=>replies.push(b));r.on('probe',p=>probes.push(p));
 await send(peer,radioPacket(4,{stream:0xffffffff,sampleMs:123}),port);await waitFor(()=>probes.length===1);await delay(250);
 assert.equal(probes[0].stream,0xffffffff);assert.equal(probes[0].sampleMs,123);assert.equal(probes[0].peer.port,peer.address().port);
 assert.equal(replies.length,0);assert.equal(r.remote,null);assert.equal(r.reassembler.metrics.packets,0);
});
test('old telemetry versions and invalid NRCS v4 packets never elicit probe ACKs',async t=>{
 const {receiver:r,port}=await startReceiver(t),peer=await boundSocket(t),replies=[],probes=[];
 peer.on('message',b=>replies.push(b));r.on('probe',p=>probes.push(p));
 for(const version of [1,2,3])await send(peer,radioPacket(version),port);
 for(const mutate of [b=>b[52]^=1,b=>b[56]^=1,b=>b[5]=1,b=>b.writeUInt16BE(56,6)]){const b=radioPacket();mutate(b);await send(peer,b,port);}
 await send(peer,radioPacket().subarray(0,59),port);await send(peer,Buffer.concat([radioPacket(),Buffer.alloc(1)]),port);
 await send(peer,radioPacket(3,{sampleMs:42}),port);await waitFor(()=>r.radio?.sampleMs===42);await delay(30);
 assert.equal(replies.length,0);assert.equal(probes.length,0);assert.equal(r.remote,null);assert.equal(r.reassembler.metrics.packets,0);
});
test('NRCS v4 from an unexpected source IP is ignored without ACK or probe event',async t=>{
 const {receiver:r,port}=await startReceiver(t,{source:'192.0.2.2'}),peer=await boundSocket(t),replies=[],probes=[];
 peer.on('message',b=>replies.push(b));r.on('probe',p=>probes.push(p));
 await send(peer,radioPacket(4,{stream:3,sampleMs:4}),port);await delay(40);
 assert.equal(replies.length,0);assert.equal(probes.length,0);assert.equal(r.radio,null);assert.equal(r.remote,null);assert.equal(r.reassembler.metrics.packets,0);
});
