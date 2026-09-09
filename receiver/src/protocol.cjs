'use strict';
const MAX_FRAME=2*1024*1024;
function crc32(b){let c=0xffffffff;for(const v of b){c^=v;for(let j=0;j<8;j++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (~c)>>>0;}
function after(a,b){const d=(a-b)>>>0;return d!==0&&d<0x80000000;}
function parsePacket(b){
  if(!Buffer.isBuffer(b)||b.length<128||b.length>1400||b.toString('ascii',0,4)!=='NRCV')return null;
  const ver=b[4],codec=b[5],flags=b[6],dg=ver===1?1400:b.readUInt16BE(34);
  if(![1,2].includes(ver)||![1,2].includes(codec)||(flags&~15)||b[7]!==36||dg!==b.length||(ver===1&&b.readUInt16BE(34)!==0))return null;
  const size=b.readUInt32BE(24),index=b.readUInt16BE(28),count=b.readUInt16BE(30),len=b.readUInt16BE(32),cap=dg-36;
  if(!size||size>MAX_FRAME||!count||count!==Math.ceil(size/cap)||index>=count||len!==Math.min(cap,size-index*cap))return null;
  if(Boolean(flags&2)!==(index===0)||Boolean(flags&4)!==(index===count-1))return null;
  return {codec,flags,sequence:b.readUInt32BE(8),stream:b.readUInt32BE(12),frame:b.readUInt32BE(16),pts:b.readUInt32BE(20),size,index,count,len,dg,payload:b.subarray(36,36+len)};
}
class Reassembler {
  constructor({maxFrames=8,timeoutMs=5000}={}){
    this.maxFrames=maxFrames;this.timeoutMs=timeoutMs;this.stream=null;this.frames=new Map();this.next=null;this.waitKey=true;
    this.metrics={packets:0,invalid:0,duplicate:0,outOfOrder:0,completed:0,expired:0,referenceDrops:0,superseded:0,bytes:0};
    this.seen=new Set();this.highest=null;this.retired=new Set();
  }
  reset(stream){this.frames.clear();this.stream=stream;this.next=null;this.waitKey=true;this.seen.clear();this.highest=null;}
  push(b,now){
    const p=parsePacket(b);if(!p){this.metrics.invalid++;return [];}
    if(this.stream!==p.stream){
      if(this.retired.has(p.stream))return [];
      if(this.stream!==null){this.retired.add(this.stream);if(this.retired.size>16)this.retired.delete(this.retired.values().next().value);}
      this.reset(p.stream);
    }
    const m=this.metrics;m.packets++;m.bytes+=b.length;
    if(this.seen.has(p.sequence)){m.duplicate++;return [];}
    this.seen.add(p.sequence);if(this.seen.size>4096)this.seen.delete(this.seen.values().next().value);
    if(this.highest!==null&&!after(p.sequence,this.highest))m.outOfOrder++;else this.highest=p.sequence;
    if(this.next===null)this.next=p.frame;
    if(p.frame!==this.next&&!after(p.frame,this.next))return [];
    if(((p.frame-this.next)>>>0)>4096){this.frames.clear();this.next=p.frame;this.waitKey=true;}
    let f=this.frames.get(p.frame);
    if(!f){
      if(this.frames.size>=this.maxFrames){const oldest=[...this.frames.keys()].sort((a,b)=>((a-this.next)>>>0)-((b-this.next)>>>0))[0];this.frames.delete(oldest);m.expired++;this.waitKey=true;}
      f={...p,key:Boolean(p.flags&1),data:Buffer.alloc(p.size),got:new Set(),born:now};this.frames.set(p.frame,f);
    }
    if(f.size!==p.size||f.count!==p.count||f.dg!==p.dg||f.codec!==p.codec||f.pts!==p.pts||f.key!==Boolean(p.flags&1)){m.invalid++;return [];}
    if(!f.got.has(p.index)){p.payload.copy(f.data,p.index*(p.dg-36));f.got.add(p.index);}
    return this.drain(now);
  }
  drain(now){
    const out=[];
    // A complete independent image can replace older references immediately.
    // Keep a slow frame's assembly budget without delaying newer live snapshots.
    let newest=null;
    for(const [id,f]of this.frames){
      if(f.got.size===f.count&&(f.codec===2||f.key)&&id!==this.next&&after(id,this.next)&&(newest===null||after(id,newest)))newest=id;
    }
    if(newest!==null){
      for(const id of this.frames.keys())if(id!==newest&&after(newest,id)){this.frames.delete(id);this.metrics.superseded++;}
      this.next=newest;this.waitKey=true;
    }
    while(this.frames.size){
      let f=this.frames.get(this.next);
      if(!f){
        const key=[...this.frames.keys()].sort((a,b)=>((a-this.next)>>>0)-((b-this.next)>>>0))[0];
        f=this.frames.get(key);if(now-f.born<this.timeoutMs)break;
        this.metrics.expired++;this.waitKey=true;this.next=key;
      }
      if(f.got.size===f.count){
        this.frames.delete(this.next);this.next=(this.next+1)>>>0;
        if(f.codec===1&&this.waitKey&&!f.key){this.metrics.referenceDrops++;continue;}
        this.waitKey=false;this.metrics.completed++;
        out.push({codec:f.codec,key:f.key,stream:f.stream,frame:f.frame,pts:f.pts,data:f.data});continue;
      }
      if(now-f.born>=this.timeoutMs){this.frames.delete(this.next);this.next=(this.next+1)>>>0;this.metrics.expired++;this.waitKey=true;continue;}
      break;
    }
    return out;
  }
}
function readClip(b){
  if(!Buffer.isBuffer(b)||b.length<32||b.length>32*1024*1024||b.toString('ascii',0,4)!=='HVC1'||b.readUInt16LE(4)!==1||b.readUInt16LE(6)!==32||crc32(b.subarray(0,28))!==b.readUInt32LE(28))throw new Error('Invalid HVC1 header');
  const codec=b[8],width=b.readUInt16LE(10),height=b.readUInt16LE(12),count=b.readUInt32LE(20),duration=b.readUInt32LE(24);
  if(![1,2].includes(codec)||!count||count>100000||!duration||!width||!height||width>1920||height>1088||b[9]||b.readUInt16LE(14)||!b.readUInt16LE(16)||!b.readUInt16LE(18))throw new Error('Invalid clip parameters');
  const frames=[];let at=32,prev=-1;
  for(let i=0;i<count;i++){
    if(at+16>b.length)throw new Error('Truncated clip record');
    const n=b.readUInt32LE(at),pts=b.readUInt32LE(at+4),flags=b.readUInt32LE(at+8),crc=b.readUInt32LE(at+12);at+=16;
    if(!n||n>256*1024||at+n>b.length||pts<=prev||pts>=duration||(flags&~1)||(!i&&!(flags&1)))throw new Error('Invalid clip record');
    const data=b.subarray(at,at+n);if(crc32(data)!==crc)throw new Error('Clip CRC mismatch');
    frames.push({codec,pts,key:Boolean(flags&1),data});at+=n;prev=pts;
  }
  return {codec,width,height,duration,frames};
}
function telemetry(b){
  if(!Buffer.isBuffer(b)||![40,48,56,60].includes(b.length)||b.toString('ascii',0,4)!=='NRCS')return null;
  const version=b[4],length=version===1?40:version===2?48:version===3?56:version===4?60:0;
  if(b.length!==length||b[5]||b.readUInt16BE(6)!==length||crc32(b.subarray(0,length-4))!==b.readUInt32BE(length-4))return null;
  const result={version,sampleMs:b.readUInt32BE(8),rssi:b.readInt8(12),snr:b.readInt8(13),txMcs:b[14],rxMcs:b[15],duty:!!b[16],connected:!!b[17],queue:b.readUInt16BE(18),hostDrops:b.readUInt32BE(20),radioDrops:b.readUInt32BE(24),sentFrames:b.readUInt32BE(28),skippedFrames:b.readUInt32BE(32)};
  if(version>=2){result.adaptivePaceBps=b.readUInt32BE(36);result.feedbackAgeMs=b.readUInt32BE(40);}
  if(version>=3){result.width=b.readUInt16BE(44);result.height=b.readUInt16BE(46);result.intervalMs=b.readUInt32BE(48);}
  if(version===4)result.stream=b.readUInt32BE(52);
  return result;
}
function probeAck({stream,sampleMs}){
  const b=Buffer.alloc(20);b.write('NRPA');b[4]=1;b.writeUInt16BE(20,6);
  b.writeUInt32BE(stream>>>0,8);b.writeUInt32BE(sampleMs>>>0,12);b.writeUInt32BE(crc32(b.subarray(0,16)),16);return b;
}
function feedback({interval,highest,received,lost,jitter=0,goodput,now,stream,completed}){
  const version=stream===undefined?1:completed===undefined?2:3;
  const b=Buffer.alloc(version===1?36:version===2?40:44);b.write('NRFB');b[4]=version;b.writeUInt32BE(interval>>>0,8);b.writeUInt32BE(highest>>>0,12);
  b.writeUInt32BE(received>>>0,16);b.writeUInt32BE(lost>>>0,20);b.writeUInt16BE(Math.min(1000,Math.floor(1000*lost/Math.max(1,received+lost))),24);
  b.writeUInt16BE(Math.min(65535,Math.round(jitter)),26);b.writeUInt32BE(goodput>>>0,28);b.writeUInt32BE(now>>>0,32);
  if(stream!==undefined)b.writeUInt32BE(stream>>>0,36);
  if(version===3)b.writeUInt32BE(completed>>>0,40);
  const crc=crc32(b.subarray(8));b[5]=(crc>>>16)&255;b[6]=(crc>>>8)&255;b[7]=crc&255;return b;
}
module.exports={crc32,after,parsePacket,Reassembler,readClip,telemetry,feedback,probeAck};
