'use strict';
function packets(data,{seq=0,frame=0,stream=1,key=true,codec=1,size=1400,pts=0}={}){
 const out=[];const cap=size-36,n=Math.ceil(data.length/cap);
 for(let i=0;i<n;i++){
  const part=data.subarray(i*cap,(i+1)*cap),b=Buffer.alloc(size);
  b.write('NRCV');b[4]=2;b[5]=codec;b[6]=(key?1:0)|(i===0?2:0)|(i===n-1?4:0)|(part.length<cap?8:0);b[7]=36;
  b.writeUInt32BE((seq+i)>>>0,8);b.writeUInt32BE(stream,12);b.writeUInt32BE(frame>>>0,16);b.writeUInt32BE(pts,20);
  b.writeUInt32BE(data.length,24);b.writeUInt16BE(i,28);b.writeUInt16BE(n,30);b.writeUInt16BE(part.length,32);b.writeUInt16BE(size,34);part.copy(b,36);out.push(b);
 }return out;
}
module.exports={packets};
