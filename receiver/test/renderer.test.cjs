'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
function renderer(){
 const elements=new Map(),events={},decoders=[],drawn=[];let done=0;
 const element=id=>{if(!elements.has(id))elements.set(id,{textContent:'',value:'',hidden:false,dataset:{},classList:{toggle(){}},getContext:()=>({drawImage:image=>drawn.push(image)})});return elements.get(id);};
 const api={on:(kind,fn)=>events[kind]=fn,frameDone:()=>done++};
 class Decoder{
  static async isConfigSupported(){return {supported:true};}
  constructor(callbacks){this.callbacks=callbacks;this.state='unconfigured';this.decodeQueueSize=0;decoders.push(this);}
  configure(){this.state='configured';}close(){this.state='closed';}decode(){this.decodeQueueSize++;}
 }
 const context={document:{getElementById:element,body:element('body')},window:{halow:api,VideoDecoder:Decoder,addEventListener(){}},VideoDecoder:Decoder,EncodedVideoChunk:class{constructor(fields){Object.assign(this,fields);}},Uint8Array,Date,console};
 vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../ui/renderer.js'),'utf8'),context);
 return {events,element,decoders,drawn,get done(){return done;}};
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const key=frame=>({stream:1,frame,pts:frame*2000,key:true,codec:1,data:Uint8Array.from([0,0,0,1,0x67,0x42,0xc0,0x16,0])});
test('renderer supersedes queued independent images and rejects old asynchronous decoder output',async()=>{
 const r=renderer();r.events.frame(key(0));r.events.frame(key(1));await tick();
 assert.equal(r.done,2);assert.equal(r.decoders.length,1,'older queued keyframe was not decoded');
 const old=r.decoders[0];r.events.frame(key(2));await tick();
 assert.equal(old.state,'closed');assert.equal(r.decoders.length,2);
 let closed=0;const image=()=>({displayWidth:214,displayHeight:120,close:()=>closed++});
 old.callbacks.output(image());assert.equal(r.drawn.length,0,'obsolete decode output must not overwrite the latest picture');
 r.decoders[1].callbacks.output(image());assert.equal(r.drawn.length,1);assert.equal(closed,2);
 assert.equal(r.element('body').dataset.displayedFrames,'1');assert.equal(r.done,3);
});
test('renderer displays v3 profile, v2 pacing and hides unavailable v1 pacing',()=>{
 const r=renderer(),stats={goodputBps:50000,completed:1,lossEstimate:0,lastRxMs:0,expired:0,invalid:0};
 r.events.stats({...stats,radio:{rssi:-70,txMcs:0,rxMcs:0,duty:true,queue:0,adaptivePaceBps:50000,feedbackAgeMs:0xffffffff,width:214,height:120,intervalMs:10000}});
 assert.equal(r.element('pacing').hidden,false);assert.match(r.element('pacing').textContent,/214×120 \/ 10\.0秒間隔/);assert.match(r.element('pacing').textContent,/未受信/);
 r.events.stats({...stats,radio:{adaptivePaceBps:100000,feedbackAgeMs:20}});
 assert.equal(r.element('pacing').hidden,false);assert.match(r.element('pacing').textContent,/100\.0 kbit\/s/);assert.doesNotMatch(r.element('pacing').textContent,/undefined|214×120/);
 r.events.stats({...stats,radio:{rssi:-65}});assert.equal(r.element('pacing').hidden,true);
});
