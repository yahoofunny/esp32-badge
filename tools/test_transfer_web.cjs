// Test the exact embedded page with an in-memory HTTP fixture; no badge/SD writes.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.BADGE_NODE_MODULES||'C:/Users/yoummo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules']}));
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'lib/badge_transfer/src/BadgeTransfer.cpp'),'utf8');
const html=source.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const writes=[],files={music:[{name:'Calm.wav',bytes:4096}],photos:[]};
let fail=false,holdUploads=false;const held=[];
(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.BADGE_BROWSER||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844},deviceScaleFactor:1});
  await page.addInitScript(()=>{window.uploadTimeouts=[];const send=XMLHttpRequest.prototype.send;
   XMLHttpRequest.prototype.send=function(body){window.uploadTimeouts.push(this.timeout);return send.call(this,body)};});
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.route('http://badge.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
   if(url.pathname==='/api/files')return route.fulfill({json:{...files,free:60000000000}});
   assert.equal(url.pathname,'/api/upload');assert.equal(req.headers()['x-badge-upload'],'1');
   if(holdUploads){held.push(route);return;}
   if(fail)return route.fulfill({status:500,json:{error:'SD 卡写入失败'}});
   const kind=url.searchParams.get('kind'),original=url.searchParams.get('name');let name=original;
   if(files[kind].some(f=>f.name===name))name=name.replace(/(\.[^.]+)$/,' (1)$1');
   const body=req.postDataBuffer();writes.push({kind,name,body});files[kind].push({name,bytes:body.length});
   const result={name,bytes:body.length};
   if(body.length>=26&&body.subarray(0,4).toString()==='fLaC'&&(body[4]&127)===0&&body[7]===34){
    const bits=((body[20]&1)<<4)+(body[21]>>4)+1,channels=((body[20]>>1)&7)+1;
    result.audio={rate:(body[18]<<12)|(body[19]<<4)|(body[20]>>4),bits,channels,supported:(bits===8||bits===16)&&channels<=2};
   }
   return route.fulfill({json:result});
  });
  await page.goto('http://badge.test/');
  await page.waitForFunction(()=>document.querySelector('#files').textContent.includes('Calm.wav'));
  // Some phone document providers cannot classify FLAC for extension/MIME filters.
  // Keep the native picker unrestricted; validate supported extensions afterwards.
  assert.equal(await page.locator('#music').getAttribute('accept'),null);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#result').textContent==='请先选择文件');
  const audio=Buffer.from('RIFF'+'.'.repeat(4096));
  const flac=Buffer.from('fLaC'+'flac-fixture'.repeat(100));
  await page.locator('#music').setInputFiles([
   {name:'Calm.wav',mimeType:'audio/wav',buffer:audio},
   {name:'花朵.mp3',mimeType:'audio/mpeg',buffer:Buffer.from('ID3fixture')},
   {name:'Night.FLAC',mimeType:'application/octet-stream',buffer:flac}
  ]);
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#result').textContent.includes('已完成 3 个文件。关闭'));
  assert.equal(writes[0].name,'Calm (1).wav');assert(writes[0].body.equals(audio));
  assert.equal(writes[1].name,'花朵.mp3');
  assert.equal(writes[2].name,'Night.FLAC');assert(writes[2].body.equals(flac));
  // Real browser File larger than the previous cap, not a forged size field.
  await page.evaluate(()=>{const data=new Uint8Array(51*1048576);data.set([102,76,97,67,128,0,0,34]);
   data[10]=16;data[18]=10;data[19]=196;data[20]=66;data[21]=240;data[data.length-1]=90;
   const selection=new DataTransfer();selection.items.add(new File([data],'Large.FLAC',{type:'application/octet-stream'}));
   const input=document.querySelector('#music');input.files=selection.files;input.dispatchEvent(new Event('change'));});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#music-feedback').textContent.includes('已完成 1 个文件。关闭'));
  assert.equal(writes[3].body.length,51*1048576);assert.equal(writes[3].body.at(-1),90);
  assert.equal(writes[3].body.subarray(0,4).toString(),'fLaC');
  assert(await page.evaluate(()=>uploadTimeouts.every(t=>t===0)));
  assert((await page.evaluate(()=>{try{checkFile('music',{name:'TooBig.flac',size:512*1048576+1})}catch(e){return e.message}})).includes('512 MB'));
  assert((await page.evaluate(()=>{try{checkFile('photos',{name:'TooBig.gif',size:50*1048576+1})}catch(e){return e.message}})).includes('50 MB'));
  const png=await page.evaluate(async()=>{const c=document.createElement('canvas');c.width=2000;c.height=1000;
   const g=c.getContext('2d');g.fillStyle='#e68d65';g.fillRect(0,0,2000,1000);
   return [...new Uint8Array(await (await new Promise(r=>c.toBlob(r,'image/png'))).arrayBuffer())];});
  const gifPath=path.join(root,'assets/clawd-gifs',fs.readdirSync(path.join(root,'assets/clawd-gifs')).find(n=>n.endsWith('.gif')));
  const gif=fs.readFileSync(gifPath);
  await page.locator('#photos').setInputFiles([
   {name:'Flower.png',mimeType:'image/png',buffer:Buffer.from(png)},
   {name:'Clawd.gif',mimeType:'image/gif',buffer:gif}
  ]);
  await page.getByRole('button',{name:'上传照片'}).click();
  await page.waitForFunction(()=>document.querySelector('#result').textContent.includes('已完成 2 个文件。关闭'));
  assert.equal(writes[4].name,'Flower.jpg');assert.equal(writes[4].body.readUInt16BE(0),0xffd8);
  const dimensions=await page.evaluate(async data=>{const im=await createImageBitmap(new Blob([new Uint8Array(data)],{type:'image/jpeg'}));return [im.width,im.height]},[...writes[4].body]);
  assert.deepEqual(dimensions,[1280,640]);assert(writes[5].body.equals(gif));
  await page.screenshot({path:path.join(root,'docs/images/wifi-transfer-web.png'),fullPage:true});
  // Existing SD names must be treated as text, even when supplied outside this UI.
  files.music.push({name:'<img src=x onerror=alert(1)>.mp3',bytes:10});
  await page.getByRole('button',{name:'刷新',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#files').textContent.includes('<img'));
  assert.equal(await page.locator('#files img').count(),0);
  const countBefore=writes.length;
  await page.locator('#music').setInputFiles({name:'Unsupported.zip',mimeType:'application/zip',buffer:audio});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#result').textContent.includes('不是支持的音乐格式'));
  assert.equal(writes.length,countBefore);
  fail=true;
  await page.locator('#music').setInputFiles({name:'Failure.wav',mimeType:'audio/wav',buffer:audio});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#result').textContent.includes('SD 卡写入失败'));
  assert(await page.getByRole('button',{name:'上传音乐'}).isEnabled());
  const box=await page.locator('#music-feedback').boundingBox();assert(box.y>=0&&box.y+box.height<=844);
  fail=false;holdUploads=true;
  await page.locator('#music').setInputFiles({name:'Cancel.flac',mimeType:'audio/flac',buffer:flac});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>!document.querySelector('[data-cancel="music"]').hidden);
  await page.locator('[data-cancel="music"]').click();
  await page.waitForFunction(()=>document.querySelector('#music-feedback').textContent.includes('已取消上传'));
  assert(await page.getByRole('button',{name:'上传音乐'}).isEnabled());
  for(const route of held.splice(0))await route.abort().catch(()=>{});
  await page.clock.install();
  await page.locator('#music').setInputFiles({name:'Stalled.flac',mimeType:'audio/flac',buffer:flac});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>!document.querySelector('[data-cancel="music"]').hidden);
  await page.clock.fastForward(47000);
  await page.waitForFunction(()=>document.querySelector('#music-feedback').textContent.includes('45 秒没有传输进度'));
  for(const route of held.splice(0))await route.abort().catch(()=>{});
  holdUploads=false;
  const hiRes=Buffer.alloc(42);hiRes.set([102,76,97,67,128,0,0,34]);hiRes[10]=16;hiRes[18]=10;hiRes[19]=196;hiRes[20]=67;hiRes[21]=112;
  await page.locator('#music').setInputFiles({name:'HiRes.flac',mimeType:'audio/flac',buffer:hiRes});
  await page.getByRole('button',{name:'上传音乐'}).click();
  await page.waitForFunction(()=>document.querySelector('#music-feedback').textContent.includes('不支持此 FLAC（24 位'));
  assert.deepEqual(errors,[]);
  console.log('PASS: actual 51 MiB browser upload, 512 MiB music/50 MiB photo limits, inline errors, cancel/stall recovery, no total timeout, FLAC compatibility warning, JPEG/GIF preservation and picker regression');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
