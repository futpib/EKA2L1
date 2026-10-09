// Browser input contract checks. Real game integration lives in game-picker.ts.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import puppeteer from 'puppeteer';

const temp=fs.mkdtempSync(path.join(os.tmpdir(),'eka-touch-'));
const source=new URL('../../emu/wasm/',import.meta.url);
for (const file of ['audio.js','touch-controls.js','touch-controls.css']) fs.copyFileSync(new URL(file,source),path.join(temp,file));
const setup=`<script>
window.events=[];Module._eka2l1_key_state=(scan,down)=>events.push([scan,down]);_gameRunning=true;
document.getElementById('play-surface').classList.add('playing');
for(const id of ['touch-deck','game-controls','control-hint'])document.getElementById(id).hidden=false;
ekaTouchControls.startGame('');
</script>`;
fs.writeFileSync(path.join(temp,'eka2l1.html'),fs.readFileSync(new URL('shell.html',source),'utf8').replace('{{{ SCRIPT }}}',setup));
process.env.EKA2L1_WASM_BUILD_DIR=temp;
const {startServer}=await import('./server.ts');
const {server,port}=await startServer(0);
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
try {
    const page=await browser.newPage(), errors:string[]=[];
    page.on('pageerror',e=>errors.push(String(e)));
    await page.setViewport({width:390,height:844,hasTouch:true});
    const url=`http://127.0.0.1:${port}`;
    await page.goto(url);await page.waitForFunction(()=>Boolean((window as any).ekaTouchControls));
    const cdp=await page.createCDPSession();
    const rect=async(selector:string)=>page.$eval(selector,e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};});
    const point=(r:any,x=.5,y=.5,id=1)=>({x:r.x+r.width*x,y:r.y+r.height*y,id});
    const touch=async(type:string,points:any[])=>{
        await cdp.send('Input.dispatchTouchEvent',{type,touchPoints:points} as any);
        await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    };
    const down=()=>page.evaluate(()=>[...new Set((window as any)._inputSources.values())].sort((a:any,b:any)=>a-b));
    const prefs=()=>page.evaluate(()=>(window as any).ekaTouchControls.prefs);
    const menu=async()=>{if(!await page.$eval('#session-panel',e=>(e as HTMLDialogElement).open))await page.click('#btn-game-menu');};
    const controls=async()=>{await menu();await page.click('#btn-touch-settings');};
    const configure=(name:string)=>page.evaluate(n=>(window as any).ekaTouchControls.configure(n),name);
    const settings=async(key:string,value:any)=>page.evaluate(({key,value})=>{
        const el=document.querySelector(`[data-pref="${key}"]`) as HTMLInputElement;
        if(el.type==='checkbox')el.checked=value;else el.value=String(value);
        el.dispatchEvent(new Event('input',{bubbles:true}));
    },{key,value});
    assert.equal((await prefs()).mode,'stick');assert.equal((await prefs()).directions,8);
    assert.equal(await page.$eval('#session-panel',e=>(e as HTMLDialogElement).open),false);
    assert.deepEqual(await rect('#canvas'),{x:0,y:0,width:390,height:844});
    await settings('mode','pad');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    const css=await page.$eval('link[rel=stylesheet]',e=>(e as HTMLLinkElement).href);
    assert.match(css,/touch-controls.css\?v=[a-f0-9]{64}/);
    assert.equal((await fetch(css)).headers.get('content-type'),'text/css');
    let pad=await rect('#movement-pad'), action=await rect('#primary-action');
    await touch('touchStart',[point(pad,.85,.5)]);assert.deepEqual(await down(),[15]);
    await touch('touchMove',[point(pad,.15,.5)]);assert.deepEqual(await down(),[14]);
    // Sliding retains contact; adding/releasing an action finger preserves movement.
    await touch('touchStart',[point(pad,.15,.5),point(action,.5,.5,2)]);assert.deepEqual(await down(),[14,167]);
    await touch('touchEnd',[point(action,.5,.5,2)]);assert.deepEqual(await down(),[14]);
    await touch('touchCancel',[]);assert.deepEqual(await down(),[]);
    await page.focus('#canvas');await page.keyboard.down('ArrowLeft');
    await touch('touchStart',[point(pad,.15,.5)]);await touch('touchEnd',[]);
    assert.deepEqual(await down(),[14]);await page.keyboard.up('ArrowLeft');assert.deepEqual(await down(),[]);
    await configure('snakes');assert.equal((await prefs()).directions,4);
    await touch('touchStart',[point(pad,.9,.1)]);assert.equal((await down()).length,1);await touch('touchEnd',[]);
    await configure('sky-force');assert.equal((await prefs()).mode,'stick');
    pad=await rect('#movement-pad');let origin=point(pad);
    await touch('touchStart',[origin]);assert.deepEqual(await down(),[]);
    await touch('touchMove',[{...origin,x:origin.x+40,y:origin.y-40}]);assert.deepEqual(await down(),[15,16]);
    await touch('touchMove',[{...origin,x:origin.x+2,y:origin.y-2}]);assert.deepEqual(await down(),[]);
    await touch('touchEnd',[]);
    // Sky Force accepts a movement gesture away from the old thumb-pad area.
    origin={x:320,y:200,id:1};
    await touch('touchStart',[origin]);await touch('touchMove',[{...origin,x:280,y:160}]);
    assert.deepEqual(await down(),[14,16]);await touch('touchEnd',[]);
    const actionX=(await rect('#primary-action')).x;
    await settings('leftHanded',true);assert.ok((await rect('#primary-action')).x<actionX);
    await settings('size',180);await settings('opacity',.55);await settings('mode','pad');
    await settings('latch',true);await settings('action',141);
    action=await rect('#primary-action');await touch('touchStart',[point(action)]);await touch('touchEnd',[]);
    assert.deepEqual(await down(),[141]);
    await controls();assert.deepEqual(await down(),[]);
    await page.click('#touch-settings [data-close]');
    await touch('touchStart',[point(action)]);await touch('touchCancel',[]);assert.deepEqual(await down(),[]);
    await configure('snakes');assert.equal((await prefs()).size,132);
    await configure('sky-force');assert.equal((await prefs()).size,180);assert.equal((await prefs()).leftHanded,true);
    await page.reload();await configure('sky-force');assert.equal((await prefs()).size,180);assert.deepEqual(await down(),[]);
    // Editing moves controls without sending input, including when a child button is grabbed.
    await controls();await page.click('#edit-touch-layout');
    const before=await prefs();action=await rect('#primary-action');
    await touch('touchStart',[point(action)]);await touch('touchMove',[point(action,.1,.1)]);await touch('touchEnd',[]);
    assert.deepEqual(await down(),[]);assert.notDeepEqual((await prefs()).buttons,before.buttons);
    await page.click('#btn-game-menu');
    await menu();await page.click('#btn-keypad');
    const five=await rect('#phone-keypad [data-scan="141"]');
    await touch('touchStart',[point(five)]);assert.deepEqual(await down(),[141]);
    await touch('touchCancel',[]);assert.deepEqual(await down(),[]);
    await page.click('#phone-keypad [data-close]');
    await controls();await page.click('#reset-touch-layout');await page.click('#touch-settings [data-close]');
    assert.equal((await prefs()).size,132);assert.equal((await prefs()).mode,'stick');
    await configure('');await controls();await settings('size',190);await settings('gap',40);await page.click('#touch-settings [data-close]');
    for (const [width,height] of [[320,568],[390,844],[568,320],[667,375],[844,390],[1024,768]]) {
        await page.setViewport({width,height,hasTouch:true});
        await new Promise(resolve=>setTimeout(resolve,100));
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
        for (const selector of ['#movement-pad','#primary-action','#canvas']) {
            const r=await rect(selector);assert.ok(r.width>0&&r.height>0&&r.x>=0&&r.y>=0&&r.x+r.width<=width+1&&r.y+r.height<=height+1,`${selector} ${width}x${height}: ${JSON.stringify(r)}`);
        }
    }
    await page.setViewport({width:390,height:844,hasTouch:true});
    await new Promise(resolve=>setTimeout(resolve,100));pad=await rect('#movement-pad');
    await touch('touchStart',[point(pad,.15,.5)]);await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
    assert.deepEqual(await down(),[]);await touch('touchEnd',[]);
    // Malformed persisted values and unavailable storage must not break input.
    await page.evaluate(()=>localStorage.setItem('eka-touch-v1:generic','{"size":1e99,"mode":"bad","move":{"x":-9,"y":9}}'));
    await configure('');assert.equal((await prefs()).size,190);assert.equal((await prefs()).mode,'stick');
    await page.evaluate(()=>{Storage.prototype.setItem=()=>{throw Error('storage blocked');};});await settings('size',150);
    assert.equal((await prefs()).size,150);assert.deepEqual(errors,[]);
    console.log('PASS generic and game presets, sliding, multi-touch, keyboard ownership, cancellation, hold release, per-game persistence, editing, keypad, CSS caching and responsive bounds');
} finally {await browser.close();await new Promise<void>(r=>server.close(()=>r()));fs.rmSync(temp,{recursive:true,force:true});}
