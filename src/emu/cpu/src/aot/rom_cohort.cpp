#include <cpu/aot/rom_dispatch.h>
#include <cpu/aot/thumb_translator.h>
#include <algorithm>
#include <map>
#include <set>

namespace eka2l1::arm::aot {
namespace {
    using S = state_offsets;
    struct emitter {
        std::vector<std::uint8_t> &b;
        void op(unsigned v) { b.push_back(v); }
        void u(unsigned v) { do { auto c=v&127;v>>=7;op(c|(v?128:0)); } while(v); }
        void constant(std::uint32_t bits) {
            op(op_i32_const); auto v=static_cast<std::int32_t>(bits);
            for (;;) { auto c=v&127;v>>=7;bool end=(v==0&&!(c&64))||(v==-1&&(c&64));op(c|(end?0:128));if(end)break; }
        }
        void get(unsigned l) { op(op_local_get);u(l); }
        void set(unsigned l) { op(op_local_set);u(l); }
        void tee(unsigned l) { op(op_local_tee);u(l); }
        void load(unsigned o) { get(0);op(op_i32_load);u(2);u(o); }
        void store(unsigned o,unsigned l) { get(0);get(l);op(op_i32_store);u(2);u(o); }
        void put(unsigned o,unsigned v) { get(0);constant(v);op(op_i32_store);u(2);u(o); }
        void exit_if() { op(op_br_if);u(1); }
    };
    bool shared(unsigned offset) {
        return (offset<S::PC && !(offset&3)) || offset==S::NFLAG || offset==S::ZFLAG
            || offset==S::CFLAG || offset==S::VFLAG || offset==S::TFLAG;
    }
    // This decoder handles only the core instructions our emitter produces.
    // Unknown forms reject composition and retain the ordinary finalized body.
    struct instruction { unsigned opcode=0, immediate=0;std::size_t end=0; };
    bool decode(const std::vector<std::uint8_t> &body,std::size_t at,instruction &ins) {
        if(at>=body.size())return false;
        ins.opcode=body[at++];
        auto leb=[&](unsigned &out,unsigned limit=5) {
            out=0;for(unsigned i=0;i<limit;++i) {if(at>=body.size())return false;
                auto byte=body[at++];if(i<5)out|=unsigned(byte&127)<<(7*i);
                if(!(byte&128))return true;}return false;
        };
        unsigned ignored=0;
        switch(ins.opcode) {
        case op_block:case op_loop:case op_if:
            if(at>=body.size() || (body[at]!=type_void&&body[at]!=type_i32&&body[at]!=type_i64
                &&body[at]!=type_f32&&body[at]!=type_f64))return false;
            ++at;break;
        case op_br:case op_br_if:case op_call:case op_local_get:case op_local_set:case op_local_tee:
            if(!leb(ins.immediate))return false;break;
        case op_br_table:
            if(!leb(ins.immediate)||ins.immediate>body.size())return false;
            for(unsigned n=0;n<=ins.immediate;++n)if(!leb(ignored))return false;break;
        case op_i32_const:if(!leb(ignored))return false;break;
        case op_i64_const:if(!leb(ignored,10))return false;break;
        case op_f32_const:at+=4;break;
        case op_f64_const:at+=8;break;
        default:
            if(ins.opcode>=0x28&&ins.opcode<=0x3e) {if(!leb(ignored)||!leb(ignored))return false;}
            else if(!((ins.opcode>=0x45&&ins.opcode<=0xc4)||ins.opcode==op_unreachable
                ||ins.opcode==op_else||ins.opcode==op_end||ins.opcode==op_return
                ||ins.opcode==op_drop||ins.opcode==op_select))return false;
        }
        ins.end=at;return at<=body.size();
    }
    bool eligible(const wasm_func_def &f,unsigned imports) {
        if(!f.cached_body||f.cached_body->shared_return||f.outlined_callee||!f.outlined_calls.empty()
            ||f.num_prefix_i64_locals||f.num_suffix_i64_locals||!f.export_aliases.empty())return false;
        const auto &m=*f.cached_body;
        for(std::size_t at=0;at<m.body.size();) {
            instruction ins;if(!decode(m.body,at,ins))return false;
            if(ins.opcode==op_call) {
                if(ins.immediate>=imports)return false;
                bool flush=false,reload=false;
                for(const auto &point:m.barriers) {
                    flush|=point.position==at&&!point.reload;
                    reload|=point.position==ins.end&&point.reload;
                }
                if(!flush||!reload)return false;
            }
            at=ins.end;
        }
        return true;
    }
    constexpr unsigned BUDGET=1,TOTAL=2,BLOCKS=3,LIMIT=4,KEY=5,SLOT=6,TMP=7,COUNT=8;
    struct layout {
        unsigned scratch=0,integers=0,floats=0,doubles=0;
        std::map<unsigned,unsigned> guest;
        std::set<unsigned> written;
        unsigned local(const wasm_func_def &f,unsigned old) const {
            if(!old)return 0;
            for(const auto &[offset,slot]:f.cached_body->locals)
                if(slot==old&&shared(offset))return guest.at(offset);
            if(old<=f.num_locals)return COUNT+old;
            old-=f.num_locals+1;
            if(old<f.num_f32_locals)return integers+1+old;
            return integers+1+floats+old-f.num_f32_locals;
        }
        void transfer(emitter &w,bool reload) const {
            for(const auto &[offset,slot]:guest) {
                if(!reload&&!written.count(offset))continue;
                if(reload){w.load(offset);w.set(slot);}else w.store(offset,slot);
            }
        }
        void private_transfer(emitter &w,const wasm_func_def &f,bool reload) const {
            for(const auto &[offset,slot]:f.cached_body->locals) {
                if(shared(offset)||(!reload&&!f.cached_body->written.count(offset)))continue;
                const auto target=local(f,slot);
                if(reload){w.load(offset);w.set(target);}else w.store(offset,target);
            }
        }
    };
    bool append_region(emitter &w,const wasm_func_def &f,const layout &l,unsigned imports) {
        const auto &m=*f.cached_body;
        std::set<unsigned> cached;
        for(const auto &[offset,slot]:m.locals)cached.insert(slot);
        // A standalone WASM invocation starts with zeroed scratch locals.
        // Reusing one local bank must preserve that on every constituent entry.
        for(unsigned old=1;old<=f.num_locals;++old)if(!cached.count(old)) {w.constant(0);w.set(l.local(f,old));}
        for(unsigned old=0;old<f.num_f32_locals;++old) {
            w.op(op_f32_const);for(unsigned b=0;b<4;++b)w.op(0);w.set(l.integers+1+old);
        }
        for(unsigned old=0;old<f.num_f64_locals;++old) {
            w.op(op_f64_const);for(unsigned b=0;b<8;++b)w.op(0);w.set(l.integers+1+l.floats+old);
        }
        l.private_transfer(w,f,true);
        w.op(op_block);w.op(type_i32);
        unsigned depth=0;std::size_t barrier=0;
        for(std::size_t at=0;at<m.body.size();) {
            instruction ins;if(!decode(m.body,at,ins))return false;
            while(barrier<m.barriers.size()&&m.barriers[barrier].position==at) {
                const auto &point=m.barriers[barrier++];
                // Original return barriers publish runtime fields, but guest
                // registers remain live until the whole group exits.
                if(ins.opcode!=op_return||point.reload)l.transfer(w,point.reload);
                l.private_transfer(w,f,point.reload);
            }
            if(barrier<m.barriers.size()&&m.barriers[barrier].position<at)return false;
            if(ins.opcode==op_local_get||ins.opcode==op_local_set||ins.opcode==op_local_tee) {
                if(ins.immediate>f.num_locals+f.num_f32_locals+f.num_f64_locals)return false;
                w.op(ins.opcode);w.u(l.local(f,ins.immediate));
            } else if(ins.opcode==op_return) {w.op(op_br);w.u(depth);}
            else {
                if(ins.opcode==op_call&&ins.immediate>=imports)return false;
                w.b.insert(w.b.end(),m.body.begin()+at,m.body.begin()+ins.end);
            }
            if(ins.opcode==op_block||ins.opcode==op_loop||ins.opcode==op_if)++depth;
            if(ins.opcode==op_end){if(!depth)return false;--depth;}
            at=ins.end;
        }
        if(depth||barrier!=m.barriers.size())return false;
        w.op(op_end);w.set(COUNT);return true;
    }
    bool compose(const std::vector<wasm_func_def> &functions,unsigned first,unsigned count,
        const std::vector<wasm_import_func> &imports,const rom_dispatch_map &map,wasm_func_def &out) {
        layout l;
        l.guest.emplace(S::TFLAG,0);
        for(unsigned i=first;i<first+count;++i) {
            const auto &f=functions[i];if(!eligible(f,imports.size()))return false;
            l.scratch=std::max(l.scratch,f.num_locals);l.floats=std::max(l.floats,f.num_f32_locals);
            l.doubles=std::max(l.doubles,f.num_f64_locals);
            for(const auto &[offset,slot]:f.cached_body->locals)if(shared(offset))l.guest.emplace(offset,0);
            for(auto offset:f.cached_body->written)if(shared(offset))l.written.insert(offset);
        }
        l.integers=COUNT+l.scratch;
        for(auto &[offset,slot]:l.guest)slot=++l.integers;
        out.export_name="cohort_"+std::to_string(first);out.private_export=true;
        out.num_locals=l.integers;out.num_f32_locals=l.floats;out.num_f64_locals=l.doubles;
        for(unsigned i=first;i<first+count;++i)out.export_aliases.push_back(functions[i].export_name);
        emitter w{out.body};
        l.transfer(w,true);w.load(S::AOT_BUDGET);w.set(BUDGET);w.load(S::AOT_REGIONS_LEFT);w.set(LIMIT);
        w.put(S::AOT_ROM_CALLBACK,0);
        w.op(op_block);w.op(type_void);w.op(op_loop);w.op(type_void);
        w.get(TOTAL);w.get(BUDGET);w.op(op_i32_ge_u);w.exit_if();
        w.get(BLOCKS);w.get(LIMIT);w.op(op_i32_ge_u);w.exit_if();
        w.load(S::PC);w.get(l.guest.at(S::TFLAG));w.op(op_if);w.op(type_i32);
        w.constant(~1u);w.op(op_else);w.constant(~3u);w.op(op_end);w.op(op_i32_and);
        w.get(l.guest.at(S::TFLAG));w.op(op_i32_or);w.set(KEY);
        w.get(KEY);w.constant(map.base);w.op(op_i32_lt_u);w.exit_if();
        w.get(KEY);w.constant(map.base);w.op(op_i32_sub);w.tee(TMP);w.constant(map.size);w.op(op_i32_ge_u);w.exit_if();
        w.get(TMP);w.constant(12);w.op(op_i32_shr_u);w.constant(2);w.op(op_i32_shl);
        w.constant(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(map.data())));w.op(op_i32_add);
        w.op(op_i32_load);w.u(2);w.u(0);w.tee(SLOT);w.op(op_i32_eqz);w.exit_if();
        w.get(SLOT);w.get(TMP);w.constant(4095);w.op(op_i32_and);w.constant(2);w.op(op_i32_shl);w.op(op_i32_add);
        w.op(op_i32_load);w.u(2);w.u(0);w.constant(first+1);w.op(op_i32_sub);w.tee(SLOT);
        w.constant(count);w.op(op_i32_ge_u);w.exit_if();
        w.get(KEY);w.constant(~1u);w.op(op_i32_and);w.set(TMP);w.store(S::PC,TMP);
        w.get(BUDGET);w.get(TOTAL);w.op(op_i32_sub);w.set(TMP);w.store(S::AOT_BUDGET,TMP);w.put(S::AOT_EXIT,0);
        // Nested blocks implement a bounded switch without recursive calls.
        w.op(op_block);w.op(type_void);
        for(unsigned i=0;i<count;++i){w.op(op_block);w.op(type_void);}
        w.get(SLOT);w.op(op_br_table);w.u(count);
        for(unsigned i=0;i<count;++i)w.u(i);w.u(count);
        for(unsigned i=0;i<count;++i) {
            w.op(op_end);
            if(!append_region(w,functions[first+i],l,imports.size()))return false;
            w.op(op_br);w.u(count-1-i);
        }
        w.op(op_end);
        w.get(COUNT);w.get(TMP);w.op(op_i32_gt_u);w.op(op_if);w.op(type_void);w.op(op_unreachable);w.op(op_end);
        w.get(BLOCKS);w.constant(1);w.op(op_i32_add);w.set(BLOCKS);
        w.get(TOTAL);w.get(COUNT);w.op(op_i32_add);w.set(TOTAL);
        w.get(COUNT);w.op(op_i32_eqz);w.op(op_if);w.op(type_void);w.put(S::AOT_ROM_CALLBACK,2);w.op(op_br);w.u(2);w.op(op_end);
        w.load(S::AOT_ROM_CALLBACK);w.load(S::AOT_EXIT);w.op(op_i32_or);w.exit_if();
        w.load(S::NUM_INSTRS_TO_EXECUTE);w.load(S::NUM_INSTRS_TO_EXECUTE+4);w.op(op_i32_or);w.op(op_i32_eqz);w.exit_if();
        w.load(S::NIRQ);w.op(op_i32_eqz);w.load(S::CPSR);w.constant(0x80);w.op(op_i32_and);w.op(op_i32_eqz);w.op(op_i32_and);w.exit_if();
        w.op(op_br);w.u(0);w.op(op_end);w.op(op_end);
        l.transfer(w,false);w.store(S::AOT_BUDGET,BUDGET);w.store(S::AOT_REGIONS_USED,BLOCKS);w.get(TOTAL);w.op(op_return);
        return true;
    }
}

