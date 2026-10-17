// Test the exact embedded page with an in-memory HTTP fixture; no badge/SD writes.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.BADGE_NODE_MODULES||'C:/Users/yoummo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules']}));
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'lib/badge_transfer/src/BadgeTransfer.cpp'),'utf8');
const html=source.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const writes=[],files={music:[{name:'Calm.wav',bytes:4096}],photos:[]};
let fail=false;
(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.BADGE_BROWSER||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.route('http://badge.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
   if(url.pathname==='/api/files')return route.fulfill({json:{...files,free:60000000000}});
   assert.equal(url.pathname,'/api/upload');assert.equal(req.headers()['x-badge-upload'],'1');
   if(fail)return route.fulfill({status:500,json:{error:'SD 卡写入失败'}});
   const kind=url.searchParams.get('kind'),original=url.searchParams.get('name');let name=original;
   if(files[kind].some(f=>f.name===name))name=name.replace(/(\.[^.]+)$/,' (1)$1');
   const body=req.postDataBuffer();writes.push({kind,name,body});files[kind].push({name,bytes:body.length});
   return route.fulfill({json:{name,bytes:body.length}});
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
  assert.equal(writes[3].name,'Flower.jpg');assert.equal(writes[3].body.readUInt16BE(0),0xffd8);
  const dimensions=await page.evaluate(async data=>{const im=await createImageBitmap(new Blob([new Uint8Array(data)],{type:'image/jpeg'}));return [im.width,im.height]},[...writes[3].body]);
  assert.deepEqual(dimensions,[1280,640]);assert(writes[4].body.equals(gif));
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
  assert(await page.getByRole('button',{name:'上传音乐'}).isEnabled());assert.deepEqual(errors,[]);
  console.log('PASS: unrestricted picker, opaque-MIME FLAC bytes/case, post-selection format validation, music Unicode/batch/collision response, static JPEG conversion/size, unchanged GIF, safe names and error recovery');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
