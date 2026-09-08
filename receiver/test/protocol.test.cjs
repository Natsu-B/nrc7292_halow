'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const fs=require('node:fs');const path=require('node:path');
const {crc32,after,parsePacket,Reassembler,readClip,telemetry}=require('../src/protocol.cjs');
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
 const r=new Reassembler({maxFrames:4});for(let i=0;i<100;i++)r.push(packets(Buffer.alloc(3000),{frame:i,seq:i*3})[0],i);
 assert(r.frames.size<=4);assert(r.metrics.expired>0);
});
test('on-flash real H264 and MJPEG clips validate every CRC',()=>{
 const dir=path.join(__dirname,'../../assets');
 for(const [name,count]of [['test_h264.bin',120],['test_mjpeg.bin',12],['test_h264_1080p.bin',60]]){
  const b=fs.readFileSync(path.join(dir,name));assert.equal(readClip(b).frames.length,count);
  b[b.length-1]^=1;assert.throws(()=>readClip(b),/CRC/);
 }
});
test('radio telemetry checksum',()=>{
 const b=Buffer.alloc(40);b.write('NRCS');b[4]=1;b.writeUInt16BE(40,6);b.writeInt8(-57,12);b[16]=1;b.writeUInt32BE(crc32(b.subarray(0,36)),36);
 assert.equal(telemetry(b).rssi,-57);b[12]^=1;assert.equal(telemetry(b),null);
});

test('late packets from a retired stream cannot reset the current session',()=>{
 const r=new Reassembler();r.push(packets(Buffer.alloc(30),{stream:1})[0],0);
 r.push(packets(Buffer.alloc(30),{stream:2})[0],1);
 assert.equal(r.push(packets(Buffer.alloc(30),{stream:1,seq:1,frame:1})[0],2).length,0);
 assert.equal(r.stream,2);
});
