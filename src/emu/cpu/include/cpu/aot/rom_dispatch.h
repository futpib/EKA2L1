#pragma once

#include <cpu/aot/wasm_emitter.h>
#include <array>
#include <memory>
#include <vector>

namespace eka2l1::arm::aot {
    // Immutable host-owned index. A mode-tagged guest address names a private
    // function-table slot, never an arbitrary host pointer or RAM translation.
    class rom_dispatch_map {
        using page = std::array<std::uint32_t, 4096>;
        std::vector<std::unique_ptr<page>> pages_;
        std::vector<std::uint32_t> pointers_;
    public:
        const std::uint32_t base, size, functions;
        const bool valid;
        rom_dispatch_map(std::uint32_t base, std::uint32_t size, std::uint32_t functions);
        bool insert(std::uint32_t key, std::uint32_t index);
        std::uint32_t lookup(std::uint32_t key) const;
        const std::uint32_t *data() const { return pointers_.data(); }
        std::size_t bytes() const;
    };

    // Composition preserves each original block as a separately budgeted step.
    // Unsupported emission forms and singleton groups keep their original body.
    std::vector<std::uint8_t> build_rom_cohort_module(
        const std::vector<wasm_func_def> &functions,
        const std::vector<wasm_import_func> &imports,
        std::uint32_t base, std::uint32_t size,
        std::shared_ptr<rom_dispatch_map> &map,
        unsigned *composed_entries = nullptr);

    // The map must outlive all exported functions from the returned module.
    // Original functions are private and never call the dispatcher recursively.
    std::vector<std::uint8_t> build_rom_dispatch_module(
        const std::vector<wasm_func_def> &functions,
        const std::vector<wasm_import_func> &imports,
        const rom_dispatch_map &map);
}
