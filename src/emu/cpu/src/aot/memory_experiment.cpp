#include <cpu/aot/memory_experiment.h>
#include <common/types.h>
#include <stdexcept>
#include <algorithm>

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
            if (direct.banks) {
                std::copy_n(direct.banks, bank_count, banks_.begin());
                for (auto b : bindings_) if (b.guest - direct_begin < direct.size) {
                    auto &bank = banks_[(b.guest - direct_begin) >> bank_shift];
                    if (!bank.end || b.host != bank.bias + b.guest) bank = {};
                    else ++stats.direct_pages;
                }
                // Cross-bank proofs stop at a backing conflict or a segment
                // boundary. Existing pointers and physical aliases stay valid.
                for (unsigned i = bank_count; i-- > 0;) {
                    auto &bank = banks_[i];
                    if (!bank.end) continue;
                    const auto boundary = direct_begin + ((i + 1) << bank_shift);
                    bank.end = std::min(bank.end, i + 1 < bank_count && banks_[i + 1].end && banks_[i + 1].bias == bank.bias
                        ? banks_[i + 1].end : boundary);
                }
                direct_ = {direct_begin, direct.size, 0,
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pages_.data())), 0,
                    direct.size ? ~0u : 0u,
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(banks_.data()))};
                direct_.bias = banks_[0].bias;
                direct_.root_end = banks_[0].end;
                const auto root_size = direct_.root_end ? direct_.root_end - direct_begin : 0;
                direct_.root_limit1 = root_size;
                direct_.root_limit2 = root_size >= 2 ? root_size - 1 : 0;
                direct_.root_limit4 = root_size >= 4 ? root_size - 3 : 0;
                if (direct.size) ++stats.direct_rebuilds;
            } else {
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
