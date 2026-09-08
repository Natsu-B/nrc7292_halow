'use strict';
// Runs in a sandboxed renderer. Network and file access are confined to the main process.
const $=id=>document.getElementById(id),api=window.halow,canvas=$('video');
const ctx=canvas.getContext('2d',{alpha:false});
let decoder=null,decoderKey=null,streamId=null,waitKey=true,recording=false,shown=0,inputFrames=0,latestFrame=-1;
let lastError='',lastErrorAt=0,work=Promise.resolve();
function log(text){const lines=$('log').textContent.split('\n');lines.push(`${new Date().toLocaleTimeString()}  ${text}`);$('log').textContent=lines.slice(-35).join('\n');$('log').scrollTop=$('log').scrollHeight;}
function fail(text){const now=Date.now();if(text!==lastError||now-lastErrorAt>4000){log(text);lastError=text;lastErrorAt=now;}}
function state(text,live=false){$('state').textContent=text;$('state').classList.toggle('live',live);}
async function call(fn){try{const r=await fn();if(!r.ok)throw new Error(r.error);return r.value;}catch(e){fail(e.message);throw e;}}
function closeDecoder(){if(decoder&&decoder.state!=='closed')decoder.close();decoder=null;decoderKey=null;waitKey=true;}
function draw(image,w,h){if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}ctx.drawImage(image,0,0,w,h);$('empty').hidden=true;shown++;document.body.dataset.displayedFrames=String(shown);$('decoded').textContent=`表示 ${shown}`;$('picture').textContent=`${w} × ${h} / frame ${latestFrame}`;}
function codecFromSps(b){
  for(let i=0;i+7<b.length;i++){
    let offset=0;
    if(b[i]===0&&b[i+1]===0&&b[i+2]===1)offset=i+3;
    else if(b[i]===0&&b[i+1]===0&&b[i+2]===0&&b[i+3]===1)offset=i+4;
    if(offset&&(b[offset]&31)===7&&offset+3<b.length)return 'avc1.'+[b[offset+1],b[offset+2],b[offset+3]].map(x=>x.toString(16).padStart(2,'0')).join('');
  }
  return null;
}
async function acceptFrame(f){
  try{
    inputFrames++;latestFrame=f.frame;
    if(streamId!==f.stream){closeDecoder();streamId=f.stream;}
    const bytes=new Uint8Array(f.data);
    if(f.codec===2){
      closeDecoder();const image=await createImageBitmap(new Blob([bytes],{type:'image/jpeg'}));
      try{draw(image,image.width,image.height);}finally{image.close();}
      $('codec-label').textContent='MJPEG / NRCV';$('decoder').textContent='内蔵 JPEG デコーダ';return;
    }
    if(f.codec!==1)throw new Error('対応していない codec です');
    if(!('VideoDecoder' in window))throw new Error('WebCodecs が利用できません。Electron の指定バージョンで起動してください。');
    if(waitKey&&!f.key)return;
    let codec=f.key?codecFromSps(bytes):decoderKey;
    if(!codec){waitKey=true;throw new Error('H.264 IDR に SPS がありません。Annex B + repeat-headers が必要です。');}
    if(!decoder||decoder.state==='closed'||codec!==decoderKey){
      closeDecoder();
      if(!f.key)return;
      const config={codec,optimizeForLatency:true};
      const support=await VideoDecoder.isConfigSupported(config);
      if(!support.supported)throw new Error(`H.264 ${codec} はこの配布物で非対応です。MJPEG bin で切り分けてください。`);
      decoder=new VideoDecoder({output:image=>{try{draw(image,image.displayWidth,image.displayHeight);}finally{image.close();}},error:e=>{waitKey=true;fail(`H.264 復号: ${e.message}`);}});
      decoder.configure(config);decoderKey=codec;
      $('codec-label').textContent=`H.264 ${codec} / NRCV`;$('decoder').textContent='内蔵 WebCodecs デコーダ';
    }
    if(decoder.decodeQueueSize>=4){closeDecoder();return;}
    decoder.decode(new EncodedVideoChunk({type:f.key?'key':'delta',timestamp:Number(f.pts)*1000,data:bytes}));
    waitKey=false;
  }catch(e){waitKey=true;fail(e.message);}
  finally{api.frameDone();}
}
api.on('frame',f=>{work=work.then(()=>acceptFrame(f)).catch(e=>{fail(e.message);api.frameDone();});});
api.on('gap',()=>{work=work.then(()=>{closeDecoder();}).catch(e=>fail(e.message));});
api.on('fault',text=>fail(text));
api.on('stats',s=>{
  $('rate').textContent=(s.goodputBps/1000).toFixed(1);$('frames').textContent=String(s.completed);
  $('loss').textContent=`${(s.lossEstimate*100).toFixed(1)}%`;
  $('age').textContent=s.lastRxMs===null?'最終受信 —':`最終受信 ${s.lastRxMs} ms 前 / 破棄 ${s.expired} / 不正 ${s.invalid}`;
  if(s.radio){$('rssi').textContent=`${s.radio.rssi} dBm`;$('radio').textContent=`MCS ${s.radio.txMcs}/${s.radio.rxMcs} · Duty ${s.radio.duty?'可':'待機'} · Q ${s.radio.queue}`;}
});
api.on('record-state',s=>{recording=s.recording;$('record').textContent=recording?'録画停止':'録画開始';});
$('listen').onclick=async()=>{try{await call(()=>api.listen({bind:$('bind').value.trim(),port:Number($('port').value),source:$('source').value.trim(),feedbackEnabled:$('feedback').checked}));closeDecoder();state('UDP 受信中',true);$('source-title').textContent=`${$('source').value} → UDP ${$('port').value}`;log('UDP 待受を開始しました。');}catch{state('開始失敗');}};
$('stop').onclick=async()=>{try{await call(()=>api.stop());closeDecoder();state('停止');log('映像入力を停止しました。');}catch{}};
$('directory').onclick=async()=>{try{const p=await call(()=>api.chooseDirectory());if(p){$('directory-label').textContent=p;$('record').disabled=false;}}catch{}};
$('record').onclick=async()=>{try{recording=await call(()=>api.record(!recording));$('record').textContent=recording?'録画停止':'録画開始';log(recording?'完全受信フレームの保存を開始します。':'保存を終了しました。');}catch{}};
async function openDemo(fn,title){try{closeDecoder();const c=await call(fn);if(c){state('ローカル再生',true);$('source-title').textContent=title;log(`${c.width}×${c.height} / ${c.frames} frames。無線は使用していません。`);}}catch{state('再生失敗');}}
$('sample').onclick=()=>openDemo(()=>api.sample($('demo-codec').value),'内蔵テストパターン / RF 未使用');
$('open').onclick=()=>openDemo(()=>api.openClip(),'HVC1 クリップ / RF 未使用');
window.addEventListener('beforeunload',()=>closeDecoder());
