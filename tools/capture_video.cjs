#!/usr/bin/env node
'use strict';
// Reuse the production NRCV parser; this receives ESP packets and never replays locally.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isIPv4}=require('node:net');
const {UdpReceiver}=require('../receiver/src/udp.cjs');
const {readClip,crc32}=require('../receiver/src/protocol.cjs');
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
function matchFrame(frame,clip){
  // The firmware may skip late source frames; NRCV frame IDs count emitted frames.
  // ponytail: linear reference scan; index hashes if longer clips make this expensive.
  return clip.frames.findIndex(ref=>ref.codec===frame.codec&&ref.key===frame.key&&
    ref.data.length===frame.data.length&&crc32(ref.data)===crc32(frame.data)&&ref.data.equals(frame.data));
}
class CycleVerifier{
  constructor(clip){this.clip=clip;this.stream=null;this.frames=new Map();this.starts=new Set();this.cycle=null;this.matched=0;this.mismatched=0;this.streamResets=0;}
  push(frame){
    if(this.stream!==frame.stream){
      if(this.stream!==null)this.streamResets++;
      this.stream=frame.stream;this.frames.clear();this.starts.clear();
    }
    if(matchFrame(frame,this.clip)<0){this.mismatched++;return;}
    this.matched++;
    if(this.cycle)return;
    this.frames.set(frame.frame,frame);
    if(matchFrame(frame,{frames:[this.clip.frames[0]]})===0)this.starts.add(frame.frame);
    // ponytail: two clip lengths bound completion reordering; increase only for measured longer reordering.
    while(this.frames.size>this.clip.frames.length*2){
      const oldest=this.frames.keys().next().value;this.frames.delete(oldest);this.starts.delete(oldest);
    }
    for(const start of this.starts){
      const cycle=[];let valid=true;
      for(let i=0;i<this.clip.frames.length;i++){
        const actual=this.frames.get((start+i)>>>0);
        if(!actual)continue;
        if(matchFrame(actual,{frames:[this.clip.frames[i]]})!==0){valid=false;break;}
        cycle.push(actual);
      }
      if(!valid){this.starts.delete(start);continue;}
      if(cycle.length===this.clip.frames.length){
        this.cycle={stream:this.stream,firstFrame:start,lastFrame:(start+cycle.length-1)>>>0,frames:cycle};
        return;
      }
    }
  }
}
async function main(){
  const source=process.env.ESP_IP,bind=process.env.PC_IP;
  const seconds=Number(process.env.VIDEO_SECONDS||30),port=Number(process.env.UDP_PORT||50000);
  if(!isIPv4(source)||!isIPv4(bind)||source.startsWith('127.')||bind.startsWith('127.'))
    throw new Error('Set ESP_IP and PC_IP to actual bench IPv4 addresses. Local replay is not an RF test.');
  if(!Number.isFinite(seconds)||seconds<=0||seconds>3600)throw new Error('VIDEO_SECONDS must be 1..3600');
  const clip=readClip(fs.readFileSync(process.env.VIDEO_CLIP||path.join(__dirname,'../assets/video.bin')));
  if(clip.codec!==1||clip.height!==360)throw new Error('Stage-1 video test requires a 360p H.264 HVC1 reference clip.');
  const out=process.env.LOG_DIR;if(!out)throw new Error('LOG_DIR is required');
  fs.mkdirSync(out,{recursive:true});
  const verified=new CycleVerifier(clip),rx=new UdpReceiver();let stats=null,fault=null;
  rx.on('fault',message=>{fault=message;});
  rx.on('stats',value=>{stats=value;});
  rx.on('frame',frame=>{verified.push(frame);});
  try{
    await rx.start({source,bind,port,feedbackEnabled:false});
    fs.writeFileSync(path.join(out,'video.ready'),`${source} -> ${bind}:${port}\n`);
    console.log(`Listening for ESP NRCV: ${source} -> ${bind}:${port}; ${seconds}s`);
    await new Promise(resolve=>setTimeout(resolve,seconds*1000));
  }finally{rx.stop();}
  const metrics=rx.reassembler.metrics;
  const pass=!fault&&verified.mismatched===0&&metrics.invalid===0&&verified.cycle!==null;
  const result={result:pass?'PASS':'FAIL',source,bind,port,width:clip.width,height:clip.height,
    criterion:'one complete same-stream cycle with consecutive NRCV frame IDs and byte-exact source frame order',
    expectedFrames:clip.frames.length,verifiedFrames:verified.cycle?.frames.length??0,
    verifiedStream:verified.cycle?.stream??null,firstFrame:verified.cycle?.firstFrame??null,lastFrame:verified.cycle?.lastFrame??null,
    matchedFrames:verified.matched,mismatchedFrames:verified.mismatched,streamResets:verified.streamResets,
    metrics,lossEstimate:stats?.lossEstimate??null,duty:'NOT_MEASURED: correlate esp_video.log duty samples; gaps alone are not duty stops',fault};
  if(pass){
    const output=Buffer.concat(verified.cycle.frames.map(frame=>frame.data));
    const expected=Buffer.concat(clip.frames.map(frame=>frame.data));
    if(!output.equals(expected))throw new Error('Ordered H.264 output differs from reference');
    fs.writeFileSync(path.join(out,'received_360p.h264'),output);
    result.h264Bytes=output.length;result.h264Sha256=digest(output);
  }
  fs.writeFileSync(path.join(out,'video_result.json'),JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify(result));process.exitCode=pass?0:1;
}
if(require.main===module)main().catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={matchFrame,CycleVerifier};
