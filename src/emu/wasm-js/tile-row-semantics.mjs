// An entire original tile call and its dispatch loop backedge form one unit.
export function createTileRowFactory(entries) {
 return ({geometry,shifterCarry,heap,resolve,diagnostics=false})=>{
  if(!Array.isArray(geometry)||geometry.length!==8||[...geometry,shifterCarry].some(x=>!Number.isSafeInteger(x)||x<0))throw Error('CPU geometry');
  const [size,reg,cpsr,n,z,c,v,t]=geometry;
  if((reg&3)||reg+64>size||[cpsr,n,z,c,v,t,shifterCarry].some(x=>(x&3)||x+4>size))throw Error('CPU geometry');
  const stats=diagnostics?{calls:0,tiles:0,transparent:0,additive:0,subtractive:0,empty:0,half:0,instructions:0,pages:0,declines:{}}:null;
  return {abi:'original-resource-symbol-v1',stats,entries:entries.map(row=>{
   const pages=new Uint32Array(256),reads=new Uint32Array(256),writes=new Uint32Array(256);
   let buffer,owner,gen,space,low,high;
   function execute(cpu,budget,generation,asid,mappingLow,mappingHigh){
    const bytes=heap(),words=new Uint32Array(bytes.buffer),halves=new Uint16Array(bytes.buffer);
    const decline=why=>{if(stats)stats.declines[why]=(stats.declines[why]||0)+1;return 0;};
    if(!Number.isInteger(cpu)||cpu<=0||(cpu&3)||cpu+size>bytes.byteLength||!Number.isInteger(budget)||budget<245)return 0;
    const ri=(cpu+reg)>>>2;
    if(words[ri+15]!==row.pc||words[(cpu+t)>>>2])return 0;
    if(buffer!==bytes.buffer||owner!==cpu||gen!==generation||space!==asid||low!==mappingLow||high!==mappingHigh){
     reads.fill(0);writes.fill(0);buffer=bytes.buffer;owner=cpu;gen=generation;space=asid;low=mappingLow;high=mappingHigh;
    }
    const pagePointer=(address,write)=>{
     const page=(address&0xfffff000)>>>0,slot=(page>>>12)&255,cache=write?writes:reads;
     if(pages[slot]!==page){reads[slot]=writes[slot]=0;pages[slot]=page;}
     let host=cache[slot];
     if(!host){host=resolve(cpu,page,4096,write?1:0)>>>0;if(!host||(host&3)||host+4096>bytes.byteLength)return 0;cache[slot]=host;if(stats)stats.pages++;}
     return host+(address&4095);
    };
    const pointer=(address,length,write)=>{
     if(address<0||address+length>0x100000000)return 0;
     const host=pagePointer(address,write);if(!host)return 0;
     if((address&4095)+length>4096&&pagePointer(address+length-1,write)!==host+length-1)return 0;
     return host;
    };
    const object=words[ri+4],resource=words[ri+6],stack=words[ri+13];
    let col=words[ri+5],cursor=words[ri+8],context=words[ri+12];
    const limit=words[ri+10],destination=words[ri+9];
    if((object|resource|stack|context)&3||(cursor|destination)&1)return decline('alignment');
    let count=0,tiles=0;
    const overlap=(a,an,b,bn)=>a<b+bn&&b<a+an;
    while(tiles<128&&((col+1)|0)<(limit|0)){
     const ob=pointer(object,100,false),res=pointer(resource,48,false),idx=pointer(cursor,2,false),ctx=pointer(context+252,1,false);
     if(!ob||!res||!idx||!ctx){decline('metadata');break;}
     const mode=bytes[ob+9],subtractive=mode===5,half=mode===2,blend=mode===4||subtractive||half;
     if((mode!==0&&!blend)||bytes[ctx]===12||(words[(ob+96)>>>2]&4)){decline('mode');break;}
     const tile=halves[idx>>>1];
     if(!tile){
      if(count+17>budget)break;
      words[ri]=0;words[ri+1]=words[(res+28)>>>2];words[ri+2]=0;words[ri+3]=words[(ob+96)>>>2];words[ri+14]=words[(res+20)>>>2];
      col=(col+1)>>>0;cursor+=2;words[ri+5]=col;words[ri+8]=cursor;
      const difference=(col-limit)>>>0,flags=(difference&0x80000000)|(difference===0?0x40000000:0)|(col>=limit?0x20000000:0)|((((col^limit)&(col^difference))>>>31)<<28);
      words[(cpu+cpsr)>>>2]=(words[(cpu+cpsr)>>>2]&0x0fffffff)|flags;
      words[(cpu+n)>>>2]=flags>>>31;words[(cpu+z)>>>2]=(flags>>>30)&1;words[(cpu+c)>>>2]=(flags>>>29)&1;words[(cpu+v)>>>2]=(flags>>>28)&1;words[(cpu+shifterCarry)>>>2]=1;
      count+=17;tiles++;if(stats)stats.empty++;continue;
     }
     const signed=(tile<<16)>>16,negative=signed<0;
     const srcBase=words[(res+(negative?32:28))>>>2],tableBase=words[(res+(negative?24:20))>>>2];
     const attrBase=words[(res+(negative?44:40))>>>2],attr=pointer(attrBase+Math.abs(signed),1,false);
     if(!attr){decline('attribute');break;}
     const attributes=bytes[attr],transparent=(attributes&128)!==0;
     const source=srcBase+Math.abs(signed)*32,palette=tableBase+(attributes&127)*32,dest=destination+col*16;
     const src=pointer(source,32,false),pal=pointer(palette,32,false),sp=pointer(stack-((transparent||blend)?32:36),(transparent||blend)?32:36,true);
     if(!src||!pal||!sp||(source&3)||(palette&1)||dest<0||dest+3600>0x100000000){decline('data');break;}
     const out=pointer(dest,3600,true),stackBytes=(transparent||blend)?32:36;
     const unsafe=(a,len)=>overlap(sp,stackBytes,a,len)||overlap(out,3600,a,len);
     if(!out||(blend&&pointer(dest,3600,false)!==out)||overlap(sp,stackBytes,out,3600)||unsafe(ob,100)||unsafe(res,48)||unsafe(idx,2)||unsafe(ctx,1)||unsafe(attr,1)||unsafe(src,32)||unsafe(pal,32)){decline('alias-or-output');break;}
     let cost=transparent?267:(dest&3)?254:245;
     if(blend){
      cost=half?1420:subtractive?1430:1618;
      if(transparent){cost=half?396:subtractive?406:402;const pixelCost=(half||subtractive)?18:21;for(let i=0;i<32;i++){const b=bytes[src+i];if(b&15)cost+=pixelCost;if(b>>>4)cost+=pixelCost;}}
     }
     if(count+cost>budget)break;
     // Preserve the guest stack writes of PUSH, including the return address.
     const saved=(transparent||blend)?[4,5,6,7,8,9,10]:[4,5,6,7,8,9,10,11];
     const target=blend?(half?(transparent?0x700039fc:0x70003490):subtractive?(transparent?0x70003cb4:0x70003718):(transparent?0x70003bc4:0x70003638)):transparent?0x700037e0:0x700032bc;
     for(let j=0;j<saved.length;j++){
      const r=saved[j];words[(sp>>>2)+j]=r===5?col:r===7?target:r===8?cursor+2:words[ri+r];
     }
     words[(sp>>>2)+saved.length]=0x70020e8c;
     let packed=0;const pi=pal>>>1;
     if(blend){
      for(let y=0;y<8;y++){
       const sourceWord=words[(src>>>2)+y],di=(out>>>1)+y*256;
       for(let x=0;x<8;x++){
        const nibble=(sourceWord>>>(x*4))&15;packed=0;
        if(!transparent||nibble){
         const color=halves[pi+nibble],old=halves[di+x];
         if(half){
          const mixed=(color&old&0xffdf)+(((color^old)&0xf79e)>>>1);
          packed=mixed&31;halves[di+x]=mixed;
         }else if(subtractive){
          const red=(old&31)-(color&31),green=((old>>>6)&31)-((color>>>6)&31),blue=(old>>>11)-(color>>>11);
          packed=red<0?0:red;halves[di+x]=packed|((green<0?0:green)<<6)|((blue<0?0:blue)<<11);
         }else{
          const red=(color&31)+(old&31),green=((color>>>6)&31)+((old>>>6)&31),blue=(color>>>11)+(old>>>11);
          packed=red>31?31:red;halves[di+x]=packed|((green>31?31:green)<<6)|((blue>31?31:blue)<<11);
         }
        }
       }
      }
     }else if(!transparent){
      for(let y=0;y<8;y++){
       packed=words[(src>>>2)+y];const di=(out>>>1)+y*256;
       halves[di]=halves[pi+(packed&15)];halves[di+1]=halves[pi+((packed>>>4)&15)];
       halves[di+2]=halves[pi+((packed>>>8)&15)];halves[di+3]=halves[pi+((packed>>>12)&15)];
       halves[di+4]=halves[pi+((packed>>>16)&15)];halves[di+5]=halves[pi+((packed>>>20)&15)];
       halves[di+6]=halves[pi+((packed>>>24)&15)];halves[di+7]=halves[pi+(packed>>>28)];
      }
     }else{
      for(let y=0;y<8;y++){
       packed=words[(src>>>2)+y];const di=(out>>>1)+y*256;
       for(let x=0;x<8;x++){const nibble=(packed>>>(x*4))&15;if(nibble)halves[di+x]=halves[pi+nibble];}
      }
     }
     context=words[(ob+4)>>>2];cursor+=2;col=(col+1)>>>0;
     words[ri]=dest+((transparent||blend)?4096:3584)+(!transparent&&!blend&&(dest&3)?2:0);
     words[ri+1]=source+32;words[ri+2]=palette;words[ri+3]=packed;
     words[ri+5]=col;words[ri+7]=target;words[ri+8]=cursor;words[ri+12]=context;words[ri+14]=0x70020e8c;
     const difference=(col-limit)>>>0,flags=(difference&0x80000000)|(difference===0?0x40000000:0)|(col>=limit?0x20000000:0)|((((col^limit)&(col^difference))>>>31)<<28);
     words[(cpu+cpsr)>>>2]=(words[(cpu+cpsr)>>>2]&0x0fffffff)|flags;
     words[(cpu+n)>>>2]=flags>>>31;words[(cpu+z)>>>2]=(flags>>>30)&1;words[(cpu+c)>>>2]=(flags>>>29)&1;words[(cpu+v)>>>2]=(flags>>>28)&1;words[(cpu+shifterCarry)>>>2]=1;
     count+=cost;tiles++;if(stats){if(transparent)stats.transparent++;if(blend){if(half)stats.half++;else if(subtractive)stats.subtractive++;else stats.additive++;}}
    }
    if(stats&&count){stats.calls++;stats.tiles+=tiles;stats.instructions+=count;}return count;
   }
   return {...row,originalFallback:true,execute};
  })};
 };
}
