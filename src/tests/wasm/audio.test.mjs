// Exercise the actual worklet source, including every output sample and queue edge.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
let Processor;
const context = vm.createContext({Int16Array, ArrayBuffer,
  AudioWorkletProcessor: class {constructor(){this.port={postMessage(){}};}},
  registerProcessor(name, type){assert.equal(name,'eka-audio');Processor=type;}});
vm.runInContext(fs.readFileSync(new URL('../../emu/wasm/audio-worklet.js',import.meta.url),'utf8'),context);
const p = new Processor({processorOptions:{prebufferFrames:128}});
const send = pcm => p.port.onmessage({data:{pcm:pcm.buffer}});
const render = () => {const out=[new Float32Array(128),new Float32Array(128)];assert.equal(p.process([], [out]),true);return out;};
assert.ok(render().every(c=>c.every(v=>v===0)));
const input=new Int16Array(1024);
for(let i=0;i<input.length;++i)input[i]=(i*117-32768)&65535;
send(input);
for(let block=0;block<4;++block){const out=render();for(let i=0;i<128;++i)for(let c=0;c<2;++c)assert.equal(out[c][i],input[(block*128+i)*2+c]/32768);}
assert.equal(p.played,512);assert.equal(p.underruns,0);
render();render();assert.equal(p.underruns,1);assert.equal(p.count,0);
send(new Int16Array(126));assert.ok(render().every(c=>c.every(v=>v===0)));assert.equal(p.count,63);
send(new Int16Array(130));render();assert.equal(p.count,0);
const large=new Int16Array((p.capacity+256)*2);for(let i=0;i<large.length;++i)large[i]=i;
send(large);assert.equal(p.count,p.target);assert.equal(p.dropped,p.capacity+256-p.target);assert.equal(p.resyncs,1);
const out=render();for(let i=0;i<128;++i)for(let c=0;c<2;++c)assert.equal(out[c][i],large[(i+p.capacity+256-p.target)*2+c]/32768);
p.port.onmessage({data:{reset:true}});assert.equal(p.count,0);assert.ok(render().every(c=>c.every(v=>v===0)));
console.log('PASS: exact stereo samples, prebuffer, underflow/recovery, overflow, reset');
