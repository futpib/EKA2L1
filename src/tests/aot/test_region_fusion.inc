#include <cpu/aot/wasm_cost.h>

#ifdef __EMSCRIPTEN__
EM_JS(double, fusion_operation_count, (int kind, bool clear), {
    globalThis.ekaTestCounts ||= new Float64Array(12);
    const value=globalThis.ekaTestCounts[kind];
    if(clear)globalThis.ekaTestCounts[kind]=0;
    return value;
});
#endif

static bool count_fusion_operations(wasm_func_def &function) {
    enum immediate {none,leb,pair,table,float32,float64,unknown};
    const auto &body=function.body;std::vector<std::uint8_t> counted;
    for(std::size_t pos=0;pos<body.size();) {
        const auto begin=pos;const unsigned opcode=body[pos++];
        immediate encoding=unknown;
#define WASM_COST(first,last,category,format) if(opcode>=first && opcode<=last)encoding=format;
#include <cpu/aot/wasm_cost_model.def>
#undef WASM_COST
        const auto read=[&]() {unsigned value=0,shift=0;while(pos<body.size()) {
            const auto byte=body[pos++];if(shift<32)value|=(byte&127)<<shift;shift+=7;if(!(byte&128))break;}return value;};
        switch(encoding) {
        case none:break;
        case leb:read();break;
        case pair:read();read();break;
        case table:{const auto n=read();for(unsigned i=0;i<=n;++i)read();break;}
        case float32:pos+=4;break;
        case float64:pos+=8;break;
        default:return false;
        }
        if(pos>body.size())return false;
        const auto category=wasm_cost::classify(opcode);
        if(category!=wasm_cost::kind::structural) {
            counted.insert(counted.end(),{op_i32_const,static_cast<std::uint8_t>(category),op_call,6});
        }
        counted.insert(counted.end(),body.begin()+begin,body.begin()+pos);
    }
    function.body=std::move(counted);function.sources.clear();return true;
}

