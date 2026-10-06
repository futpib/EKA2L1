// The compiler and test counter consume one opcode/cost-class specification.
import fs from 'node:fs';
export const classes = ['structural','local','constant','load','store','control','call','global','memory_size','arithmetic','division','unknown'];
const source = fs.readFileSync(new URL('../../emu/cpu/include/cpu/aot/wasm_cost_model.def', import.meta.url),'utf8');
export const model = Array(256).fill(null);
for (const [,first,last,category,immediate] of source.matchAll(/WASM_COST\((0x[\da-f]+), (0x[\da-f]+), (\w+), (\w+)\)/g)) {
    if (!classes.includes(category)) throw Error(`Unknown cost class ${category}`);
    for (let opcode=Number(first);opcode<=Number(last);++opcode) {
        if (model[opcode]) throw Error(`Duplicate cost entry ${opcode}`);
        model[opcode] = {category,immediate,operations:category==='structural'?0:1};
    }
}
export function instruction(bytes, offset) {
    const start=offset,opcode=bytes[offset++],entry=model[opcode];
    if (!entry) throw Error(`Unaccounted WASM opcode 0x${opcode?.toString(16)} at ${start}`);
    const immediates=[];
    const leb=()=>{
        let value=0n,shift=0n;
        for(let i=0;i<10;++i) {
            if(offset>=bytes.length)throw Error('Truncated WASM immediate');
            const byte=bytes[offset++];value|=BigInt(byte&127)<<shift;
            if(!(byte&128)){immediates.push(value);return Number(value);}
            shift+=7n;
        }
        throw Error('Overlong WASM immediate');
    };
    switch(entry.immediate) {
    case 'none':break;
    case 'leb':leb();break;
    case 'pair':leb();leb();break;
    case 'table':{const n=leb();if(n>bytes.length-offset)throw Error('Truncated branch table');for(let i=0;i<=n;++i)leb();break;}
    case 'float32':offset+=4;break;
    case 'float64':offset+=8;break;
    default:throw Error(`Unknown immediate format ${entry.immediate}`);
    }
    if(offset>bytes.length)throw Error('Truncated WASM immediate');
    return {start,end:offset,opcode,...entry,immediates};
}
export function emptyCost() { return Object.fromEntries(classes.filter(c=>c!=='structural'&&c!=='unknown').map(c=>[c,0])); }
export function staticCost(bytes) {
    const result=emptyCost();let operations=0;
    for(let offset=0;offset<bytes.length;) {
        const op=instruction(bytes,offset);offset=op.end;
        if(op.operations){++operations;++result[op.category];}
    }
    return {operations,classes:result,bytes:bytes.length,scope:'emitted WASM inventory, not executed path cost'};
}

// Aggregate corresponding executed paths without conflating local/constant
// savings with removed memory/control work. Consumers still check semantics.
export class CostComparisons {
    fixtures={};
    add(name,before,after,context={}) {
        const row=this.fixtures[name]??={paths:0,operation_growth:0,reduced:0,
            class_growth:emptyCost(),worst_class_delta:emptyCost(),growth_examples:{}};
        ++row.paths;
        row.operation_growth+=after.operations>before.operations;
        row.reduced+=after.operations<before.operations;
        for(const category of Object.keys(row.class_growth)) {
            const delta=after.classes[category]-before.classes[category];
            if(!Number.isSafeInteger(delta))throw Error('Cost breakdown is incomplete');
            row.class_growth[category]+=delta>0;
            row.worst_class_delta[category]=Math.max(row.worst_class_delta[category],delta);
            if(delta>0&&!row.growth_examples[category])row.growth_examples[category]={context,before,after};
        }
    }
}
