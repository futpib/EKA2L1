import fs from 'node:fs';
import assert from 'node:assert/strict';

// Match the linear-memory mapping with a live marker, rather than guessing
// from anonymous mapping size. Linux pagemap exposes residency without PFNs.
export async function allocationResidency(page, processes, capacity, allocations) {
 const renderer=processes.filter(p=>p.type==='renderer').sort((a,b)=>b.pssKiB-a.pssKiB)[0];
 const smaps=fs.readFileSync(`/proc/${renderer.id}/smaps`,'utf8');
 const candidates=[...smaps.matchAll(/^([0-9a-f]+)-([0-9a-f]+)\s+rw-p\s+\S+\s+\S+\s+\d+\s*(.*)$/gm)]
  .map(m=>({begin:Number.parseInt(m[1],16),end:Number.parseInt(m[2],16),name:m[3]}))
  .filter(m=>m.end-m.begin===capacity);
 const pointer=await page.evaluate(()=>{
  const p=Module._malloc(4096);if(!p)throw Error('Residency marker allocation failed');
  for(let i=0;i<64;i++)HEAPU8[p+i]=(i*73+42)&255;
  return p;
 });
 let memory, pagemap;
 try {
  memory=fs.openSync(`/proc/${renderer.id}/mem`,'r');
  const expected=Buffer.from(Array.from({length:64},(_,i)=>(i*73+42)&255));
  const matching=candidates.filter(m=>{
   const bytes=Buffer.alloc(64);assert.equal(fs.readSync(memory,bytes,0,64,m.begin+pointer),64);return bytes.equals(expected);
  });
  assert.equal(matching.length,1,'A marker must identify exactly one WASM backing mapping');
  const mapping=matching[0];pagemap=fs.openSync(`/proc/${renderer.id}/pagemap`,'r');
  const rows=[];
  for(const allocation of allocations){
   assert(allocation.pointer+allocation.requested<=capacity,'Tracked allocation exceeds linear memory');
   const first=Math.ceil((mapping.begin+allocation.pointer)/4096);
   const end=Math.floor((mapping.begin+allocation.pointer+allocation.requested)/4096);
   const count=Math.max(0,end-first),bytes=Buffer.alloc(count*8);
   assert.equal(fs.readSync(pagemap,bytes,0,bytes.length,first*8),bytes.length,'Short pagemap read');
   let resident=0,swapped=0;
   for(let i=0;i<count;i++){
    const bits=bytes.readBigUInt64LE(i*8);
    if(bits&(1n<<63n))resident++;
    if(bits&(1n<<62n))swapped++;
   }
   rows.push({...allocation,whole_page_resident_bytes:resident*4096,whole_page_swapped_bytes:swapped*4096,whole_page_count:count});
  }
  return {renderer_pid:renderer.id,mapping,marker_verified:true,page_bytes:4096,
   scope:'Whole pages wholly inside requested allocations; boundary pages excluded, live guest is not globally frozen',allocations:rows};
 } finally {
  if(memory!==undefined)fs.closeSync(memory);if(pagemap!==undefined)fs.closeSync(pagemap);
  await page.evaluate(p=>Module._free(p),pointer);
 }
}
