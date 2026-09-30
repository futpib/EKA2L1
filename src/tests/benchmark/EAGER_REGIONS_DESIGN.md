# Apply the existing region compiler to eager ROM exports

The eager ARM export path in aot_setup.cpp previously selected bounded basic
blocks. Hot ROM and RAM compilation selected the region compiler. Since
observe_hot_pc receives lookup misses, an already-installed eager export
does not graduate through that path. This is a coverage difference, not a
measurement of its performance cost.

EKA2L1_AOT_EAGER_REGIONS=1 now opts eager ARM candidates into the existing
bounded region compiler when chaining is enabled. Thumb translation, DLL
selection, instruction budgets, interrupts, memory guards, branch/resume
successor enumeration and immutable-ROM registry behavior remain the same.
The region path follows the build's deferred-memory setting and requested IR
policy. It does not inline callees or use game addresses. Normal startup
retains the old eager call. The browser API accepts only 0/1 before init.

The first comparison uses IR policy 0 throughout. Off/on variants use the
same archived application JS/WASM; a separately served build is an anchor.
This tests basic-block versus region coverage without mixing in experimental
IR lowering. Profile/replay reports record both policies; serial_variants.py
accepts `--eager-regions NAME=0/1` and clears inherited policy settings.

Startup logs record scan/translation time, module emission time, complete
module bytes and function count. They exclude browser compilation and
instantiation, which are additional costs. The existing per-DLL candidate
limit and 512-byte input cap remain. Bigger regions may increase startup or
code size, duplicate work at multiple entries, or slow the workload.

Validation requires native/frontend checks, the existing full compiler/fault
coverage, and a fresh checked native image/audio replay for the actual eager
path. Existing region tests alone do not establish that the changed boot
export graph runs correctly. Serial timings follow acceptance; no promotion
without repeatable gameplay benefit and live/audio acceptance.
