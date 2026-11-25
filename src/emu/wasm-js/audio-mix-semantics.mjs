// Complete backedges of the authenticated original signed sample mixer.
// Unsupported memory, short budgets and the exiting iteration use the CPU.
export function createAudioMixFactory(entries){
 return ({geometry,shifterCarry,heap,resolve,diagnostics=false})=>{
  const [size,reg,cpsr,n,z,c,v,t]=geometry;
  if(!Array.isArray(geometry)||geometry.length!==8||[...geometry,shifterCarry].some(x=>!Number.isSafeInteger(x)||x<0)
   ||(reg&3)||reg+64>size||[cpsr,n,z,c,v,t,shifterCarry].some(x=>(x&3)||x+4>size))throw Error('CPU geometry');
  const stats=diagnostics?{calls:0,samples:0,instructions:0,pages:0}:null;
  return {abi:'original-resource-symbol-v1',stats,entries:entries.map(row=>{
   const pages=new Uint32Array(256),reads=new Uint32Array(256),writes=new Uint32Array(256);
   let buffer,owner,gen,space,low,high;
   function execute(cpu,budget,generation,asid,mappingLow,mappingHigh){
    const bytes=heap(),words=new Uint32Array(bytes.buffer),halves=new Uint16Array(bytes.buffer);
    if(!Number.isInteger(cpu)||cpu<=0||(cpu&3)||cpu+size>bytes.byteLength||!Number.isInteger(budget)||budget<21||budget>0xffffffff)return 0;
    const ri=(cpu+reg)>>>2;if(words[ri+15]!==row.pc||words[(cpu+t)>>>2])return 0;
    if(buffer!==bytes.buffer||owner!==cpu||gen!==generation||space!==asid||low!==mappingLow||high!==mappingHigh){
     reads.fill(0);writes.fill(0);buffer=bytes.buffer;owner=cpu;gen=generation;space=asid;low=mappingLow;high=mappingHigh;
    }
    const pointer=(address,write)=>{
     if(address<0||address>0xffffffff)return 0;
     const page=(address&0xfffff000)>>>0,slot=(page>>>12)&255,cache=write?writes:reads;
     if(pages[slot]!==page){reads[slot]=writes[slot]=0;pages[slot]=page;}
     let host=cache[slot];if(!host){host=resolve(cpu,page,4096,write?1:0)>>>0;if(!host||(host&3)||host+4096>bytes.byteLength)return 0;cache[slot]=host;if(stats)stats.pages++;}
     return host+(address&4095);
    };
    const object=words[ri],bound=words[ri+4],limit=words[ri+8],volume=words[ri+9],source=words[ri+10];
    let dest=words[ri+5],index=words[ri+6],fraction=words[ri+12],count=0,samples=0,value;
    if((object&3)||(dest&1)||object>0xffffffaf)return 0;
    while(samples<1024&&fraction<=bound){
     const next=(index+1)>>>0;if(next>=limit)break;
     const src=pointer((source+(fraction>>>16))>>>0,false),dst=pointer(dest,true),old=pointer(dest,false),step=pointer(object+0x50,false),pos=pointer(object+0x4c,true);
     if(!src||!dst||old!==dst||!step||!pos)break;
     const mixed=(Math.imul(volume,(bytes[src]<<24)>>24)+((halves[dst>>>1]<<16)>>16))|0;
     const cost=mixed>32767?21:23;if(count+cost>budget)break;
     value=mixed>32767?32767:mixed< -32768?-32768:mixed;
     fraction=(fraction+words[step>>>2])>>>0;halves[dst>>>1]=value;words[pos>>>2]=fraction;
     dest=(dest+2)>>>0;index=next;count+=cost;samples++;
    }
    if(!samples)return 0;
    words[ri+1]=0xffff8000;words[ri+2]=value;words[ri+3]=fraction;words[ri+5]=dest;words[ri+6]=index;words[ri+12]=fraction;words[ri+14]=32767;
    const difference=(index-limit)>>>0,flags=(difference&0x80000000)|(difference===0?0x40000000:0)|(index>=limit?0x20000000:0)|((((index^limit)&(index^difference))>>>31)<<28);
    words[(cpu+cpsr)>>>2]=(words[(cpu+cpsr)>>>2]&0x0fffffff)|flags;
    words[(cpu+n)>>>2]=flags>>>31;words[(cpu+z)>>>2]=(flags>>>30)&1;words[(cpu+c)>>>2]=(flags>>>29)&1;words[(cpu+v)>>>2]=(flags>>>28)&1;words[(cpu+shifterCarry)>>>2]=0;
    if(stats){stats.calls++;stats.samples+=samples;stats.instructions+=count;}return count;
   }
   return {...row,originalFallback:true,execute};
  })};
 };
}
