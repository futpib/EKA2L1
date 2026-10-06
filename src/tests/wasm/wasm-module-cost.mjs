// Inventory generated modules using the same instruction decoder as the counter.
// Call operands are module-local indices: normalize them before comparing builds.
import crypto from 'node:crypto';
import {instruction,emptyCost} from './wasm-cost-model.mjs';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function reader(bytes) {
    return {bytes,pos:0,u(){let n=0,shift=0;for(let i=0;i<5;++i){const b=this.byte();n|=(b&127)<<shift;if(!(b&128))return n>>>0;shift+=7;}throw Error('Overlong u32');},
        byte(){if(this.pos>=bytes.length)throw Error('Truncated WASM');return bytes[this.pos++];},
        take(n){if(n>bytes.length-this.pos)throw Error('Truncated WASM');const b=bytes.subarray(this.pos,this.pos+n);this.pos+=n;return b;},
        string(){return Buffer.from(this.take(this.u())).toString('utf8');},
        limits(){const flags=this.u();if(flags&~3)throw Error('Unsupported memory limits');this.u();if(flags&1)this.u();}};
}
export function moduleCosts(bytes) {
    if(Buffer.from(bytes.subarray(0,8)).toString('hex')!=='0061736d01000000')throw Error('Bad WASM header');
    const file=reader(bytes);file.pos=8;
    const imports=[],exports=new Map(),bodies=[];
    while(file.pos<bytes.length) {
        const id=file.byte(),section=reader(file.take(file.u()));
        if(id===2)for(let i=section.u();i;--i) {
            const module=section.string(),name=section.string(),kind=section.byte();
            if(kind===0){imports.push(module+'.'+name);section.u();}
            else if(kind===1){section.byte();section.limits();}
            else if(kind===2)section.limits();
            else if(kind===3){section.byte();section.byte();}
            else throw Error('Unsupported import kind');
        }
        if(id===7)for(let i=section.u();i;--i) {
            const name=section.string(),kind=section.byte(),index=section.u();
            if(kind===0)exports.set(index,name);
        }
        if(id===10)for(let i=section.u();i;--i)bodies.push(section.take(section.u()));
    }
    const memo=new Map(),pending=new Set();
    function analyze(index) {
        if(memo.has(index))return memo.get(index);
        if(pending.has(index))throw Error('Recursive private callee needs explicit accounting');
        pending.add(index);
        const body=bodies[index-imports.length];if(!body)throw Error('Missing WASM function body');
        const r=reader(body);let locals=0;
        for(let n=r.u();n;--n){locals+=r.u();r.byte();}
        const canonical=crypto.createHash('sha256');canonical.update(body.subarray(0,r.pos));
        const classes=emptyCost();let operations=0;
        while(r.pos<body.length) {
            const op=instruction(body,r.pos);r.pos=op.end;
            if(op.opcode===0x10) {
                const target=Number(op.immediates[0]);
                const identity=target<imports.length?'import:'+imports[target]:exports.has(target)
                    ?'export:'+exports.get(target):'private:'+analyze(target).normalized_sha256;
                canonical.update(JSON.stringify(['call',identity]));
            } else canonical.update(body.subarray(op.start,op.end));
            if(op.operations){++operations;++classes[op.category];}
        }
        const result={normalized_sha256:canonical.digest('hex'),raw_sha256:hash(body),
            body_bytes:body.length,locals,operations,classes};
        memo.set(index,result);pending.delete(index);return result;
    }
    return Object.fromEntries([...exports].filter(([index,name])=>index>=imports.length&&name.startsWith('f_'))
        .map(([index,name])=>[name,analyze(index)]));
}
