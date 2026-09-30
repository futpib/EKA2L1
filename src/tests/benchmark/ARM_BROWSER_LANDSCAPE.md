# ARM browser emulation survey, 2026-09-30

Primary-source survey prompted by the user's general state-of-the-art question.
No cross-project performance benchmark was executed. A WASM build of an
interpreter and a guest-code-to-WASM JIT are different capabilities.

- QEMU Wasm: https://github.com/ktock/qemu-wasm
  Experimental TCG-to-WASM backend, AArch64 guest build, multithreaded TCG,
  cold TCI plus hot translated blocks instantiated through browser APIs.
  Serious semantic-frontend/IR alternative; not a drop-in EKA2L1 CPU backend.
- CloudpilotEmu: https://github.com/cloudpilot-emu/cloudpilot-emu
  Palm OS5/Tungsten E2 based on optimized uARM, with audio and savestates.
  Particularly relevant handheld comparator. Documentation describes replacing
  guest PACE with direct host m68k emulation, and host-dependent ARM performance.
- SkyEmu: https://github.com/skylersaleh/SkyEmu
  Shipped browser GBA/DS app using WASM and JavaScript. No claim that its CPU
  frontend or performance transfers to Symbian.
- RPCEmu-WASM: https://github.com/GMH-Code/RPCEmu
  ARM610/710/7500/StrongARM RISC OS platform, interpreter-only browser port.
- Pebble: https://github.com/ericmigi/pebble-qemu-wasm
  Real firmware on QEMU's TCI interpreter, STM32 peripheral models. Its README
  lists Emery as the tested board; other definitions are not all validated.
- Relevant different ISAs: https://github.com/copy/v86 (x86-to-WASM),
  https://github.com/nasomers/flycast-wasm (SH4-to-WASM). Flycast performance
  headlines are author reports, not measurements independently reproduced here.
- https://github.com/voland-emu/Voland describes a browser Switch target but
  its status table lists real CPU backends as planned. Do not treat its roadmap
  as demonstrated ARM64 game execution.

Next source comparison: QEMU WASM dispatch/helper ABI, hotness threshold,
compiled-code validity and memory lowering; Cloudpilot removal of redundant
emulation layers. Any experiment must keep EKA2L1 callback/budget/code-write
contracts or identify an explicit equivalent subsystem boundary. The source
survey establishes available designs, not a measured migration advantage.
