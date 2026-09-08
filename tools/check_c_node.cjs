'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');const {Reassembler,readClip}=require('../receiver/src/protocol.cjs');
if(process.argv.length!==4){console.error('usage: node check_c_node.cjs clip.bin C-packets.fixture');process.exit(2);}
const clip=readClip(fs.readFileSync(process.argv[2])),wire=fs.readFileSync(process.argv[3]),r=new Reassembler(),frames=[];let at=0,packets=0;
while(at<wire.length){assert(at+2<=wire.length);const n=wire.readUInt16BE(at);at+=2;assert(at+n<=wire.length);frames.push(...r.push(wire.subarray(at,at+n),packets++));at+=n;}
assert.equal(frames.length,clip.frames.length);for(let i=0;i<frames.length;i++)assert.deepEqual(frames[i].data,clip.frames[i].data);
console.log(`C packetizer -> Node receiver: ${packets} packets / ${frames.length} frames, all bytes match`);
