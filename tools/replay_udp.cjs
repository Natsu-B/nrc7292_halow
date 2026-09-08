#!/usr/bin/env node
'use strict';
/* Developer-only transmitter for testing the packaged receiver without any radio.
 * Use loopback by default. Runtime users do not need Node or this script. */
const dgram=require('node:dgram'),fs=require('node:fs'),path=require('node:path');
const {isIPv4}=require('node:net');const {readClip}=require('../receiver/src/protocol.cjs');
const {packets}=require('../receiver/test/helpers.cjs');
const clip=readClip(fs.readFileSync(process.argv[2]||path.join(__dirname,'../assets/video.bin')));
const destination=process.argv[3]||'127.0.0.1',port=Number(process.argv[4]||50000);
if(!isIPv4(destination)||!Number.isInteger(port)||port<1024||port>65535)throw new Error('Invalid IPv4/port');
const s=dgram.createSocket('udp4'),stream=(Date.now()>>>0);let sequence=0;
(async()=>{const epoch=performance.now();for(let i=0;i<clip.frames.length;i++){
 const f=clip.frames[i];await new Promise(r=>setTimeout(r,Math.max(0,epoch+f.pts-performance.now())));
 const ps=packets(f.data,{frame:i,seq:sequence,stream,key:f.key,codec:f.codec,pts:f.pts});sequence+=ps.length;
 for(const b of ps)await new Promise((resolve,reject)=>s.send(b,port,destination,e=>e?reject(e):resolve()));
}console.log(`${clip.frames.length} frames, ${sequence} packets sent to ${destination}:${port}`);})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>s.close());
