# Pending independent guard-interval publication experiment

The mode-3 source audit finds that lookup still publishes two host interval fields on every compiled entry although all generated interval readers are omitted at translation time. The detail-free profile recorded 18–20% of busy-worker self samples in outer execution, but does not isolate the two stores. This experiment must establish whether removing them helps, not assign that whole fraction to them.

Introduce an off-by-default, pre-initialization `omit_guard_publication` selector, with configure/report APIs. Select a templated publication path once at runner entry; no per-store generated test. The option has an effect only for policies 2/3, whose translated modules have no interval readers. Policies 0/1 retain publication even with the option enabled. Keep lookup validity, ASIDs, mapping generations, callbacks, permissions, budgets, interrupts and scheduling fixed.

The source audit includes startup ROM exports, hot ROM/RAM, profile-guided Thumb and current IR paths. Tests must exercise the real lookup and runner with sentinel interval values, normal and detailed runners, both option values and all four executable-byte policies. Keep exact stale-code incompatibility of mode 3 unchanged. Exact normal and checked native standard/longer image/audio replays must verify the selected option, mode and current literal feature 128. Review emitted runtime disassembly: omitted specialization must not publish offsets 856/860; retain the compatibility specialization. Record extra static WASM bytes from duplicated runner specializations.

After correctness, freeze a new archive. Compare candidate and matching control plus untouched current delivered archive, with lookup fixed to the reviewed result of the currently running independent lookup panel. All original limits, literal fusion and mode 3 remain fixed. Predeclare 24 observations over both routes and two reordered batches with all modes moved between inner/outer positions. Retain all samples and passive host observation; no concurrent owned builds, profiles or diagnostics. Require normal/live/audio/HTTPS checks before any promotion. No Git push.

This plan is preparatory: no implementation or gain is claimed. The current outlined-lookup timing queue takes precedence and must finish unmodified.

The lookup panel is now complete and not promoted. The next comparison fixes
lookup 0 in every mode. Timing order A: matching control, candidate, untouched
live, untouched live, candidate, matching control. Order B: candidate, untouched
live, matching control, matching control, untouched live, candidate. Run long A,
standard A, long B, standard B for 24 observations total.
