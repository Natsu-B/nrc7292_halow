#!/usr/bin/env node
'use strict';
// Near-bench RF delivery with explicitly synthetic adaptation feedback. Never a distance test.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {performance}=require('node:perf_hooks'),{isIPv4}=require('node:net');
const {UdpReceiver}=require('../receiver/src/udp.cjs');
const {readClip,crc32,feedback}=require('../receiver/src/protocol.cjs');
const LABEL='SYNTHETIC_FEEDBACK_NOT_RF_DISTANCE';
const dimensions=[[640,360],[640,360],[426,240],[320,180],[214,120]];
const ladder=[[640,360,100],[640,360,500],[640,360,1000],[640,360,2000],[426,240,2000],[320,180,2000],[214,120,2000],[214,120,4000],[214,120,6000],[214,120,10000]];
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function readBundle(b){
  if(!Buffer.isBuffer(b)||b.length<112||b.length>2*1024*1024||b.toString('ascii',0,4)!=='HVB1'||
     b.readUInt16LE(4)!==1||b.readUInt16LE(6)!==32||b.readUInt32LE(8)!==5||b.readUInt32LE(12)!==b.length||
     b.readUInt32LE(20)||b.readUInt32LE(24)||crc32(b.subarray(0,28))!==b.readUInt32LE(28)||
     crc32(b.subarray(32,112))!==b.readUInt32LE(16))throw new Error('Invalid HVB1 header/index/CRC');
  const profiles=[],ranges=[],references=new Map();
  for(let index=0;index<5;index++){
    const at=32+16*index,offset=b.readUInt32LE(at),length=b.readUInt32LE(at+4),flags=b.readUInt32LE(at+8);
    if(offset<112||length<32||offset+length>b.length||flags!==(index===0?0:1)||b.readUInt32LE(at+12)||
       ranges.some(([start,end])=>offset<end&&start<offset+length))throw new Error('Invalid HVB1 profile range/flags');
    ranges.push([offset,offset+length]);const clip=readClip(b.subarray(offset,offset+length));
    if(clip.codec!==1||clip.width!==dimensions[index][0]||clip.height!==dimensions[index][1]||
       (index&&clip.duration!==profiles[0].duration)||b.readUInt16LE(offset+16)!==(index?5:10)||
       b.readUInt16LE(offset+18)!==1||32+clip.frames.reduce((n,f)=>n+16+f.data.length,0)!==length||
       (index&&clip.frames.some(f=>!f.key)))throw new Error('Unexpected HVB1 profile geometry/timing/dependency');
    profiles.push({index,width:clip.width,height:clip.height,frames:clip.frames.length,duration:clip.duration,independent:!!flags,bytes:length});
    clip.frames.forEach((frame,frameIndex)=>{
      const hash=sha(frame.data),ref={...frame,profile:index,frameIndex,width:clip.width,height:clip.height,hash};
      if(!references.has(hash))references.set(hash,[]);references.get(hash).push(ref);
    });
  }
  return {profiles,references};
}
function matchReference(frame,references){
  const hash=sha(frame.data),matches=(references.get(hash)||[]).filter(ref=>ref.codec===frame.codec&&ref.key===frame.key&&ref.data.equals(frame.data));
  return {hash,matches};
}
function profileRank(radio){return radio?.version===3?ladder.findIndex(([w,h,ms])=>radio.width===w&&radio.height===h&&radio.intervalMs===ms):-1;}
function seconds(name,fallback){const value=Number(process.env[name]??fallback);if(!Number.isFinite(value)||value<1||value>300)throw new Error(`${name} must be 1..300 seconds`);return value;}
async function main(){
  const source=process.env.ESP_IP||'192.168.10.20',bind=process.env.PC_IP||'192.168.10.250',port=Number(process.env.UDP_PORT||50000);
  if(!isIPv4(source)||!isIPv4(bind)||source.startsWith('127.')||bind.startsWith('127.')||source==='0.0.0.0'||bind==='0.0.0.0'||source===bind)
    throw new Error('Use distinct actual ESP_IP/PC_IP bench IPv4 addresses; no local replay');
  if(!Number.isInteger(port)||port<1024||port>65535)throw new Error('UDP_PORT must be 1024..65535');
  const durations={initial:seconds('RANGE_HEALTHY_SECONDS',15),bad:seconds('RANGE_BAD_SECONDS',25),badMax:seconds('RANGE_BAD_MAX_SECONDS',45),recovery:seconds('RANGE_RECOVERY_SECONDS',30),startup:seconds('RANGE_STARTUP_SECONDS',45),idle:seconds('RANGE_IDLE_SECONDS',30)};
  if(durations.badMax<durations.bad)throw new Error('RANGE_BAD_MAX_SECONDS must be at least RANGE_BAD_SECONDS');
  const bundlePath=path.resolve(process.env.VIDEO_PROFILES||path.join(__dirname,'../.build-range-video/video_profiles.bin'));
  const bundleBytes=fs.readFileSync(bundlePath),bundle=readBundle(bundleBytes);
  const out=process.env.LOG_DIR;if(!out)throw new Error('LOG_DIR is required');fs.mkdirSync(out,{recursive:true});
  for(const name of ['range.ready','range_result.json']){try{fs.unlinkSync(path.join(out,name));}catch(e){if(e.code!=='ENOENT')throw e;}}
  const timeline=fs.openSync(path.join(out,'range_timeline.jsonl'),'w'),rx=new UdpReceiver(),began=performance.now();
  let phase='waiting_for_video',phaseAt=began,started=null,closed=false,fault=null,lastStats=null,matched=0,mismatched=0,receivedBytes=0;
  let badWorst=-1,lowestMatched=0,recovery=null,pendingSends=0;
  const phaseFrames={},profileFrames={},feedbackSent={initial_real_feedback:0,synthetic_loss:0,synthetic_recovery:0},failures=[],observed=[];
  const log=(event,fields={})=>{fs.writeSync(timeline,JSON.stringify({label:LABEL,elapsedMs:Math.round(performance.now()-began),phase,event,...fields})+'\n');};
  let complete;const completion=new Promise(resolve=>{complete=resolve;});
  const finish=reason=>{if(closed)return;closed=true;if(reason)failures.push(reason);complete();};
  const transition=next=>{log('phase_end');phase=next;phaseAt=performance.now();log('phase_start');};
  rx.on('fault',message=>{fault=message;log('socket_fault',{message});finish('receiver socket fault');});
  rx.on('frame',frame=>{
    if(closed)return;
    if(started===null){started=performance.now();transition('initial_real_feedback');}
    const found=matchReference(frame,bundle.references);
    if(!found.matches.length){mismatched++;log('reference_mismatch',{stream:frame.stream,frame:frame.frame,bytes:frame.data.length,sha256:found.hash});finish('received frame does not equal any HVB1 reference frame');return;}
    matched++;receivedBytes+=frame.data.length;phaseFrames[phase]=(phaseFrames[phase]||0)+1;
    const profiles=[...new Set(found.matches.map(ref=>ref.profile))];for(const index of profiles)profileFrames[index]=(profileFrames[index]||0)+1;
    if(phase==='synthetic_loss'&&profiles.includes(4))lowestMatched++;
    log('frame_match',{stream:frame.stream,frame:frame.frame,pts:frame.pts,bytes:frame.data.length,sha256:found.hash,profiles,
      references:found.matches.map(({profile,frameIndex,width,height})=>({profile,frameIndex,width,height}))});
  });
  rx.on('stats',stats=>{
    if(closed)return;lastStats=stats;const now=performance.now(),radio=stats.radio;
    const freshRadio=radio&&now-radio.receivedAt<2500,rank=freshRadio?profileRank(radio):-1;
    if(rank>=0){
      const observation={elapsedMs:Math.round(now-began),phase,rank,width:radio.width,height:radio.height,intervalMs:radio.intervalMs,paceBps:radio.adaptivePaceBps,feedbackAgeMs:radio.feedbackAgeMs};
      observed.push(observation);
      if(phase==='synthetic_loss')badWorst=Math.max(badWorst,rank);
      if(phase==='synthetic_recovery'&&badWorst>=0&&rank<badWorst&&feedbackSent.synthetic_recovery>=10&&!recovery)recovery={...observation,reportsSent:feedbackSent.synthetic_recovery};
      log('telemetry',{...observation,rssi:radio.rssi,duty:radio.duty,queue:radio.queue,hostDrops:radio.hostDrops,radioDrops:radio.radioDrops});
    }
    log('rx_stats',{packets:stats.packets,completed:stats.completed,invalid:stats.invalid,expired:stats.expired,superseded:stats.superseded,
      referenceDrops:stats.referenceDrops,goodputBps:stats.goodputBps,lossEstimate:stats.lossEstimate,lastRxMs:stats.lastRxMs});
    if(stats.invalid){finish('invalid NRCV datagram from configured source');return;}
    if(started===null){if(now-began>=durations.startup*1000)finish('no complete video frame before startup timeout');return;}
    const lastActivity=Math.max(rx.lastRx??began,radio?.receivedAt??began);
    if(now-lastActivity>=durations.idle*1000){finish('no valid video/NRCS activity before idle timeout');return;}
    const elapsed=(now-phaseAt)/1000;
    if(phase==='initial_real_feedback'&&elapsed>=durations.initial)transition('synthetic_loss');
    else if(phase==='synthetic_loss'&&elapsed>=durations.bad&&(badWorst===9&&lowestMatched>0||elapsed>=durations.badMax))transition('synthetic_recovery');
    else if(phase==='synthetic_recovery'&&elapsed>=durations.recovery){finish();return;}
    const remote=rx.remote,stream=rx.reassembler.stream,p=rx.previous;
    // Bind replies to a video-established stream/peer. Telemetry keeps this known peer
    // live across planned ten-second snapshot gaps; it cannot establish a new peer.
    if(!remote||remote.address!==source||remote.stream!==stream||(!freshRadio&&now-(rx.lastRx??0)>=3000))return;
    const interval=Math.max(100,Math.min(10000,Math.round(now-p.time)));let received,lost;
    if(phase==='synthetic_loss'){received=90;lost=10;}
    else if(phase==='synthetic_recovery'){received=100;lost=0;}
    else{
      if(p.stream!==stream)return;
      received=Math.max(0,stats.packets-stats.duplicate-p.packets);let expected=received;
      if(p.highest!==null&&rx.reassembler.highest!==null){const delta=(rx.reassembler.highest-p.highest)>>>0;if(delta<0x80000000)expected=delta;}
      lost=Math.max(0,expected-received);if(received+lost>65535){log('feedback_skipped',{reason:'counter range exceeds firmware limit'});return;}
    }
    const feedbackPhase=phase,payload=feedback({interval,highest:rx.reassembler.highest??0,received,lost,goodput:stats.goodputBps,now:Math.floor(now),stream});
    pendingSends++;
    rx.socket.send(payload,remote.port,remote.address,error=>{
      pendingSends--;if(error){fault=error.message;if(!closed){log('feedback_send_error',{message:error.message});finish('feedback send error');}return;}
      feedbackSent[feedbackPhase]++;
      if(!closed)log('feedback_sent',{feedbackPhase,mode:feedbackPhase==='initial_real_feedback'?'ACTUAL_COUNTERS':'SYNTHETIC_COUNTERS',stream,received,lost,
        highest:payload.readUInt32BE(12),intervalMs:interval,sourcePort:rx.socket?.address().port,targetPort:remote.port,hex:payload.toString('hex')});
    });
  });
  const interrupt=()=>finish('interrupted before acceptance completed');process.once('SIGINT',interrupt);process.once('SIGTERM',interrupt);
  let deadline;
  try{
    await rx.start({source,bind,port,feedbackEnabled:false});
    log('ready',{source,bind,port,durations,bundlePath,bundleSha256:sha(bundleBytes),physicalPath:'actual near-bench RF, approximately 0.5 m operator-reported; 3 km NOT_TESTED'});
    fs.writeFileSync(path.join(out,'range.ready'),`${LABEL}\n${source} -> ${bind}:${port}\n`);
    console.log(`${LABEL}: listening ${source} -> ${bind}:${port}; root must start the real ESP video sender`);
    deadline=setTimeout(()=>finish('absolute test deadline exceeded'),(durations.startup+durations.initial+durations.badMax+durations.recovery+10)*1000);
    await completion;
  }catch(error){fault=error.message;failures.push(error.message);closed=true;}
  finally{
    clearTimeout(deadline);rx.stop();process.off('SIGINT',interrupt);process.off('SIGTERM',interrupt);
  }
  const checks={actualVideoReceived:matched>0,initialFrames:(phaseFrames.initial_real_feedback||0)>0,
    framesByteExact:mismatched===0&&matched>0,lowestProfileObserved:badWorst===9,actual120pFrameInBadPhase:lowestMatched>0,
    recoveryAfterTenReports:recovery!==null,recoveryFrames:(phaseFrames.synthetic_recovery||0)>0,receiverFaultFree:!fault,
    validNrcv:!!rx.reassembler&&rx.reassembler.metrics.invalid===0};
  for(const [name,pass]of Object.entries(checks))if(!pass)failures.push(name);
  const result={result:failures.length?'FAIL':'PASS',label:LABEL,physicalPath:'ESP -> SX native HSPI -> AP-100AH -> PC; near-bench approximately 0.5 m operator-reported',
    distance3km:'NOT_TESTED',syntheticFeedback:true,productionAutomaticFeedback:false,source,bind,port,durations,
    bundle:{path:bundlePath,bytes:bundleBytes.length,sha256:sha(bundleBytes),profiles:bundle.profiles},checks,failures,fault,
    matchedFrames:matched,mismatchedFrames:mismatched,receivedFrameBytes:receivedBytes,phaseFrames,profileFrames,feedbackSent,pendingSends,
    worstObservedProfile:badWorst,matched120pFramesDuringBad:lowestMatched,recovery,observedProfiles:observed,
    metrics:rx.reassembler?.metrics??null,lastStats: lastStats?{goodputBps:lastStats.goodputBps,lossEstimate:lastStats.lossEstimate}:null,
    duty:'NRCS samples recorded independently; synthetic loss is not a measurement of RF packet loss or Duty',elapsedMs:Math.round(performance.now()-began)};
  log('result',{result:result.result,checks,failures});fs.closeSync(timeline);
  fs.writeFileSync(path.join(out,'range_result.json'),JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify(result));process.exitCode=failures.length?1:0;
}
if(require.main===module)main().catch(error=>{console.error(`${LABEL}: ${error.message}`);process.exitCode=1;});
module.exports={readBundle,matchReference,profileRank};
