// Real launcher/input integration. Route screenshots still require gameplay review.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer';

const [game,out,url,routesFile]=process.argv.slice(2);
const catalogue=JSON.parse(fs.readFileSync(new URL('./games.json',import.meta.url)));
assert(catalogue.some(entry=>entry.id===game)&&out&&url&&routesFile,
    'Usage: node game-controls-e2e.mjs GAME NEW_OUTPUT URL ROUTES_JSON');
fs.mkdirSync(out);
const profile=fs.mkdtempSync(path.join(out,'browser-profile-'));
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,userDataDir:profile,
    args:['--no-sandbox','--disable-dev-shm-usage','--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1',
        '--use-gl=angle','--use-angle=vulkan','--enable-features=Vulkan,SyntheticPointerActions',
        '--enable-gpu','--ignore-gpu-blocklist','--disable-background-timer-throttling']});
const report={game,url,checks:[],errors:[],screenshots:[]},log=fs.createWriteStream(out+'/browser.log');
try {
    const page=await browser.newPage();await page.setViewport({width:900,height:800,hasTouch:true});
    page.setDefaultTimeout(180000);
    page.on('console',message=>{
        log.write(message.text()+'\n');
        if(/panicked with category:|KERN-EXEC|RuntimeError:|AOT-only:|Aborted\(/.test(message.text()))report.errors.push(message.text());
    });
    page.on('pageerror',error=>report.errors.push(String(error)));
    await page.evaluateOnNewDocument(()=>{window.ekaBenchmarkRouteUs=6000000;});
    await page.goto(new URL('?game='+game,url).href,{waitUntil:'domcontentloaded'});
    await page.waitForFunction(()=>window._gameRunning&&Module._eka2l1_route_phase()===1);
    assert.equal(await page.evaluate(()=>ekaTouchControls.profile),game,'Actual catalogue launch must choose its own profile');
    report.browser=await browser.version();
    report.gpu=(await (await browser.target().createCDPSession()).send('SystemInfo.getInfo')).gpu;
    const status=()=>page.evaluate(()=>({guest:Module._eka2l1_guest_time_us(),phase:Module._eka2l1_route_phase(),
        frames:Module._eka2l1_presentations(),inputs:Module._eka2l1_input_consumed(),
        canvas:[Module.canvas.width,Module.canvas.height],profile:ekaTouchControls.profile,
        storageKey:ekaTouchControls.storageKey,orientation:ekaTouchControls.orientation,
        held:[...new Set(_inputSources.values())].sort((a,b)=>a-b)}));
    const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    const step=async us=>{
        const s=await status();assert.equal(s.phase,1);
        assert.equal(await page.evaluate(target=>Module._eka2l1_route_step_to_64(target>>>0,Math.floor(target/4294967296)),s.guest+us),0);
        await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);
        assert.deepEqual(report.errors,[]);return status();
    };
    const press=async(scan,us=1500000,hold=200000)=>{
        await page.evaluate(scan=>Module._eka2l1_key_state(scan,1),scan);await step(hold);
        await page.evaluate(scan=>Module._eka2l1_key_state(scan,0),scan);return step(us);
    };
    const shot=async name=>{
        await page.screenshot({path:out+'/'+name+'.png'});
        const state=await status();report.screenshots.push({name,...state});
        fs.writeFileSync(out+'/report.json',JSON.stringify(report,null,2));return state;
    };
    const input=await page.createCDPSession();
    await input.send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});
    await page.evaluate(()=>{
        window.controlTouches=[];
        window.addEventListener('pointerdown',event=>controlTouches.push({trusted:event.isTrusted,type:event.pointerType}),true);
    });
    const touch=async(type,points)=>{
        await input.send('Input.dispatchTouchEvent',{type,touchPoints:points});
        await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    };
    const touchAction=async(index,scan,us=200000,steer=false)=>{
        await page.setViewport({width:390,height:844,hasTouch:true});await wait(150);
        const before=await status();
        const point=await page.$eval('[data-action="'+index+'"]',button=>{
            const r=button.getBoundingClientRect();if(r.width===0)throw Error('Hidden action');
            return {x:r.x+r.width/2,y:r.y+r.height/2,id:2};
        });
        const origin={x:80,y:300,id:1},moved={...origin,x:40};
        if(steer){await touch('touchStart',[origin]);await touch('touchMove',[moved]);}
        await touch(steer?'touchMove':'touchStart',steer?[moved,point]:[point]);
        assert.deepEqual((await status()).held,[...new Set(steer?[14,scan]:[scan])].sort((a,b)=>a-b));
        await step(us);await shot('action-'+report.checks.length+'-held');
        if(steer){await touch('touchMove',[moved]);assert.deepEqual((await status()).held,[14]);}
        await touch('touchEnd',[]);await step(100000);
        const after=await status();assert.deepEqual(after.held,[]);assert(after.inputs>before.inputs);
        assert(await page.evaluate(()=>controlTouches.some(event=>event.trusted&&event.type==='touch')));
        report.checks.push({action:index,scan,steer,inputs:after.inputs-before.inputs});
    };
    const layout=async()=>{
        for(const [width,height] of [[390,844],[844,390]]) {
            const before=await status();await page.setViewport({width,height,hasTouch:true});await wait(150);
            assert.deepEqual((await status()).canvas,before.canvas,'Viewport must not select guest orientation');
            for(const b of await page.$$eval('#action-group button',buttons=>buttons.filter(b=>b.offsetWidth).map(b=>{
                const r=b.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height};})))
                assert(b.x>=0&&b.y>=0&&b.x+b.w<=width+1&&b.y+b.h<=height+1);
            await shot('layout-'+width);
        }
    };
    for(const command of JSON.parse(fs.readFileSync(routesFile))[game]) {
        fs.appendFileSync(out+'/commands.log',command+'\n');await eval(command);
    }
    assert.deepEqual(report.errors,[]);assert.deepEqual((await status()).held,[]);
    report.complete=true;
} catch(error) {report.failure=String(error);throw error;}
finally {
    fs.writeFileSync(out+'/report.json',JSON.stringify(report,null,2));
    await browser.close();fs.rmSync(profile,{recursive:true,force:true});log.end();
}
