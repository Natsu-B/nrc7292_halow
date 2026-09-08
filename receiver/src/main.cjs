'use strict';
const {app,BrowserWindow,ipcMain,dialog,protocol,net,session}=require('electron');
const fs=require('node:fs');const path=require('node:path');const {pathToFileURL}=require('node:url');
const {UdpReceiver}=require('./udp.cjs');const {readClip}=require('./protocol.cjs');
protocol.registerSchemesAsPrivileged([{scheme:'halow',privileges:{standard:true,secure:true,supportFetchAPI:true}}]);
let win,receiver=new UdpReceiver(),sampleTimer=null,outputDirectory=null,recording=false,recordStream=null,recordKey=null,recordWaitKey=true;
let uiBusy=false,uiSentAt=0,displayDrops=0;
function send(kind,data){if(win&&!win.isDestroyed())win.webContents.send(kind,data);}
function stopRecord(){recording=false;if(recordStream)recordStream.end();recordStream=null;recordKey=null;send('record-state',{recording:false,directory:outputDirectory});}
function recordFrame(f){
  if(!recording)return;
  const key=`${f.stream}-${f.codec}`;
  if(recordKey!==key){if(recordStream)recordStream.end();recordStream=null;recordKey=key;recordWaitKey=true;}
  if(f.codec===1&&recordWaitKey&&!f.key)return;
  recordWaitKey=false;
  if(!recordStream){
    const name=`halow-${Date.now()}-${f.stream}.${f.codec===1?'h264':'mjpg'}`;
    recordStream=fs.createWriteStream(path.join(outputDirectory,name),{flags:'wx',highWaterMark:1024*1024});
    recordStream.on('error',e=>{send('fault',`Recording stopped: ${e.message}`);stopRecord();});
  }
  if(recordStream.writableLength>4*1024*1024){send('fault','Recording disk is too slow; stopped to bound memory.');stopRecord();return;}
  recordStream.write(f.data);
}
function frame(f){
  recordFrame(f);
  // Bounded IPC: a slow renderer cannot accumulate seconds of video in the main process.
  if(uiBusy&&Date.now()-uiSentAt<2000){displayDrops++;send('gap',null);return;}
  uiBusy=true;uiSentAt=Date.now();send('frame',{...f,data:Uint8Array.from(f.data)});
}
receiver.on('frame',frame);receiver.on('stats',s=>send('stats',{...s,displayDrops}));receiver.on('fault',s=>send('fault',s));
function stopSample(){if(sampleTimer)clearTimeout(sampleTimer);sampleTimer=null;}
function stopAll(){stopSample();receiver.stop();uiBusy=false;}
function startClip(p){
  const stat=fs.statSync(p);if(stat.size>32*1024*1024)throw new Error('Clip must be 32 MiB or smaller');
  const clip=readClip(fs.readFileSync(p));stopAll();let idx=0,epoch=Date.now(),loop=0;const stream=(Date.now()&0xffffffff)>>>0;
  const tick=()=>{const f=clip.frames[idx];frame({...f,stream,frame:loop*clip.frames.length+idx,pts:loop*clip.duration+f.pts});idx++;
    if(idx===clip.frames.length){idx=0;loop++;}
    const due=epoch+loop*clip.duration+clip.frames[idx].pts;
    sampleTimer=setTimeout(tick,Math.max(0,due-Date.now()));};tick();
  return {codec:clip.codec,width:clip.width,height:clip.height,frames:clip.frames.length};
}
function allowed(event){return win && event.sender===win.webContents && event.senderFrame?.url.startsWith('halow://app/');}
function handle(name,fn){ipcMain.handle(name,async(e,...args)=>{if(!allowed(e))throw new Error('Unauthorized IPC');try{return {ok:true,value:await fn(...args)};}catch(error){return {ok:false,error:error.message};}});}
app.whenReady().then(()=>{
  const ui=path.join(__dirname,'../ui');const permitted=new Set(['index.html','style.css','renderer.js']);
  protocol.handle('halow',request=>{const u=new URL(request.url);const f=u.pathname==='/'?'index.html':u.pathname.slice(1);if(u.host!=='app'||!permitted.has(f))return new Response('Not found',{status:404});return net.fetch(pathToFileURL(path.join(ui,f)).href);});
  session.defaultSession.setPermissionRequestHandler((_w,_p,done)=>done(false));
  win=new BrowserWindow({width:1260,height:800,minWidth:900,minHeight:650,title:'HaLow Receiver',backgroundColor:'#101721',webPreferences:{preload:path.join(__dirname,'preload.cjs'),nodeIntegration:false,contextIsolation:true,sandbox:true}});
  win.removeMenu();win.webContents.setWindowOpenHandler(()=>({action:'deny'}));win.webContents.on('will-navigate',e=>e.preventDefault());
  handle('listen',async cfg=>{stopAll();displayDrops=0;await receiver.start(cfg);return cfg;});
  handle('stop',()=>{stopAll();return null;});
  handle('choose-directory',async()=>{const r=await dialog.showOpenDialog(win,{properties:['openDirectory','createDirectory']});if(!r.canceled)outputDirectory=r.filePaths[0];return outputDirectory;});
  handle('record',enabled=>{if(typeof enabled!=='boolean')throw new Error('Invalid argument');if(!enabled){stopRecord();return false;}if(!outputDirectory)throw new Error('Choose a recording directory first');recording=true;recordWaitKey=true;return true;});
  handle('sample',async(codec='h264')=>{
    if(!['h264','mjpeg'].includes(codec))throw new Error('Invalid sample codec');
    const p=app.isPackaged?path.join(process.resourcesPath,`sample_${codec}.bin`):path.join(__dirname,`../../assets/test_${codec}.bin`);
    // Development tree: receiver/src -> package root/assets.
    return startClip(p);
  });
  handle('open-clip',async()=>{const r=await dialog.showOpenDialog(win,{properties:['openFile'],filters:[{name:'HVC1 clip',extensions:['bin']}]});if(r.canceled)return null;return startClip(r.filePaths[0]);});
  ipcMain.on('frame-done',e=>{if(allowed(e))uiBusy=false;});
  win.loadURL('halow://app/');win.on('closed',()=>{stopAll();stopRecord();win=null;});
});
app.on('window-all-closed',()=>app.quit());