// Compare whole compiled chains, including observable callback boundaries.
static bool test_region_fusion(bool instrument = false) {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    struct restore {
        bool direct=thumb_direct_memory, fusion=thumb_region_fusion;
        unsigned mode=memory_experiment::mode, unsafe=eka2l1::common::code_tracking::unsafe_code_mode;
        ~restore(){thumb_direct_memory=direct;thumb_region_fusion=fusion;
            memory_experiment::mode=mode;eka2l1::common::code_tracking::unsafe_code_mode=unsafe;
            g_test_mem=nullptr;g_memory_boundary={};}
    } saved;
    thumb_direct_memory=true;eka2l1::common::code_tracking::unsafe_code_mode=3;
    std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    if(instrument)imports.push_back({"env","eka_test_count",1,false});
    const std::vector<std::vector<std::uint16_t>> programs={
        {0x2800,0xd002,0x3001,0x4770,0x46c0,0x3002,0x4770},
        {0x6808,0x2800,0xd002,0x6008,0x4770,0x46c0,0x684a,0x3201,0x600a,0x4770},
        {0x2800,0xd003,0x3801,0xe7fb,0x46c0,0x46c0,0x3301,0x4770},
        {0xf000,0xf804,0x3301,0x4770,0x46c0,0x46c0,0x3001,0x4770},
        {0x2800,0xd002,0x3001,0x4770,0x46c0,0x2b00,0xd002,0x3301,0x4770,0x46c0,0x3401,0x4770},
        {0x6008,0xe001,0x46c0,0x46c0,0x680a,0x3201,0x600a,0x4770}
    };
    unsigned checks=0,independent=0,selected=0,less=0,equal=0,more=0;std::uint64_t calls[2]{};
    double operations[2][12]{};
    for(unsigned policy:{0u,2u})for(unsigned program=0;program<programs.size();++program) {
        memory_experiment::mode=policy;
        const auto &words=programs[program];
        const code_window rom{reinterpret_cast<const std::uint8_t*>(words.data()),0x1000,static_cast<unsigned>(words.size()*2)};
        std::map<unsigned,std::vector<std::uint8_t>> modules[2];
        for(unsigned variant=0;variant<2;++variant)for(unsigned off=0;off<rom.size;off+=2) {
            thumb_region_fusion=variant;
            auto tr=translate_thumb_block(rom.host+off,rom.size-off,rom.base+off,nullptr,nullptr,true,false,true,&rom);
            selected+=tr.fused_edges;
            if(instrument && !count_fusion_operations(tr.func))return false;
            modules[variant][rom.base+off]=build_wasm_module({tr.func},imports);
        }
        for(unsigned input:{0u,1u,5u,0xffffffffu})for(unsigned flags:{0u,5u,10u,15u})
        for(unsigned budget=0;budget<18;++budget)for(unsigned mapping:{0u,1u})for(unsigned event:{0u,1u,2u,3u}) {
            if(instrument && (event || flags==5 || flags==10 || (budget%3)))continue;
            double path_cost[2]{};unsigned path_calls[2]{};
            test_mem memories[2];memories[0].write32(0x8000,input);memories[0].write32(0x8004,input+1);memories[1]=memories[0];
            alignas(8) unsigned states[2][256]{};unsigned counts[2]{},helpers[2]{};
            std::vector<std::array<unsigned,22>> observations[2];
            for(unsigned variant=0;variant<2;++variant) {
                if(instrument)for(unsigned kind=0;kind<12;++kind)fusion_operation_count(kind,true);
                auto *state=states[variant];
                for(unsigned reg=0;reg<15;++reg)state[reg]=0x12345600+reg;
                state[0]=input;state[1]=0x8000;state[3]=input;state[13]=0x9000;state[14]=0x3001;state[15]=rom.base;
                state[S::CPSR/4]=0x30|(flags<<28);state[S::TFLAG/4]=1;
                state[S::NFLAG/4]=(flags>>3)&1;state[S::ZFLAG/4]=(flags>>2)&1;state[S::CFLAG/4]=(flags>>1)&1;state[S::VFLAG/4]=flags&1;
                state[S::NIRQ/4]=1;state[S::NUM_INSTRS_TO_EXECUTE/4]=100;
                r12l1::tlb tlb(12);if(mapping)tlb.add(0x8000,memories[variant].data.data()+0x8000,3);
                memory_experiment::view view;
                const auto pointer=view.enter(1,1,[&]{return std::vector<memory_experiment::binding>{{0x8000,
                    static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memories[variant].data.data()+0x8000)),mapping?3u:0u}};});
                state[S::AOT_TLB/4]=policy?static_cast<unsigned>(pointer):reinterpret_cast<std::uintptr_t>(tlb.entries);
                g_test_mem=&memories[variant];g_all_memory_helper_calls=0;
                g_memory_boundary=[&](bool after,unsigned ptr) {
                    auto *s=reinterpret_cast<unsigned*>(ptr);
                    if(after && event==1)s[S::NUM_INSTRS_TO_EXECUTE/4]=0;
                    if(after && event==2)s[S::NIRQ/4]=0;
                    if(after && event==3){s[3]=7;s[S::CFLAG/4]=1;s[S::ZFLAG/4]=0;}
                    std::array<unsigned,22> observation{};std::copy(s,s+16,observation.begin());
                    for(unsigned i=0;i<6;++i)observation[16+i]=s[std::array<unsigned,6>{S::NFLAG,S::ZFLAG,S::CFLAG,S::VFLAG,S::TFLAG,S::CPSR}[i]/4];
                    observations[variant].push_back(observation);
                };
                for(unsigned round=0;round<64 && counts[variant]<budget;++round) {
                    auto it=modules[variant].find(state[15]);
                    if(it==modules[variant].end() || !state[S::TFLAG/4] || !state[S::NUM_INSTRS_TO_EXECUTE/4]
                            || (!state[S::NIRQ/4] && !(state[S::CPSR/4]&0x80)))break;
                    state[S::AOT_BUDGET/4]=budget-counts[variant];
                    const auto n=js_run_aot_wasm(it->second.data(),it->second.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(states[variant]));
                    ++calls[variant];++path_calls[variant];if(n<0)return false;if(!n)break;counts[variant]+=n;
                    if(counts[variant]>budget)return false;
                }
                if(instrument)for(unsigned kind=0;kind<12;++kind) {
                    const auto n=fusion_operation_count(kind,false);operations[variant][kind]+=n;path_cost[variant]+=n;
                }
                helpers[variant]=g_all_memory_helper_calls;state[S::AOT_BUDGET/4]=budget;state[S::AOT_TLB/4]=0;
            }
            if(counts[0]!=counts[1] || std::memcmp(states[0],states[1],sizeof(states[0]))
                    || helpers[0]!=helpers[1] || memories[0].data!=memories[1].data || observations[0]!=observations[1]) {
                printf(" FAIL fusion policy=%u program=%u input=%u flags=%u budget=%u mapping=%u event=%u counts=%u/%u helpers=%u/%u\n",policy,program,input,flags,budget,mapping,event,counts[0],counts[1],helpers[0],helpers[1]);
                for(unsigned i=0;i<256;++i)if(states[0][i]!=states[1][i])printf(" state[%u] %x/%x\n",i,states[0][i],states[1][i]);return false;
            }
            if(!event && mapping && counts[1]) {
                test_mem reference;reference.write32(0x8000,input);reference.write32(0x8004,input+1);
                reference.write_code(rom.base,std::vector<std::uint8_t>(rom.host,rom.host+rom.size));
                r12l1::exclusive_monitor mon(1);auto cpu=make_cpu(reference,mon);
                for(unsigned reg=0;reg<15;++reg)cpu->set_reg(reg,0x12345600+reg);
                cpu->set_reg(0,input);cpu->set_reg(1,0x8000);cpu->set_reg(3,input);cpu->set_reg(13,0x9000);cpu->set_reg(14,0x3001);
                cpu->set_pc(rom.base);cpu->set_cpsr(0x30|(flags<<28));cpu->run(counts[1]);
                for(unsigned reg=0;reg<16;++reg)if(cpu->get_reg(reg)!=states[1][reg]) {
                    printf(" FAIL fusion interpreter program=%u budget=%u reg=%u %x/%x\n",program,budget,reg,cpu->get_reg(reg),states[1][reg]);return false;
                }
                const auto actual=capture_state(*cpu);
                if(actual.nflag!=states[1][S::NFLAG/4]||actual.zflag!=states[1][S::ZFLAG/4]
                    ||actual.cflag!=states[1][S::CFLAG/4]||actual.vflag!=states[1][S::VFLAG/4]
                    ||reference.read32(0x8000)!=memories[1].read32(0x8000))return false;
                ++independent;
            }
            if(instrument){
                less+=path_cost[1]<path_cost[0];equal+=path_cost[1]==path_cost[0];more+=path_cost[1]>path_cost[0];
                if(path_cost[1]>path_cost[0] && more<=8)printf(" GROWTH program=%u budget=%u input=%u mapping=%u cost=%.0f/%.0f calls=%u/%u\n",program,budget,input,mapping,path_cost[0],path_cost[1],path_calls[0],path_calls[1]);
            }
            ++checks;
        }
    }
    if(instrument) {
        printf(" FUSION operations: paths lower=%u equal=%u higher=%u\n",less,equal,more);
        for(unsigned kind=0;kind<12;++kind)printf(" category %u %.0f -> %.0f\n",kind,operations[0][kind],operations[1][kind]);
    }
    if(!selected || calls[1]>=calls[0])return false;
    printf(" PASS region fusion (%u chain/state/memory/callback checks; %u independent interpreter checks; calls %llu -> %llu; %u fused edges)\n",checks,independent,static_cast<unsigned long long>(calls[0]),static_cast<unsigned long long>(calls[1]),selected);
#endif
    return true;
}
