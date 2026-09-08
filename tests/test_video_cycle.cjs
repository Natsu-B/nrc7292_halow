'use strict';
const assert=require('node:assert/strict');
const {CycleVerifier}=require('../tools/capture_video.cjs');
const {Reassembler}=require('../receiver/src/protocol.cjs');
const {packets}=require('../receiver/test/helpers.cjs');
const clip={frames:[
  {codec:1,key:true,data:Buffer.alloc(1500,0x11)},
  {codec:1,key:false,data:Buffer.alloc(1500,0x22)},
  {codec:1,key:false,data:Buffer.alloc(1500,0x33)},
]};
const frame=(source,id,stream=1)=>({...clip.frames[source],frame:id,stream});
let verifier=new CycleVerifier(clip);
// Coverage from separate streams must never synthesize one completed cycle.
verifier.push(frame(0,0,1));verifier.push(frame(1,1,2));verifier.push(frame(2,2,3));
assert.equal(verifier.cycle,null);
// Same-stream frame loss, or wrong source order under consecutive IDs, also fails.
verifier=new CycleVerifier(clip);
verifier.push(frame(0,0));verifier.push(frame(1,4));verifier.push(frame(2,8));
assert.equal(verifier.cycle,null);
verifier=new CycleVerifier(clip);
verifier.push(frame(0,0));verifier.push(frame(2,1));verifier.push(frame(1,2));
assert.equal(verifier.cycle,null);
// Completion order and uint32 ID wrap do not change the reconstructed source order.
verifier=new CycleVerifier(clip);
verifier.push(frame(2,0));verifier.push(frame(0,0xfffffffe));verifier.push(frame(1,0xffffffff));
assert.equal(verifier.cycle.stream,1);assert.equal(verifier.cycle.firstFrame,0xfffffffe);
assert.deepEqual(Buffer.concat(verifier.cycle.frames.map(f=>f.data)),Buffer.concat(clip.frames.map(f=>f.data)));
// Actual fragmented NRCV packets may arrive out of order; production reassembly handles them.
verifier=new CycleVerifier(clip);const reassembler=new Reassembler();let seq=0;
for(let i=0;i<clip.frames.length;i++){
  const source=clip.frames[i];const fragments=packets(source.data,{seq,frame:i,codec:source.codec,key:source.key});
  seq+=fragments.length;
  for(const datagram of fragments.reverse())for(const completed of reassembler.push(datagram,0))verifier.push(completed);
}
assert.equal(verifier.cycle.frames.length,3);
console.log('PASS: a video cycle cannot combine streams, gaps or reordered source frames; fragment reordering remains valid.');