std::vector<std::uint8_t> build_rom_cohort_module(
    const std::vector<wasm_func_def> &input,const std::vector<wasm_import_func> &imports,
    std::uint32_t base,std::uint32_t size,std::shared_ptr<rom_dispatch_map> &map,unsigned *composed_entries) {
    if(composed_entries)*composed_entries=0;
    std::map<unsigned,unsigned> indices;
    for(unsigned i=0;i<input.size();++i) {
        if(input[i].export_name.substr(0,2)!="f_"||!input[i].export_aliases.empty())return {};
        const auto key=std::stoul(input[i].export_name.substr(2));
        if(!indices.emplace(key,i).second)return {};
    }
    std::vector<bool> used(input.size());std::vector<wasm_func_def> functions;
    std::vector<std::pair<unsigned,unsigned>> groups;
    for(unsigned seed=0;seed<input.size();++seed)if(!used[seed]) {
        std::vector<unsigned> group{seed};used[seed]=true;
        std::size_t bytes=input[seed].body.size();
        if(eligible(input[seed],imports.size()))
            for(unsigned at=0;at<group.size()&&group.size()<16;++at)
                for(auto key:input[group[at]].successor_keys) {
                    auto it=indices.find(key);if(it==indices.end()||used[it->second])continue;
                    const auto next=it->second;
                    if(group.size()>=16||bytes+input[next].body.size()>65536||!eligible(input[next],imports.size()))continue;
                    used[next]=true;group.push_back(next);bytes+=input[next].body.size();
                }
        groups.emplace_back(functions.size(),group.size());
        for(auto index:group)functions.push_back(input[index]);
    }
    map=std::make_shared<rom_dispatch_map>(base,size,functions.size());
    if(!map->valid)return {};
    for(unsigned i=0;i<functions.size();++i)
        if(!map->insert(std::stoul(functions[i].export_name.substr(2)),i))return {};
    std::vector<wasm_func_def> bodies;
    for(const auto &[first,count]:groups) {
        wasm_func_def body;
        if(count>1&&compose(functions,first,count,imports,*map,body)) {
            if(composed_entries)*composed_entries+=count;
            bodies.push_back(std::move(body));
        } else for(unsigned i=first;i<first+count;++i)bodies.push_back(functions[i]);
    }
    return build_wasm_module(bodies,imports);
}
}
