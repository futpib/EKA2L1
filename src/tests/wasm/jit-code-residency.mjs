import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const pageSize=Number(execFileSync('getconf',['PAGESIZE'],{encoding:'utf8'}));

export function jitCodeResidency(pid, perfMap, smapsFile) {
 const smaps=fs.readFileSync(smapsFile,'utf8');
 const headers=[...smaps.matchAll(/^([0-9a-f]+)-([0-9a-f]+)\s+([rwxps-]{4})\s+\S+\s+\S+\s+\d+\s*(.*)$/gm)];
 const executable=[];
 for(let i=0;i<headers.length;i++){
  const match=headers[i];
  if(match[4]!=='[anon:v8]' || !match[3].includes('x'))continue;
  const text=smaps.slice(match.index+match[0].length,headers[i+1]?.index??smaps.length);
  const pssKiB=Number(text.match(/^Pss:\s+(\d+) kB/m)?.[1]??0);
  const rssKiB=Number(text.match(/^Rss:\s+(\d+) kB/m)?.[1]??0);
  executable.push({begin:Number.parseInt(match[1],16),end:Number.parseInt(match[2],16),pssKiB,rssKiB});
 }
 const guestPages=new Set(),otherPages=new Set(),seen=new Set();
 let guestEntries=0,otherEntries=0,guestCodeBytes=0,otherCodeBytes=0;
 for(const line of fs.readFileSync(perfMap,'utf8').split('\n')){
  if(!line || seen.has(line))continue;
  seen.add(line);
  const match=/^([0-9a-f]+) ([0-9a-f]+) (.+)$/.exec(line);
  if(!match)continue;
  const begin=Number.parseInt(match[1],16),size=Number.parseInt(match[2],16);
  if(!Number.isSafeInteger(begin) || !Number.isSafeInteger(size) || !size)continue;
  const guest=/^JS:(?:f_|r_|s_)\d+/.test(match[3]);
  const pages=guest?guestPages:otherPages;
  if(guest){guestEntries++;guestCodeBytes+=size;}
  else{otherEntries++;otherCodeBytes+=size;}
  for(let address=Math.floor(begin/pageSize);address<=Math.floor((begin+size-1)/pageSize);address++)pages.add(address);
 }
 const fd=fs.openSync(`/proc/${pid}/pagemap`,'r');
 const categories={guest_only:0,other_only:0,mixed:0,no_logged_code:0};
 let present=0,swapped=0;
 try{
  for(const mapping of executable){
   const first=mapping.begin/pageSize,count=(mapping.end-mapping.begin)/pageSize;
   assert(Number.isInteger(first)&&Number.isInteger(count));
   const bytes=Buffer.alloc(count*8);
   assert.equal(fs.readSync(fd,bytes,0,bytes.length,first*8),bytes.length,'Short executable pagemap read');
   for(let i=0;i<count;i++){
    const bits=bytes.readBigUInt64LE(i*8);
    if(bits&(1n<<62n))swapped++;
    if(!(bits&(1n<<63n)))continue;
    present++;
    const address=first+i,guest=guestPages.has(address),other=otherPages.has(address);
    categories[guest?(other?'mixed':'guest_only'):(other?'other_only':'no_logged_code')]++;
   }
  }
 }finally{fs.closeSync(fd);}
 return {renderer_pid:pid,page_bytes:pageSize,executable_mappings:executable.length,
  executable_pss_kib:executable.reduce((n,m)=>n+m.pssKiB,0),
  executable_rss_kib:executable.reduce((n,m)=>n+m.rssKiB,0),
  present_pages:present,swapped_pages:swapped,resident_pages_by_logged_code:categories,
  guest_code_entries:guestEntries,other_code_entries:otherEntries,
  guest_logged_code_bytes:guestCodeBytes,other_logged_code_bytes:otherCodeBytes,
  scope:'Present pages intersecting deduplicated, append-only V8 perf-map entries; reused code addresses may retain stale labels'};
}
