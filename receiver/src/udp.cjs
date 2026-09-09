'use strict';
const dgram=require('node:dgram');const {EventEmitter}=require('node:events');const {performance}=require('node:perf_hooks');
const {isIPv4}=require('node:net');const {Reassembler,parsePacket,telemetry,feedback,probeAck}=require('./protocol.cjs');
class UdpReceiver extends EventEmitter{
  constructor(){super();this.socket=null;this.timer=null;this.generation=0;}
  async start({bind='0.0.0.0',port=50000,source='192.168.10.20',feedbackEnabled=true}={}){
    if(this.socket)throw new Error('Receiver already running');
    if(!isIPv4(bind)||!isIPv4(source)||!Number.isInteger(port)||port<1024||port>65535)throw new Error('IPv4 and UDP port are required');
    this.reassembler=new Reassembler();this.source=source;this.remote=null;this.radio=null;this.lastRx=null;this.probeAcks=0;this.previous={packets:0,bytes:0,completed:0,highest:null,time:performance.now(),stream:null};
    const s=dgram.createSocket('udp4');this.socket=s;const gen=++this.generation;
    s.on('message',(b,r)=>{
      if(gen!==this.generation||r.address!==source)return;
      const now=performance.now();
      if(b.toString('ascii',0,4)==='NRCS'){
        const t=telemetry(b);if(!t)return;
        this.radio={...t,receivedAt:now};
        if(t.version===4){
          // An echoed radio sample proves this small round trip, never video capacity.
          if(feedbackEnabled)s.send(probeAck(t),r.port,r.address,error=>{if(error)this.emit('fault',error.message);else this.probeAcks++;});
          this.emit('probe',{stream:t.stream,sampleMs:t.sampleMs,peer:{...r}});
        }
        return;
      }
      const packet=parsePacket(b);
      const m=this.reassembler.metrics,before=m.invalid,oldStream=this.reassembler.stream;
      const baseline={packets:m.packets-m.duplicate,bytes:m.bytes,completed:m.completed,highest:null,time:now,stream:packet?.stream};
      const frames=this.reassembler.push(b,now);
      if(oldStream!==this.reassembler.stream)this.previous=baseline;
      // Retired streams and malformed fragments must not redirect or revive feedback.
      if(packet&&packet.stream===this.reassembler.stream&&this.reassembler.metrics.invalid===before){this.remote={...r,stream:packet.stream};this.lastRx=now;}
      for(const f of frames)this.emit('frame',f);
    });
    await new Promise((resolve,reject)=>{const initial=e=>{this.socket=null;try{s.close();}catch{}reject(e);};s.once('error',initial);s.bind(port,bind,()=>{s.off('error',initial);s.on('error',e=>this.emit('fault',e.message));resolve();});});
    this.timer=setInterval(()=>{
      const now=performance.now();for(const f of this.reassembler.drain(now))this.emit('frame',f);
      const m=this.reassembler.metrics,p=this.previous,interval=Math.max(1,Math.round(now-p.time));
      // A stream may begin immediately before this tick. Preserve its first real
      // delivery until a firmware-valid interval exists rather than discard it.
      if(interval<100)return;
      const received=Math.max(0,m.packets-m.duplicate-(p.packets||0)),bytes=m.bytes-p.bytes;
      const completed=Math.max(0,m.completed-p.completed);
      let expected=received;
      if(p.highest!==null&&this.reassembler.highest!==null&&p.stream===this.reassembler.stream){const delta=(this.reassembler.highest-p.highest)>>>0;if(delta<0x80000000)expected=delta;}
      const lost=Math.max(0,expected-received),goodput=Math.round(bytes*8000/interval);
      // Loss is a sequence-gap estimate, not a definitive RF PER measurement.
      const stats={...m,goodputBps:goodput,lossEstimate:expected?lost/expected:0,lastRxMs:this.lastRx===null?null:Math.round(now-this.lastRx),radio:this.radio,source,
        intervalMs:interval,receivedInInterval:received,lostInInterval:lost,completedInInterval:completed,stream:this.reassembler.stream,probeAcks:this.probeAcks};
      this.emit('stats',stats);
      if(feedbackEnabled&&p.stream===this.reassembler.stream&&this.remote?.stream===this.reassembler.stream&&now-this.lastRx<3000){const b=feedback({interval,highest:this.reassembler.highest??0,received,lost,completed,goodput,now:Math.floor(now),stream:this.reassembler.stream});s.send(b,this.remote.port,this.remote.address,()=>{});}
      this.previous={packets:m.packets-m.duplicate,bytes:m.bytes,completed:m.completed,highest:this.reassembler.highest,time:now,stream:this.reassembler.stream};
    },200);
  }
  stop(){this.generation++;if(this.timer)clearInterval(this.timer);this.timer=null;if(this.socket){try{this.socket.close();}catch{}}this.socket=null;}
}
module.exports={UdpReceiver};
