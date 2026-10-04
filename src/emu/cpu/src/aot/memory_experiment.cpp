#include <cpu/aot/memory_experiment.h>
#include <common/types.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, initialize_identity_js, (std::uint32_t *functions), {
    if (!globalThis.ekaIdentityMemory) {
        globalThis.ekaIdentityMemory = new WebAssembly.Memory({initial:65536, maximum:65536});
        const u = n => { const a=[]; do { const b=n&127; n>>>=7; a.push(b|(n?128:0)); } while(n); return a; };
        const str = s => [...u(s.length), ...Array.from(s, c=>c.charCodeAt(0))];
        const sec = (id, b) => [id, ...u(b.length), ...b];
        const body = (dst,src) => [0, 32,0, 32,1, 32,2, 252,10,dst,src, 11];
        const b0=body(1,0), b1=body(0,1);
        const bytes = [0,97,115,109,1,0,0,0,
            ...sec(1,[1,96,3,127,127,127,0]),
            ...sec(2,[2,...str('env'),...str('memory'),2,3,...u(256),...u(65536),
                ...str('env'),...str('guest'),2,1,...u(65536),...u(65536)]),
            ...sec(3,[2,0,0]),
            ...sec(7,[2,...str('input'),0,0,...str('output'),0,1]),
            ...sec(10,[2,...u(b0.length),...b0,...u(b1.length),...b1])];
        const instance = new WebAssembly.Instance(new WebAssembly.Module(new Uint8Array(bytes)),
            {env:{memory:wasmMemory,guest:globalThis.ekaIdentityMemory}});
        globalThis.ekaIdentityCopies = [addFunction(instance.exports.input,'viii'),addFunction(instance.exports.output,'viii')];
    }
    HEAPU32[functions>>>2]=globalThis.ekaIdentityCopies[0];
    HEAPU32[(functions>>>2)+1]=globalThis.ekaIdentityCopies[1];
});
EM_JS(void, activate_identity_js, (), {
    for(const [index,fn] of globalThis.ekaIdentityPending || [])
        WebAssembly.Table.prototype.set.call(wasmTable,index,fn);
    globalThis.ekaIdentityPending=[];
});
#endif

namespace eka2l1::arm::aot::memory_experiment {
    void activate_identity() {
#ifdef __EMSCRIPTEN__
        activate_identity_js();
#endif
        identity_active=true;
    }
    using copy_function = void (*)(std::uint32_t,std::uint32_t,std::uint32_t);
    static thread_local copy_function input = nullptr, output = nullptr;
    void initialize_identity_memory() {
#ifdef __EMSCRIPTEN__
        if (!input) {
            std::uint32_t functions[2]; initialize_identity_js(functions);
            input=reinterpret_cast<copy_function>(functions[0]);
            output=reinterpret_cast<copy_function>(functions[1]);
        }
#else
        throw std::runtime_error("Identity memory experiment requires WebAssembly multi-memory");
#endif
    }
    void copy_to_identity(std::uint32_t guest, std::uint32_t host, std::uint32_t size) { input(guest,host,size); }
    void copy_from_identity(std::uint32_t host, std::uint32_t guest, std::uint32_t size) { output(host,guest,size); }

    std::uintptr_t view::enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve) {
        if (active_) throw std::runtime_error("Nested experimental memory entry");
        const bool rebuilt=generation_ != generation || space_ != space;
        if (rebuilt) {
            bindings_ = resolve();
            std::sort(bindings_.begin(),bindings_.end(),[](auto a,auto b){return a.guest<b.guest;});
            ranges_.clear();
            for (auto b:bindings_) {
                if (!ranges_.empty()) {
                    auto &r=ranges_.back();
                    if (std::uint64_t(r.begin)+r.size==b.guest && std::uint64_t(r.host)+r.size==b.host
                            && r.permissions==b.permissions && r.size<=0xffffe000u) {
                        r.size+=4096; continue;
                    }
                }
                ranges_.push_back({b.guest,4096,b.host,b.permissions});
            }
            if (mode==1) {
                range_pages_.assign(1<<20,{});
                for (auto r:ranges_) for (std::uint32_t n=0;n<r.size;n+=4096)
                    range_pages_[(r.begin+n)>>12]=r;
            } else if (mode==3) {
                pages_.assign(1<<20,{});
                for(auto b:bindings_) pages_[b.guest>>12]={b.permissions&prot_read?b.host:0,b.permissions&prot_write?b.host:0};
            } else if(mode==2) {
                initialize_identity_memory();
                aliases_.assign(1<<20,0); dirty_.assign(1<<20,0);
                std::unordered_map<std::uint32_t,std::vector<std::uint32_t>> aliases;
                std::vector<std::uint32_t> hosts;
                for(auto b:bindings_) {aliases[b.host].push_back(b.guest);hosts.push_back(b.host);}
                std::sort(hosts.begin(),hosts.end());
                for(std::size_t n=1;n<hosts.size();++n) if(hosts[n]!=hosts[n-1] && std::uint64_t(hosts[n-1])+4096>hosts[n])
                    throw std::runtime_error("Identity experiment cannot represent partial-page physical aliases");
                stats.alias_pages=0;
                for(auto &item:aliases) if(item.second.size()>1) {
                    auto &addresses=item.second; stats.alias_pages+=addresses.size();
                    for(std::size_t n=0;n<addresses.size();++n)
                        aliases_[addresses[n]>>12]=addresses[(n+1)%addresses.size()];
                }
                refresh_ranges_.clear();
                for(auto r:ranges_) {
                    const auto first=r.begin>>12, count=r.size>>12;
                    if(r.host>=immutable_rom_begin && std::uint64_t(r.host)+r.size<=immutable_rom_end) continue;
                    if((r.permissions&prot_write) || std::any_of(aliases_.begin()+first,aliases_.begin()+first+count,[](auto next){return next!=0;}))
                        refresh_ranges_.push_back(r);
                }
                identity_={static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(aliases_.data())),
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dirty_.data()))};
            }
            generation_=generation; space_=space; ++stats.rebuilds;
            stats.mapped_pages=bindings_.size(); stats.ranges=ranges_.size(); stats.largest_range=0;
            for(auto r:ranges_) stats.largest_range=std::max(stats.largest_range,std::uint64_t(r.size));
        }
        active_=true;
        if(mode==2) {
            // Identity mode also assumes immutable read-only mappings. Refresh
            // mutable storage and aliases at every boundary; mapping changes
            // initialize all ranges. These copies are part of measured CPU work.
            for(auto r:rebuilt?ranges_:refresh_ranges_) {
                copy_to_identity(r.begin,r.host,r.size); stats.bytes_in+=r.size;
            }
            return reinterpret_cast<std::uintptr_t>(&identity_);
        }
        return mode==1?reinterpret_cast<std::uintptr_t>(range_pages_.data()):reinterpret_cast<std::uintptr_t>(pages_.data());
    }
    void view::leave() {
        if(!active_) return;
        if(mode==2) for(auto b:bindings_) if(dirty_[b.guest>>12]) {
            copy_from_identity(b.host,b.guest,4096); stats.bytes_out+=4096;
            dirty_[b.guest>>12]=0;
        }
        active_=false;
    }
}
