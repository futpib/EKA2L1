import fs from 'node:fs';
import assert from 'node:assert/strict';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const {values}=parseArgs({options:Object.fromEntries(['build','helper','output'].map(n=>[n,{type:'string'}]))});
for(const n of ['build','helper','output'])assert(values[n],'Missing --'+n);
const build=path.resolve(values.build),helper=path.resolve(values.helper),output=path.resolve(values.output);
assert(!fs.existsSync(output));fs.mkdirSync(output,{recursive:true});
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const receipt=JSON.parse(fs.readFileSync(build+'/build.json'));assert(receipt.pass);assert.equal(sha(build+'/fixture.mjs'),receipt.outputs['fixture.mjs']);
const create=(await import(pathToFileURL(build+'/fixture.mjs'))).default;
const {createTileRowFactory}=await import(pathToFileURL(helper));
const row={"asid": 1, "pc": 1879182840, "words": [3772252338, 3785369600, 3786414146, 1435906076, 1167470624, 1435951124, 1350767233, 1167515672, 1078137473, 3813670912, 436207618], "sources": [{"pc": 1879182840, "words": [3772252338, 3785369600, 3786414146, 1435906076, 1167470624, 1435951124, 1350767233, 1167515672, 1078137473, 3813670912, 436207618, 3851694176, 3809673220, 167772183, 3786422338, 1435906088, 1167466540, 1113796608, 1473456194, 1205018626, 3856412924, 3813867532, 167772224, 3786421154, 3818942464, 167772184, 3855888393, 3780313095, 436207658, 3851694176, 3809673220, 94335408, 436207717, 3791790207, 3767411330, 3767075333, 3778019127, 3851730948, 3800387585, 3780444170, 3137339350, 3843756080, 3843760184, 3800109057, 3852218612, 3780182018, 3842707504, 3767112195, 3137339328, 3796619304, 3902648304, 3855888393, 3813867520, 167772175, 3813867521, 94335316, 184549351, 3813867522, 94335308, 184549348, 3813867523, 94335300, 184549345, 3813867524, 94335292, 184549342, 3813867525, 94335284, 452984800, 3942645722, 3852431660, 3942645720, 3813867521, 94335268, 184549333, 3813867522, 94335260, 184549330, 3813867523, 94335252, 184549327, 3813867524, 94335244, 184549324, 3813867525, 94335236, 452984782, 3942645704, 3786421154, 167772169, 3855888393, 3813867520, 436207641, 3851694176, 3809673220, 94335200, 184549311, 3814129664, 452984765, 3942645697, 3855888393, 3813867520, 184549342, 3813867521, 94335168, 184549302, 3813867522, 94335160, 184549299, 3813867523, 94335152, 184549296, 3813867524, 94335144, 184549293, 3813867525, 94335136, 452984810, 3942645673, 3813867521, 94335124, 184549286, 3813867522, 94335116, 184549283, 3813867523, 94335108, 184549280, 3813867524, 94335100, 184549277, 3813867525, 94335092, 452984794, 3942645657, 3791790207, 3767411330, 3767075333, 3788780976, 3959392782, 3942645655, 1879062496, 1879061424, 1879061648, 1879061848, 1879062072, 1879062296, 1879061180, 1879062796, 1879063036, 1879063252, 1879063492, 1879063732, 1879061156, 1879061136, 1879061140, 1879061144, 1879061148, 1879061152, 1879061160, 1879061164, 1879061168, 1879061172, 1879061176]}, {"pc": 1879061180, "words": [3912060912, 3795848194, 3818958856, 3818962959, 3785408652, 3809476611, 310378498, 167772186, 3834720260, 3797659649, 3758903427, 3758907843, 3758912451, 3758917059, 3758921667, 3758926275, 3784462517, 3784466614, 3784470711, 3784474808, 3784478905, 3784483002, 3783682055, 3783817225, 3758918595, 3758923203, 3784474808, 3784478905, 3800042498, 3783952392, 3779088562, 3900703168, 3787493564, 452984805, 3925868568, 3834720260, 3797659649, 3758903427, 3758907843, 3758912451, 3758917059, 3784462517, 3784466614, 3784470711, 3784474808, 3783612422, 3783747592, 3758913475, 3758918083, 3758922691, 3758927299, 3784470711, 3784474808, 3784478905, 3784483002, 3800042498, 3783751688, 3783886858, 3900703200, 452984806, 3904737264]}, {"pc": 1879062496, "words": [3912058864, 3818954760, 3818962959, 3785408652, 3834720260, 3759952003, 294801589, 297816240, 3759952323, 294801589, 297816242, 3759952835, 294801589, 297816244, 3759953347, 294801589, 297816246, 3759953859, 294801589, 297816248, 3759954371, 294801589, 297816250, 3759954883, 294801589, 297816252, 3759955395, 294801589, 297816254, 3800042498, 3797590017, 452984803, 3904735216]}, {"pc": 1879062072, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3758305452, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3813867551, 2745184287, 3813933087, 2745188383, 3813998623, 2745192479, 3783475972, 3783542149, 3770695858, 3838914561, 3758895683, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3813867551, 2745184287, 3813933087, 2745188383, 3813998623, 2745192479, 3783475972, 3783542149, 3770695858, 3797520385, 452984783, 3797590017, 3800043039, 452984779, 3904735216]}, {"pc": 1879063492, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3759354028, 167772180, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3813867551, 2745184287, 3813933087, 2745188383, 3813998623, 2745192479, 3783475972, 3783542149, 3787473072, 3838914561, 3800039426, 3759944227, 167772180, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3813867551, 2745184287, 3813933087, 2745188383, 3813998623, 2745192479, 3783475972, 3783542149, 3787473072, 3797520385, 3800039426, 452984779, 3797590017, 3800043039, 452984775, 3904735216]}, {"pc": 1879062296, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3758305452, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3763744771, 1134571520, 3763814404, 1134575616, 3763884037, 1134579712, 3783475972, 3783542149, 3770695858, 3838914561, 3758895683, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3763744771, 1134571520, 3763814404, 1134575616, 3763884037, 1134579712, 3783475972, 3783542149, 3770695858, 3797520385, 452984789, 3797590017, 3800043039, 452984785, 3904735216]}, {"pc": 1879063732, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3759354028, 167772177, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3763744771, 1134571520, 3763814404, 1134575616, 3763884037, 1134579712, 3783475972, 3783542149, 3787473072, 3838914561, 3800039426, 3759944227, 167772177, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3763744771, 1134571520, 3763814404, 1134575616, 3763884037, 1134579712, 3783475972, 3783542149, 3787473072, 3797520385, 3800039426, 452984785, 3797590017, 3800043039, 452984781, 3904735216]}, {"pc": 1879061648, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3758305452, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3785371811, 3785375908, 3785380005, 3783475972, 3783542149, 3770695858, 3838914561, 3758895683, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3785371811, 3785375908, 3785380005, 3783475972, 3783542149, 3770695858, 3797520385, 452984789, 3797590017, 3800043039, 452984785, 3904735216]}, {"pc": 1879063036, "words": [3912058864, 3818962975, 3818954760, 3818950660, 3855691776, 3759354028, 167772177, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3785371811, 3785375908, 3785380005, 3783475972, 3783542149, 3787473072, 3838914561, 3800039426, 3759944227, 167772177, 3785371779, 3784462515, 3788538032, 3758895109, 3758900005, 3758904741, 3758907400, 3758912296, 3758917032, 3766693894, 3766763527, 3766833160, 3785371811, 3785375908, 3785380005, 3783475972, 3783542149, 3787473072, 3797520385, 3800039426, 452984785, 3797590017, 3800043039, 452984781, 3904735216]}]};
const base=0x100000,pc=row.pc,initialData=new Uint8Array(0x80000);
for(let i=0;i<initialData.length;i++)initialData[i]=(i*73+(i>>>8)*37+17)&255;
const check=(condition,label)=>{if(!condition)throw Error(label);};
const fields=['Reg','Reg_usr','Reg_svc','Reg_abort','Reg_undef','Reg_irq','Reg_firq','Spsr','CP15','VFP','ExtReg','Cpsr','Spsr_copy','Mode','Bank','NFlag','ZFlag','CFlag','VFlag','IFFlags','shifter_carry_out','TFlag','NresetSig','NfiqSig','NirqSig','abortSig','NtransSig','bigendSig','syscallSig','fault','fault_address'];
async function instance(create,mode){
 const m=await create({print(){},printErr(){}});m._ref_create();m._ref_trace(0);
 m.code=m._ref_map(0x70000000,0x30000,5);m.data=m._ref_map(base,0x80000,3);
 const w=new Uint32Array(m.HEAPU8.buffer);
 for(const span of row.sources)w.set(span.words,(m.code+span.pc-0x70000000)>>>2);
 if(mode!=='original'){
  const dummy={asid:1,pc:0x70028000,words:[0xe2522001,0x1afffffd],function:1,complete:true};
  const unused=()=>{throw Error('Unexpected generated fallback');};
  const paletteRows=[{pc:0x700032dc,words:row.sources[1].words.slice(8,34)},{pc:0x70003348,words:row.sources[1].words.slice(35,60)}].map((r,i)=>({...r,asid:1,complete:true,function:i+2}));
  const counted=mode==='baseline'?paletteRows.map((r,i)=>({...r,function:i})):[{...row,function:0,complete:true},dummy];
  m.countedBlockFactory=()=>({entries:counted,functions:[unused,unused]});
  m.loopBlockFactory=()=>({abi:'counted-original-arm-loop-v2',entries:mode==='baseline'?counted:[{...dummy,function:0}],functions:mode==='baseline'?[unused,unused]:[unused]});
  if(mode==='candidate')m.semanticFactory=createTileRowFactory([row]);
  check(m._eka2l1_counted_configure(1)===2,'counted '+m.__countedError);
  check(m._eka2l1_loop_configure(1)===(mode==='baseline'?2:1),'loop '+m.__loopError+' '+m.__semanticError);
  check(m._eka2l1_loop_packed_pixel_configure(1)===1,'pixel configure');
 }
 return m;
}
function seed(m,{transparent=false,shift=0,negative=false,flags=0,sourcePattern=null,tile=1,mode=0,format=16,source=base+0x10000,palette=base+0x20000,destination=base+0x30000,stack=base+0x70000,limit=32}={}){
 const b=m.HEAPU8,w=new Uint32Array(b.buffer),h=new Uint16Array(b.buffer),at=a=>m.data+a-base;
 b.set(initialData,m.data);
 const object=base+0x100,resource=base+0x200,context=base+0x400,cursor=base+0x800;
 w[at(object+4)>>>2]=context;b[at(object+9)]=mode;w[at(object+96)>>>2]=0;b[at(context+252)]=format;
 for(const offset of[20,24])w[at(resource+offset)>>>2]=palette;
 for(const offset of[28,32])w[at(resource+offset)>>>2]=source;
 for(const offset of[40,44])w[at(resource+offset)>>>2]=base+0x1000;
 for(let i=0;i<64;i++)h[(at(cursor)>>>1)+i]=negative?-tile:tile;
 b[at(base+0x1000+tile)]=transparent?128:0;
 for(let i=0;i<16;i++)h[(at(palette)>>>1)+i]=((i*991)^0x9bac)&65535;
 if(sourcePattern!==null)b.fill(sourcePattern,at(source+tile*32),at(source+tile*32+32));
 m._ref_cpsr((0x10|flags)>>>0);for(let i=0;i<16;i++)m._ref_register(i,0x12340000+i);
 for(const[r,v]of[[4,object],[5,0],[6,resource],[8,cursor],[9,destination+shift],[10,limit],[11,stack+100],[12,context],[13,stack],[15,pc]])m._ref_register(r,v);
 m._ref_irq(0);m._ref_enable_tlb(1);m._ref_generation();m._ref_clear_code();
}
const state=m=>{const s=JSON.parse(m.UTF8ToString(m._ref_snapshot()));return Object.fromEntries(fields.map(k=>[k,s[k]]));};
function equal(a,b,label){
 const x=state(a),y=state(b);for(const key of fields)check(JSON.stringify(x[key])===JSON.stringify(y[key]),key+' '+label+' '+JSON.stringify([x[key],y[key]]));
 assert.deepEqual(Buffer.from(a.HEAPU8.buffer,a.data,initialData.length),Buffer.from(b.HEAPU8.buffer,b.data,initialData.length),'RAM '+label);
}
async function prove(create){
 const a=await instance(create,'original'),b=await instance(create,'candidate');let cases=0;
 try{
  for(const config of [{mode:2},{mode:2,shift:2},{mode:2,negative:true},{mode:2,transparent:true},{mode:2,transparent:true,negative:true,shift:2},{mode:2,transparent:true,sourcePattern:0},{mode:2,transparent:true,sourcePattern:255},{mode:2,transparent:true,sourcePattern:15}, {},{shift:2},{transparent:true},{transparent:true,shift:2},{negative:true},{negative:true,transparent:true},{mode:5},{mode:5,transparent:true},{mode:5,shift:2,negative:true},{mode:5,transparent:true,negative:true},{mode:4},{mode:4,transparent:true},{mode:4,negative:true,shift:2},{mode:4,transparent:true,negative:true,shift:2},{source:base+0x10fe0},{destination:base+0x30ffe},{palette:base+0x20ff0}])
  for(const flags of [0,0x10000000,0x20000000,0x80000000,0xf0000000])for(const budget of [1,11,35,244,245,246,253,254,255,266,267,268,395,396,397,489,490,491,534,1024,1419,1420,1421,1429,1430,1431,1557,1558,1559,1617,1618,1619,1745,1746,1747,4000]){
   seed(a,{...config,flags});seed(b,{...config,flags});check(a._ref_run(budget)===budget,'original count '+cases+' '+JSON.stringify({config,flags,budget}));try{check(b._ref_semantic_run(budget)===budget,'selected count');}catch(e){throw Error('case '+cases+' '+JSON.stringify({config,flags,budget})+' '+b.__loopError+' '+b.__semanticError+' '+e);}equal(a,b,'case '+cases+' '+JSON.stringify({config,flags,budget}));cases++;
  }
  for(const flags of[0,0xf0000000])for(const budget of[1,16,17,18,33,34,35,510]){
   seed(a,{tile:0,flags});seed(b,{tile:0,flags});check(a._ref_run(budget)===budget,'empty original');check(b._ref_semantic_run(budget)===budget,'empty selected');equal(a,b,'empty '+budget);cases++;
  }
  check(b.__semanticCalls>0&&!b.__semanticError,'positive semantic');
  const controls=[];
  function decline(name,config={},prepare=()=>{},budget=1000){
   seed(b,config);prepare();const before=JSON.stringify(state(b)),memory=b.HEAPU8.slice(b.data,b.data+0x80000);
   check(b._ref_semantic_direct(budget,0)===0,name+' count');check(JSON.stringify(state(b))===before,name+' state');
   assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,memory.length),Buffer.from(memory),name+' RAM');controls.push(name);
  }
  for(const cfg of [{mode:6},{format:12},{limit:1},{destination:base+0x10020},{destination:base+0x20000},{stack:base+0x30010}])decline(JSON.stringify(cfg),cfg);
  decline('short',{},()=>{},244);decline('irq',{},()=>b._ref_irq(1));b._ref_irq(0);
  decline('wrong-asid',{},()=>b._ref_asid(2));b._ref_asid(1);
  decline('changed-source',{},()=>{b.HEAPU8[b.code+0x32bc]^=1;b._ref_generation();});b.HEAPU8[b.code+0x32bc]^=1;
  for(const offset of [0x3490,0x39fc]){decline('changed-half-source-'+offset.toString(16),{mode:2},()=>{b.HEAPU8[b.code+offset]^=1;b._ref_generation();},4000);b.HEAPU8[b.code+offset]^=1;}
  decline('read-denied',{},()=>b._ref_deny(base+0x10000,base+0x11000,1));b._ref_deny(0,0,0);
  decline('write-denied',{},()=>b._ref_deny(base+0x30000,base+0x31000,2));b._ref_deny(0,0,0);
  decline('big-endian',{},()=>b._ref_cpsr(0x210));
  decline('wrong-pc',{},()=>b._ref_register(15,pc+4));
  seed(a);seed(b);b.data=b._ref_remap(base);check(a._ref_run(490)===490,'remap original');check(b._ref_semantic_run(490)===490,'remap candidate');equal(a,b,'remapped RAM');controls.push('remapped-data');
  seed(b);check(b._ref_semantic_direct(490,0)===490,'warm cache first');
  const resolves=b._eka2l1_semantic_resolutions();
  const reset=()=>{for(const[r,value]of[[5,0],[8,base+0x800],[12,base+0x400],[15,pc]])b._ref_register(r,value);};
  reset();check(b._ref_semantic_direct(490,0)===490,'warm cache second');check(b._eka2l1_semantic_resolutions()===resolves,'warm cache reused');controls.push('same-generation-cache');
  reset();b._ref_deny(base+0x30000,base+0x31000,2);const before=JSON.stringify(state(b)),ram=b.HEAPU8.slice(b.data,b.data+0x80000);
  check(b._ref_semantic_direct(490,0)===0,'revoked write');check(JSON.stringify(state(b))===before,'revoked state');
  assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,ram.length),Buffer.from(ram),'revoked memory');controls.push('warm-write-revocation');
  b._ref_deny(0,0,0);check(b._ref_semantic_direct(490,0)===490,'restored write');controls.push('warm-write-restoration');
  return {pass:true,cases,controls,stats:b.__semanticBindings};
 }finally{a._ref_destroy();b._ref_destroy();}
}

const report={pass:false,scope:'Original ARM CPU versus complete tile-dispatch backedges; exact registers, flags, stack and RAM at bounded instruction prefixes. No product speed claim.',inputs:{},errors:[]};
for(const p of [new URL(import.meta.url),helper,build+'/build.json',build+'/fixture.mjs'])report.inputs[String(p)]=sha(p);
try{Object.assign(report,await prove(create));}catch(error){report.errors.push(error.stack);}
fs.writeFileSync(output+'/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));if(!report.pass)process.exitCode=1;
