#!/usr/bin/env python3
"""Development-only headless Chromium test of the REAL receiver renderer.
Requires Python playwright and a Chromium executable; neither is a receiver runtime dependency.
This does not test Electron packaging or a physical HaLow link.
"""
import argparse, base64, functools, http.server, json, pathlib, struct, threading, time
from playwright.sync_api import sync_playwright
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--chromium',default='/usr/bin/chromium')
p.add_argument('--memory-ui',action='store_true',help='No URL navigation: inject local UI, test MJPEG only (about:blank lacks WebCodecs secure context)')
p.add_argument('--screenshot',type=pathlib.Path,default=ROOT/'validation/receiver-preview.png')
a=p.parse_args()
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args): pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=ROOT/'receiver/ui'))
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 with sync_playwright() as pw:
  browser=pw.chromium.launch(executable_path=a.chromium,headless=True,args=['--no-sandbox'])
  page=browser.new_page(viewport={'width':1260,'height':850},device_scale_factor=1)
  errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  bootstrap='''window.__handlers={};window.__done=0;window.halow={
    on:(kind,cb)=>window.__handlers[kind]=cb,frameDone:()=>window.__done++,
    listen:async()=>({ok:true}),stop:async()=>({ok:true}),
    sample:async c=>({ok:true,value:c==='mjpeg'?{width:320,height:180,frames:12}:{width:640,height:360,frames:120}}),
    openClip:async()=>({ok:true,value:null}),record:async b=>({ok:true,value:b}),
    chooseDirectory:async()=>({ok:true,value:'/test/output'})};'''
  page.add_init_script(bootstrap)
  if a.memory_ui:
   # No browser network/file URL access. Read our own generated assets in Python.
   html=(ROOT/'receiver/ui/index.html').read_text()
   import re
   html=re.sub(r'<meta http-equiv="Content-Security-Policy"[^>]*>','',html)
   html=html.replace('<link rel="stylesheet" href="style.css">','').replace('<script src="renderer.js"></script>','')
   page.set_content(html);page.evaluate(bootstrap)
   page.add_style_tag(content=(ROOT/'receiver/ui/style.css').read_text())
   page.add_script_tag(content=(ROOT/'receiver/ui/renderer.js').read_text())
  else: page.goto(f'http://127.0.0.1:{server.server_port}/index.html')
  if a.memory_ui: page.select_option('#demo-codec','mjpeg')
  page.click('#sample')
  totals=[];expected_done=0
  names=['test_mjpeg.bin'] if a.memory_ui else ['test_h264.bin','test_mjpeg.bin']
  for stream,name in enumerate(names,1):
   b=(ROOT/'assets'/name).read_bytes();codec=b[8];count=struct.unpack_from('<I',b,20)[0];at=32
   before=int(page.locator('body').get_attribute('data-displayed-frames') or '0')
   for i in range(count):
    n,pts,flags,crc=struct.unpack_from('<IIII',b,at);at+=16;data=b[at:at+n];at+=n
    page.evaluate('''f=>{f.data=Uint8Array.from(atob(f.data),c=>c.charCodeAt(0));window.__handlers.frame(f);}''',
        {'codec':codec,'key':bool(flags&1),'stream':stream,'frame':i,'pts':pts,'data':base64.b64encode(data).decode()})
    expected_done+=1;page.wait_for_function('n=>window.__done>=n',arg=expected_done,timeout=5000)
    page.wait_for_timeout(12)
   if codec==1: page.evaluate('async()=>{if(decoder&&decoder.state==="configured")await decoder.flush();}')
   page.wait_for_timeout(100)
   after=int(page.locator('body').get_attribute('data-displayed-frames') or '0')
   assert after-before==count,(name,after-before,count,page.locator('#log').text_content())
   totals.append({'clip':name,'displayed':after-before,'codec':codec})
   if codec==1 or a.memory_ui:
    page.evaluate('s=>window.__handlers.stats(s)',{'goodputBps':0,'completed':count,'lossEstimate':0,'expired':0,'invalid':0,'lastRxMs':None,'radio':None})
    a.screenshot.parent.mkdir(parents=True,exist_ok=True);page.screenshot(path=str(a.screenshot),full_page=True)
  assert not errors,errors
  print(json.dumps({'status':'PASS','browser':browser.version,'tests':totals,'page_errors':errors,
      'scope':'Real renderer, simulated IPC; no Electron main/packaging or RF', 'webcodecs_tested':not a.memory_ui},indent=2))
  browser.close()
finally:server.shutdown()
