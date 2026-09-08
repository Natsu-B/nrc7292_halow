'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('halow',{
  listen:cfg=>ipcRenderer.invoke('listen',cfg),stop:()=>ipcRenderer.invoke('stop'),
  chooseDirectory:()=>ipcRenderer.invoke('choose-directory'),record:enabled=>ipcRenderer.invoke('record',enabled),
  sample:codec=>ipcRenderer.invoke('sample',codec),openClip:()=>ipcRenderer.invoke('open-clip'),
  frameDone:()=>ipcRenderer.send('frame-done'),
  on:(kind,cb)=>{if(!['frame','gap','stats','fault','record-state'].includes(kind))throw new Error('Invalid event');ipcRenderer.on(kind,(_event,data)=>cb(data));}
});
