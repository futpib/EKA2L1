// Play a captured Snakes PCM segment through the production worklet into a
// process-selected audio device. Pair with the native eka_audio_probe WAV mode.
import fs from 'node:fs';
import http from 'node:http';
import puppeteer from 'puppeteer';
const [wavPath, reportPath] = process.argv.slice(2);
const wav = fs.readFileSync(wavPath);
const source = fs.readFileSync(new URL('../../emu/wasm/audio-worklet.js', import.meta.url));
const server = http.createServer((req,res) => {
  res.setHeader('Content-Type', req.url === '/worklet.js' ? 'text/javascript' : 'text/html');
  res.end(req.url === '/worklet.js' ? source : '<button id="play">Play</button>');
}).listen(0,'127.0.0.1');
await new Promise(resolve=>server.once('listening',resolve));
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,
  ignoreDefaultArgs:['--mute-audio'],args:['--no-sandbox','--disable-background-timer-throttling']});
try {
  const page=await browser.newPage();
  const errors:string[]=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(`http://127.0.0.1:${(server.address() as any).port}`);
  await page.evaluate((bytes)=>{
    const g=window as any;
    const pcm=new Int16Array(new Uint8Array(bytes).buffer);
    document.getElementById('play')!.onclick=async()=>{
      const c=g.context=new AudioContext({sampleRate:48000});await c.resume();
      await c.audioWorklet.addModule('/worklet.js');
      const n=new AudioWorkletNode(c,'eka-audio',{numberOfInputs:0,numberOfOutputs:1,outputChannelCount:[2]});
      n.connect(c.destination);n.port.onmessage=e=>g.stats=e.data;
      let at=0;
      const send=()=>{const block=pcm.slice(at,at+960);at+=block.length;n.port.postMessage({pcm:block.buffer},[block.buffer]);};
      for(let i=0;i<12;++i)send();
      const timer=setInterval(()=>{
        if(at<pcm.length)send();else{clearInterval(timer);setTimeout(()=>{g.done=true;},500);}
      },10);
    };
  },[...wav.subarray(44)]);
  await page.click('#play');
  await page.waitForFunction(()=>(window as any).done,{timeout:60000});
  const report=await page.evaluate(()=>({stats:(window as any).stats,state:(window as any).context.state,rate:(window as any).context.sampleRate}));
  if(errors.length)throw Error(errors.join('\n'));
  if(!report.stats.nonzero)throw Error('No audio consumed');
  fs.writeFileSync(reportPath,JSON.stringify(report,null,2));
} finally {await browser.close();server.close();}
