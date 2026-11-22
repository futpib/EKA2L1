// Complete backedges of the authenticated original transparent indexed-row loop.
// Unsupported memory and the exiting iteration remain in the original CPU.
export function createIndexedRowFactory(entries) {
 return function configure({geometry,shifterCarry,heap,resolve,diagnostics=false}) {
  const [size,reg,cpsr,n,z,c,v,t]=geometry;
  if(!Array.isArray(geometry)||geometry.length!==8||[...geometry,shifterCarry].some(x=>!Number.isSafeInteger(x)||x<0)
   ||(reg&3)||reg+64>size||[cpsr,n,z,c,v,t,shifterCarry].some(x=>(x&3)||x+4>size))throw Error('CPU geometry');
  const stats=diagnostics?{calls:0,iterations:0,pages:0}:null;
  return {abi:'original-resource-symbol-v1',stats,entries:entries.map(row=>{
   const pages=new Uint32Array(64),reads=new Uint32Array(64),writes=new Uint32Array(64);
   let buffer,owner,gen,space,low,high;
   function execute(cpu,budget,generation,asid,mappingLow,mappingHigh){
    const bytes=heap(),words=new Uint32Array(bytes.buffer),halves=new Uint16Array(bytes.buffer);
    if(!Number.isInteger(cpu)||cpu<=0||(cpu&3)||cpu+size>bytes.byteLength||!Number.isInteger(budget)||budget<11||budget>0xffffffff)return 0;
    const ri=(cpu+reg)>>>2;
    if(words[ri+15]!==row.pc||words[(cpu+t)>>>2])return 0;
    if(buffer!==bytes.buffer||owner!==cpu||gen!==generation||space!==asid||low!==mappingLow||high!==mappingHigh){
     reads.fill(0);writes.fill(0);buffer=bytes.buffer;owner=cpu;gen=generation;space=asid;low=mappingLow;high=mappingHigh;
    }
    const pointer=(address,write)=>{
     const page=(address&0xfffff000)>>>0,slot=(page>>>12)&63,cache=write?writes:reads;
     if(pages[slot]!==page){reads[slot]=writes[slot]=0;pages[slot]=page;}
     let host=cache[slot];
     if(!host){host=resolve(cpu,page,4096,write?1:0)>>>0;if(!host||(host&3)||host+4096>bytes.byteLength)return 0;cache[slot]=host;if(stats)stats.pages++;}
     return host+(address&4095);
    };
    let source=words[ri+5],destination=words[ri+4],counter=words[ri+6],limit=words[ri+12],value,index,flags;
    const palette=words[ri],limitAddress=words[ri+8]+16,maximum=Math.min(Math.floor(budget/11),1024);
    if((destination|palette)&1||limitAddress>0xffffffff)return 0;
    let count=0,lastValue,lastIndex;
    while(count<maximum){
     // One callback-free page span covers each source/destination and the table.
     if((palette&4095)>4096-512)break;
     const src=pointer(source,false),dst=pointer(destination,true),table=pointer(palette,false),control=pointer(limitAddress,false);
     if(!src||!dst||!table||!control)break;
     const chunk=Math.min(maximum-count,4096-(source&4095),(4096-(destination&4095))>>>1);
     if(control>=dst&&control<dst+chunk*2)break;
     const riTable=table>>>1,di=dst>>>1,newLimit=bytes[control];
     let used=0;
     for(;used<chunk;used++){
      index=bytes[src+used];value=index*2;
      const nextLimit=index?newLimit:limit,after=(counter+1)>>>0;
      // The exiting iteration remains entirely in the original interpreter.
      if((nextLimit|0)<=(after|0))break;
      if(index){value=halves[riTable+index];halves[di+used]=value;}
      counter=after;limit=nextLimit;lastValue=value;lastIndex=index;
     }
     count+=used;source=(source+used)>>>0;destination=(destination+used*2)>>>0;
     if(used<chunk)break;
    }
    if(!count)return 0;
    const difference=(limit-counter)>>>0;
    flags=(difference&0x80000000)|(difference===0?0x40000000:0)|(limit>=counter?0x20000000:0)|((((limit^counter)&(limit^difference))>>>31)<<28);
    words[ri+1]=lastValue;words[ri+3]=lastIndex;
    words[ri+4]=destination;words[ri+5]=source;words[ri+6]=counter;words[ri+12]=limit;
    words[(cpu+cpsr)>>>2]=(words[(cpu+cpsr)>>>2]&0x0fffffff)|flags;
    words[(cpu+n)>>>2]=flags>>>31;words[(cpu+z)>>>2]=(flags>>>30)&1;words[(cpu+c)>>>2]=(flags>>>29)&1;words[(cpu+v)>>>2]=(flags>>>28)&1;
    words[(cpu+shifterCarry)>>>2]=1;
    if(stats){stats.calls++;stats.iterations+=count;}return count*11;
   }
   return {...row,originalFallback:true,execute};
  })};
 };
}
