#include <cpu/aot/memory_experiment.h>
#include <common/types.h>
#include <algorithm>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
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
    std::uintptr_t view::enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve, range direct) {
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
            } else if (mode==3 || mode==2) {
                pages_.assign(1<<20,{});
                for(auto b:bindings_) pages_[b.guest>>12]={b.permissions&prot_read?b.host:0,b.permissions&prot_write?b.host:0};
            }
            if (mode==2) {
                // External mappings/remaps inside the arena invalidate the affine
                // shortcut. The canonical page table still handles those pages.
                stats.direct_pages=0;
                for (auto b:bindings_) if (b.guest>=direct.begin && std::uint64_t(b.guest)<std::uint64_t(direct.begin)+direct.size) {
                    if (b.host != direct.host+b.guest-direct.begin) { direct.size=0; break; }
                    ++stats.direct_pages;
                }
                if (!direct.size) stats.direct_pages=0;
                else ++stats.direct_rebuilds;
                direct_={direct.begin,direct.size,direct.host,
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pages_.data())),
                    direct.host-direct_begin,
                    direct.begin==direct_begin && direct.size==direct_size ? ~0u : 0u};
            }
            generation_=generation; space_=space; ++stats.rebuilds;
            stats.mapped_pages=bindings_.size(); stats.ranges=ranges_.size(); stats.largest_range=0;
            for(auto r:ranges_) stats.largest_range=std::max(stats.largest_range,std::uint64_t(r.size));
        }
        active_=true;
        if(mode==2) return reinterpret_cast<std::uintptr_t>(&direct_);
        return mode==1?reinterpret_cast<std::uintptr_t>(range_pages_.data()):reinterpret_cast<std::uintptr_t>(pages_.data());
    }
    void view::leave() {
        if(!active_) return;
        active_=false;
    }
}
