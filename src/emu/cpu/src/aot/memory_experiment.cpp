#include <cpu/aot/memory_experiment.h>
#include <common/types.h>
#include <stdexcept>

namespace eka2l1::arm::aot::memory_experiment {
    std::uintptr_t view::enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve, range direct) {
        if (active_) throw std::runtime_error("Nested experimental memory entry");
        const bool rebuilt=generation_ != generation || space_ != space;
        if (rebuilt) {
            // Only pages published by the previous view can contain a mapping.
            // Avoid clearing the entire 8 MiB table on every process switch.
            if (pages_.empty()) pages_.resize(1<<20);
            else for (auto b:bindings_) pages_[b.guest>>12]={};
            bindings_ = resolve();
            for(auto b:bindings_) pages_[b.guest>>12]={b.permissions&prot_read?b.host:0,b.permissions&prot_write?b.host:0};
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
                direct.host-direct_begin};
            const bool affine = direct.begin==direct_begin && direct.size>0 && direct.size<=direct_size;
            // Scalars, list nodes and ARM register transfers fit this guard.
            // Its conservative end uses canonical pages for the final bytes.
            direct_.arena_start_limit = affine && direct.size >= 64 ? direct.size - 63 : 0;
            direct_.return_limit4 = affine && direct.size >= 4 ? direct.size - 3 : 0;
            direct_.return_limit8 = affine && direct.size >= 8 ? direct.size - 7 : 0;
            generation_=generation; space_=space; ++stats.rebuilds;
            stats.mapped_pages=bindings_.size();
        }
        active_=true;
        return reinterpret_cast<std::uintptr_t>(&direct_);
    }
    void view::leave() {
        if(!active_) return;
        active_=false;
    }
}
