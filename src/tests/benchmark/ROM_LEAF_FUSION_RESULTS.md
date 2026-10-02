# Bounded immutable-ROM leaf fusion

The opt-in ROM leaf resolver supplies eager and hot ARM ROM regions with the
existing bounded call-fusion compiler. It rejects unaligned/out-of-image targets
and truncates reads at the image end, the guest address-space end and the
existing leaf-instruction limit. RAM mapping/lifetime resolution is unchanged.
No game name, UID, ROM hash or guest instruction address selects this behavior.
The option is off by default, with configuration/readback frozen before CPU
initialization. Harnesses reject mismatched readback and post-init changes.

The fresh corrected compiler suite passes all 183 tests, including 97,776 ROM
extent comparisons against a byte-wise oracle and 2,560 execution/state/memory/
budget comparisons through the production resolver under policy 7. The first
suite hit an old fixture assertion requiring IR memory guards, which does not
apply to policy 7 deferred-memory lowering. That failed run was stopped and
retained. Only the fixture assertion changed; its semantic oracle is unchanged.

Production/probe artifacts are byte-identical between V14 and V14b. Therefore
the first build's passing production checks are explicitly reused: all three
native CTest targets, 46,016 mode-3 region/fault comparisons, 4,032 ARM short-
block memory-fault comparisons, 1,152 Thumb call cases, and 2,880 Thumb memory-
fault cases. The native operation probes cover emitted instructions/callbacks;
the production resolver's image bounds are separately covered by its unit tests.

Seven exact native image/record/PCM replays pass: stationary Sky Force region
control/candidate/checked, moving-and-firing Sky Force candidate/checked and
both established Snakes routes. All use shared Thumb memory 1, ARM memory 0,
mode 3 and original limits. Startup counts show actual eager-ROM fusion rather
than only flag selection. Checked invocations use callback memory.

The fixed twelve-observation exploratory screen compares three policies in the
same frozen production archive: basic ROM, ROM regions without leaves, and ROM
regions with leaves. Orders rotate across both Sky Force and both Snakes routes.
Capture, detailed counters and sampling are disabled, with physical GPU and
shared audio enabled. It waits for all correctness workers to finish. The
original waiting screen was stopped before any sample to correct its acceptance
gate; its schedule and original timestamp remain unchanged and are preserved.
Single observations are not confirmation or untouched-live delivery evidence.
Sky Force realtime remains the open goal. Nothing is deployed or pushed.
