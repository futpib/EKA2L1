// Generated-region throughput only: excludes EKA2L1's outer code-cache validator.
async function createCore(){
 const memory=new WebAssembly.Memory({initial:256,maximum:32768,shared:true});
 const words=new Uint32Array(memory.buffer),bytes=new Uint8Array(memory.buffer);
 const state=1024,tlb=4096,ram=65536;
 const imports={env:{memory}};
 for(const name of ['tlb_read32','tlb_write32','tlb_read8','tlb_write8','tlb_read16','tlb_write16'])imports.env[name]=()=>{throw Error('unexpected memory helper '+name);};
 const modules=[];
 for(let i=0;i<4;i++)modules.push((await WebAssembly.instantiate(await (await fetch('/eka'+i+'.wasm')).arrayBuffer(),imports)).instance);
 let kind=0,iterations=0;
 return {
  _setup(k,n){
   kind=k;iterations=n;bytes.fill(0);words[state/4]=0x12345678;words[state/4+1]=n;words[state/4+2]=65536;words[state/4+3]=0x9abcdef0;
   words[(state+60)/4]=4096;words[(state+784)/4]=0xd3;words[(state+796)/4]=0x13;words[(state+876)/4]=1;
   words[(state+852)/4]=tlb;words[(state+856)/4]=4096;words[(state+860)/4]=4096+(k===2?48:40);
   for(let p=1;p<64;p++){words[tlb/4+p*4]=p*4096;words[tlb/4+p*4+1]=p*4096;words[tlb/4+p*4+2]=p*4096;words[tlb/4+p*4+3]=ram+p*4096;}
   for(let i=0;i<(k===3?16384:256);i++)words[(ram+65536)/4+i]=k===3?((i*109+1021)&16383)*4:(Math.imul(i,2654435761)+17)>>>0;
  },
  _run(){words[(state+848)/4]=iterations*(kind===2?10:8)+2;const count=modules[kind].exports.run(state);if(count!==iterations*(kind===2?10:8)+2)throw Error('unexpected execution count '+count);return words[state/4+10]===1;},
  _result(i){if(i!==16)return words[state/4+i];let h=2166136261;for(let j=0;j<(kind===3?16384:256);j++)h=Math.imul(h^words[(ram+65536)/4+j],16777619)>>>0;return h;}
 };
}
