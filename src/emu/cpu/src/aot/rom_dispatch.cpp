#include <cpu/aot/rom_dispatch.h>
#include <cpu/aot/thumb_translator.h>
#include <numeric>

namespace eka2l1::arm::aot {
    rom_dispatch_map::rom_dispatch_map(std::uint32_t address, std::uint32_t length, std::uint32_t count)
        : base(address), size(length), functions(count),
          valid(length && count && count != UINT32_MAX && !(address & 4095)
              && std::uint64_t(address) + length <= (std::uint64_t{1} << 32)) {
        if (valid) {
            const auto pages = (std::uint64_t(size) + 4095) / 4096;
            pages_.resize(pages);
            pointers_.resize(pages);
        }
    }

    bool rom_dispatch_map::insert(std::uint32_t key, std::uint32_t index) {
        if (!valid || index >= functions || key < base || key - base >= size
            || (!(key & 1) && (key & 3))) return false;
        const auto offset = key - base;
        auto &entry = pages_[offset >> 12];
        if (!entry) {
            entry = std::make_unique<page>();
            pointers_[offset >> 12] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entry->data()));
        }
        auto &slot = (*entry)[offset & 4095];
        if (slot && slot != index + 1) return false;
        slot = index + 1;
        return true;
    }

    std::uint32_t rom_dispatch_map::lookup(std::uint32_t key) const {
        if (!valid || key < base || key - base >= size || (!(key & 1) && (key & 3))) return 0;
        const auto offset = key - base;
        const auto &entry = pages_[offset >> 12];
        return entry ? (*entry)[offset & 4095] : 0;
    }

    std::size_t rom_dispatch_map::bytes() const {
        std::size_t size = pointers_.size() * sizeof(std::uint32_t);
        for (const auto &entry : pages_) if (entry) size += sizeof(page);
        return size;
    }

    namespace {
        struct emitter {
            std::vector<std::uint8_t> &b;
            void op(unsigned v) { b.push_back(v); }
            void u(unsigned v) { do { auto c=v&127;v>>=7;op(c|(v?128:0)); } while(v); }
            void constant(std::uint32_t bits) {
                op(op_i32_const); auto value=static_cast<std::int32_t>(bits);
                for (;;) { auto c=value&127;value>>=7;bool done=(value==0&&!(c&64))||(value==-1&&(c&64));op(c|(done?0:128));if(done)break; }
            }
            void get(unsigned local) { op(op_local_get);u(local); }
            void set(unsigned local) { op(op_local_set);u(local); }
            void tee(unsigned local) { op(op_local_tee);u(local); }
            void load(unsigned offset) { get(0);op(op_i32_load);u(2);u(offset); }
            void store(unsigned offset,unsigned local) { get(0);get(local);op(op_i32_store);u(2);u(offset); }
            void store_const(unsigned offset,unsigned value) { get(0);constant(value);op(op_i32_store);u(2);u(offset); }
            void exit_if() { op(op_br_if);u(1); }
        };
    }

    std::vector<std::uint8_t> build_rom_dispatch_module(
        const std::vector<wasm_func_def> &functions,
        const std::vector<wasm_import_func> &imports,
        const rom_dispatch_map &map) {
        if (!map.valid || map.functions != functions.size()) return {};
        auto bodies = functions;
        wasm_func_def dispatcher;
        dispatcher.export_name = "rom_dispatch";
        dispatcher.private_export = true;
        dispatcher.num_locals = 8;
        for (auto &body : bodies) {
            if (body.export_name.substr(0,2) != "f_" || !body.export_aliases.empty()) return {};
            body.private_export = true;
            dispatcher.export_aliases.push_back(body.export_name);
        }
        using S = state_offsets;
        // Locals: original budget, total instructions, blocks, block limit,
        // normalized guest key, table slot, lookup scratch, returned count.
        constexpr unsigned BUDGET=1,TOTAL=2,BLOCKS=3,LIMIT=4,KEY=5,SLOT=6,TMP=7,COUNT=8;
        emitter w{dispatcher.body};
        w.load(S::AOT_BUDGET);w.set(BUDGET);
        w.load(S::AOT_REGIONS_LEFT);w.set(LIMIT);
        w.store_const(S::AOT_ROM_CALLBACK,0);
        w.op(op_block);w.op(type_void);
        w.op(op_loop);w.op(type_void);
        w.get(TOTAL);w.get(BUDGET);w.op(op_i32_ge_u);w.exit_if();
        w.get(BLOCKS);w.get(LIMIT);w.op(op_i32_ge_u);w.exit_if();

        w.load(S::PC);w.load(S::TFLAG);w.op(op_if);w.op(type_i32);
        w.constant(~1u);w.op(op_else);w.constant(~3u);w.op(op_end);
        w.op(op_i32_and);w.load(S::TFLAG);w.op(op_i32_or);w.set(KEY);
        w.get(KEY);w.constant(map.base);w.op(op_i32_lt_u);w.exit_if();
        w.get(KEY);w.constant(map.base);w.op(op_i32_sub);w.tee(TMP);
        w.constant(map.size);w.op(op_i32_ge_u);w.exit_if();
        w.get(TMP);w.constant(12);w.op(op_i32_shr_u);w.constant(2);w.op(op_i32_shl);
        w.constant(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(map.data())));w.op(op_i32_add);
        w.op(op_i32_load);w.u(2);w.u(0);w.tee(SLOT);w.op(op_i32_eqz);w.exit_if();
        w.get(SLOT);w.get(TMP);w.constant(4095);w.op(op_i32_and);w.constant(2);w.op(op_i32_shl);w.op(op_i32_add);
        w.op(op_i32_load);w.u(2);w.u(0);w.tee(SLOT);w.op(op_i32_eqz);w.exit_if();
        w.get(SLOT);w.constant(1);w.op(op_i32_sub);w.tee(SLOT);
        w.constant(map.functions);w.op(op_i32_ge_u);w.exit_if();

        w.get(KEY);w.constant(~1u);w.op(op_i32_and);w.set(TMP);w.store(S::PC,TMP);
        w.get(BUDGET);w.get(TOTAL);w.op(op_i32_sub);w.set(TMP);w.store(S::AOT_BUDGET,TMP);
        w.store_const(S::AOT_EXIT,0);
        w.get(0);w.get(SLOT);w.op(0x11);w.u(imports.size());w.u(0); // call_indirect, private table 0
        w.set(COUNT);
        w.get(COUNT);w.get(TMP);w.op(op_i32_gt_u);w.op(op_if);w.op(type_void);w.op(op_unreachable);w.op(op_end);
        w.get(BLOCKS);w.constant(1);w.op(op_i32_add);w.set(BLOCKS);
        w.get(TOTAL);w.get(COUNT);w.op(op_i32_add);w.set(TOTAL);
        w.get(COUNT);w.op(op_i32_eqz);w.op(op_if);w.op(type_void);
        w.store_const(S::AOT_ROM_CALLBACK,2);w.op(op_br);w.u(2);w.op(op_end);
        w.load(S::AOT_ROM_CALLBACK);w.load(S::AOT_EXIT);w.op(op_i32_or);w.exit_if();
        w.load(S::NUM_INSTRS_TO_EXECUTE);w.load(S::NUM_INSTRS_TO_EXECUTE+4);w.op(op_i32_or);w.op(op_i32_eqz);w.exit_if();
        w.load(S::NIRQ);w.op(op_i32_eqz);w.load(S::CPSR);w.constant(0x80);w.op(op_i32_and);w.op(op_i32_eqz);w.op(op_i32_and);w.exit_if();
        w.op(op_br);w.u(0);
        w.op(op_end);w.op(op_end);
        w.store(S::AOT_BUDGET,BUDGET);w.store(S::AOT_REGIONS_USED,BLOCKS);
        w.get(TOTAL);w.op(op_return);
        bodies.push_back(std::move(dispatcher));
        std::vector<std::uint32_t> table(functions.size());
        std::iota(table.begin(),table.end(),0u);
        return build_wasm_module(bodies,imports,table);
    }
}
