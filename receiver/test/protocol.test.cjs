'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const fs=require('node:fs');const path=require('node:path');
const {crc32,after,parsePacket,Reassembler,readClip,telemetry,feedback}=require('../src/protocol.cjs');
const {packets}=require('./helpers.cjs');
test('CRC32 standard vector and serial wrap',()=>{assert.equal(crc32(Buffer.from('123456789')),0xcbf43926);assert(after(0,0xffffffff));assert(!after(0xffffffff,0));});
test('NRCV rejects inconsistent lengths, flags and excessive allocation',()=>{
 const p=packets(Buffer.alloc(2900))[0];assert(parsePacket(p));
 for(const mutate of [b=>b.writeUInt32BE(0xffffffff,24),b=>b.writeUInt16BE(0,30),b=>b.writeUInt16BE(65535,32),b=>b[6]|=0x80,b=>b[7]=35]){const b=Buffer.from(p);mutate(b);assert.equal(parsePacket(b),null);}
 assert.equal(parsePacket(Buffer.alloc(5000)),null);
});
test('reassembly supports reverse packet order and duplicate suppression',()=>{
 const payload=Buffer.alloc(9500);for(let i=0;i<payload.length;i++)payload[i]=i&255;
 const ps=packets(payload),r=new Reassembler();let frames=[];
 for(const p of [...ps].reverse())frames.push(...r.push(p,0));
 assert.equal(frames.length,1);assert.deepEqual(frames[0].data,payload);assert.equal(r.push(ps[0],1).length,0);assert.equal(r.metrics.duplicate,1);
});
test('incomplete reference frame discards following delta until a new keyframe',()=>{
 const r=new Reassembler({timeoutMs:20});r.push(packets(Buffer.alloc(3000),{frame:0})[0],0);
 const p1=packets(Buffer.alloc(50),{frame:1,seq:3,key:false})[0];assert.equal(r.push(p1,1).length,0);
 assert.equal(r.drain(22).length,0);assert.equal(r.metrics.referenceDrops,1);
 const got=r.push(packets(Buffer.alloc(60),{frame:2,seq:4,key:true})[0],23);assert.equal(got.length,1);
});
test('frame number and packet sequence wrap keep order',()=>{
 const r=new Reassembler();assert.equal(r.push(packets(Buffer.alloc(50),{frame:0xffffffff,seq:0xffffffff})[0],0).length,1);
 assert.equal(r.push(packets(Buffer.alloc(50),{frame:0,seq:0,key:false})[0],1).length,1);
});
test('pending frame allocation is bounded',()=>{
 const r=new Reassembler();for(let i=0;i<100;i++)r.push(packets(Buffer.alloc(3000),{frame:i,seq:i*3})[0],i);
 assert.equal(r.maxFrames,8);assert(r.frames.size<=8);assert(r.metrics.expired>0);
 assert([...r.frames.values()].reduce((sum,f)=>sum+f.data.length,0)<=8*3000);
});
test('default reassembly timeout tolerates a slow frame but expires stalled frames at 5 s',()=>{
 const data=Buffer.alloc(4000,0x5a),ps=packets(data),r=new Reassembler();
 assert.equal(r.push(ps[0],0).length,0);assert.equal(r.drain(2501).length,0);assert.equal(r.frames.size,1);
 assert.equal(r.push(ps[1],2502).length,0);const got=r.push(ps[2],3200);
 assert.equal(got.length,1);assert.deepEqual(got[0].data,data);assert.equal(r.metrics.expired,0);
 const stalled=new Reassembler();stalled.push(ps[0],100);stalled.drain(5099);assert.equal(stalled.frames.size,1);
 stalled.drain(5100);assert.equal(stalled.frames.size,0);assert.equal(stalled.metrics.expired,1);
});
test('a new complete IDR preempts an incomplete reference and queued deltas immediately',()=>{
 const r=new Reassembler(),old=packets(Buffer.alloc(3000),{frame:0,seq:0});
 r.push(old[0],0);assert.equal(r.push(packets(Buffer.alloc(30),{frame:1,seq:3,key:false})[0],10).length,0);
 const current=packets(Buffer.alloc(2000,0x7a),{frame:2,seq:4,key:true});
 assert.equal(r.push(current[1],20).length,0);const got=r.push(current[0],25);
 assert.deepEqual(got.map(f=>f.frame),[2]);assert.deepEqual(got[0].data,Buffer.alloc(2000,0x7a));
 assert.equal(r.metrics.superseded,2);assert.equal(r.metrics.expired,0);
 for(const p of old.slice(1))assert.equal(r.push(p,30).length,0);
 assert.deepEqual(r.push(packets(Buffer.alloc(30),{frame:3,seq:6,key:false})[0],40).map(f=>f.frame),[3]);
});
test('independent images preempt across frame wrap and MJPEG needs no H264 key flag',()=>{
 for(const codec of [1,2]){
  const r=new Reassembler();r.push(packets(Buffer.alloc(3000),{frame:0xfffffffe,seq:10,codec})[0],0);
  r.push(packets(Buffer.alloc(3000),{frame:0xffffffff,seq:13,codec,key:false})[0],1);
  const got=r.push(packets(Buffer.alloc(30),{frame:0,seq:16,codec,key:codec===1})[0],2);
  assert.deepEqual(got.map(f=>f.frame),[0]);assert.equal(r.metrics.superseded,2);assert.equal(r.metrics.expired,0);
 }
});
test('on-flash real H264 and MJPEG clips validate every CRC',()=>{
 const dir=path.join(__dirname,'../../assets');
 for(const [name,count]of [['test_h264.bin',120],['test_mjpeg.bin',12],['test_h264_1080p.bin',60]]){
  const b=fs.readFileSync(path.join(dir,name));assert.equal(readClip(b).frames.length,count);
  b[b.length-1]^=1;assert.throws(()=>readClip(b),/CRC/);
 }
});
test('NRFB feedback keeps the v1 wire layout and binds v2 CRC to the stream, including zero',()=>{
 const args={interval:1000,highest:0xdeadbeef,received:9,lost:1,jitter:12.6,goodput:500000,now:0x12345678};
 // Fixed vectors cross-checked with Python's independent zlib.crc32.
 const vectors=[
  [{},'4e524642016d073c000003e8deadbeef00000009000000010064000d0007a12012345678'],
  [{stream:0},'4e52464202a9ecff000003e8deadbeef00000009000000010064000d0007a1201234567800000000'],
  [{stream:0x89abcdef},'4e5246420234f125000003e8deadbeef00000009000000010064000d0007a1201234567889abcdef'],
 ];
 for(const [extra,hex]of vectors){const b=feedback({...args,...extra});assert.equal(b.toString('hex'),hex);assert.equal(b.readUIntBE(5,3),crc32(b.subarray(8))&0xffffff);}
 const b=feedback({...args,stream:0x89abcdef});b[39]^=1;assert.notEqual(b.readUIntBE(5,3),crc32(b.subarray(8))&0xffffff);
});
function radioPacket(version,age=1234){
 const b=Buffer.alloc(version===1?40:version===2?48:56);b.write('NRCS');b[4]=version;b.writeUInt16BE(b.length,6);
 b.writeUInt32BE(0x12345678,8);b.writeInt8(-57,12);b.writeInt8(23,13);b[14]=4;b[15]=3;b[16]=1;b[17]=1;
 b.writeUInt16BE(17,18);b.writeUInt32BE(11,20);b.writeUInt32BE(12,24);b.writeUInt32BE(13,28);b.writeUInt32BE(14,32);
 if(version>=2){b.writeUInt32BE(250000,36);b.writeUInt32BE(age,40);}
 if(version===3){b.writeUInt16BE(214,44);b.writeUInt16BE(120,46);b.writeUInt32BE(10000,48);}
 b.writeUInt32BE(crc32(b.subarray(0,b.length-4)),b.length-4);return b;
}
test('NRCS v1/v2 remain compatible and v3 exposes current resolution and snapshot interval',()=>{
 const expected={sampleMs:0x12345678,rssi:-57,snr:23,txMcs:4,rxMcs:3,duty:true,connected:true,queue:17,hostDrops:11,radioDrops:12,sentFrames:13,skippedFrames:14};
 for(const version of [1,2,3]){
  const decoded=telemetry(radioPacket(version));assert(decoded);
  if('version'in decoded)assert.equal(decoded.version,version);
  const {version:ignored,...fields}=decoded;
  assert.deepEqual(fields,{...expected,...(version>=2?{adaptivePaceBps:250000,feedbackAgeMs:1234}:{}),...(version===3?{width:214,height:120,intervalMs:10000}:{})});
 }
 assert.equal(telemetry(radioPacket(2,0xffffffff)).feedbackAgeMs,0xffffffff);
});
test('NRCS rejects corrupt fields, CRC, version/length mismatches and trailing bytes',()=>{
 for(const version of [1,2,3]){
  const packet=radioPacket(version);
  for(const offset of [8,12,32,packet.length-1,...(version>=2?[36,40]:[]),...(version===3?[44,46,48]:[])]){
   const bad=Buffer.from(packet);bad[offset]^=1;assert.equal(telemetry(bad),null,`v${version} byte ${offset}`);
  }
  for(const mutate of [b=>b[0]=0,b=>b[4]=4,b=>b[5]=1,b=>b.writeUInt16BE(version===1?48:40,6)]){
   const bad=Buffer.from(packet);mutate(bad);bad.writeUInt32BE(crc32(bad.subarray(0,bad.length-4)),bad.length-4);assert.equal(telemetry(bad),null);
  }
  assert.equal(telemetry(packet.subarray(0,-1)),null);assert.equal(telemetry(Buffer.concat([packet,Buffer.alloc(1)])),null);
 }
});

test('late packets from a retired stream cannot reset the current session',()=>{
 const r=new Reassembler();r.push(packets(Buffer.alloc(30),{stream:1})[0],0);
 r.push(packets(Buffer.alloc(30),{stream:2})[0],1);
 assert.equal(r.push(packets(Buffer.alloc(30),{stream:1,seq:1,frame:1})[0],2).length,0);
 assert.equal(r.stream,2);
});
