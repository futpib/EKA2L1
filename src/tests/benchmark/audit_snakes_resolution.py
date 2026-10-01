#!/usr/bin/env python3
"""Run the original Snakes ARM rectangle selector in isolation (requires unicorn).

Input is unrelocated E32 code, not the compressed executable. Only the three
Symbian geometry helpers are substituted; game instructions are never patched.
This audits buffer selection, not OS compatibility or full-game rendering.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_PROT_READ, UC_PROT_EXEC
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5,
                              UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
                              UC_ARM_REG_R9, UC_ARM_REG_SP, UC_ARM_REG_LR,
                              UC_ARM_REG_PC)


VARIANTS = {
    '23dec1c3fe2831b2faf02fa0577eddb180cf33642c74e9d8ee78070a925c38f4': {
        'name': 'benchmark SIS, 6r45_1b.exe, UID 0x2000730F',
        'start': 0x19F4, 'stop': 0x1B38, 'stack_size_offset': 0x200,
        'screen_size': 0x62850, 'size_equal': 0x62518, 'rect': 0x62730,
    },
    '56c569d7de85d21c57eb60aff361be68ba54a5a21a09b1a17cd69aa942d00a5f': {
        'name': 'N80 ROM, 6r45_1.exe, UID 0x10208A45',
        'start': 0x1F30, 'stop': 0x1FF0, 'stack_size_offset': 0x280,
        'screen_size': 0x6BDD8, 'size_equal': 0x6BAA8, 'rect': 0x6BCB8,
    },
    '1eb0c7a7eca6c123025672b042b51b1f40b7d6fb6763c4437458fe1eea030a12': {
        'name': 'archive Snakes HD SISX, 6r45_1.exe, UID 0x10208A45',
        'start': 0x1EDC, 'stop': 0x1F9C, 'stack_size_offset': 0x280,
        'screen_size': 0x6BA98, 'size_equal': 0x6B778, 'rect': 0x6B988,
    },
}

SIZES = [(176, 208), (208, 176), (208, 208), (240, 320), (320, 240),
         (352, 416), (416, 352), (320, 480), (480, 320), (360, 640),
         (640, 360), (640, 480), (480, 640), (800, 352), (1280, 720)]


def select(code, variant, width, height):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    code_size = (len(code) + 4095) & ~4095
    cpu.mem_map(0, code_size)
    cpu.mem_write(0, code)
    cpu.mem_protect(0, code_size, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_map(0x100000, 0x20000)
    obj, stack = 0x100000, 0x110000
    offset = variant['stack_size_offset']
    for register, value in [(UC_ARM_REG_R4, obj), (UC_ARM_REG_R5, stack + offset),
                            (UC_ARM_REG_R6, stack + offset - 8), (UC_ARM_REG_R7, 0),
                            (UC_ARM_REG_R8, 320), (UC_ARM_REG_R9, 240),
                            (UC_ARM_REG_SP, stack)]:
        cpu.reg_write(register, value)
    calls = {'screen_size': 0, 'size_equal': 0, 'rect': 0}

    def helper(cpu, address, size, user_data):
        r0 = cpu.reg_read(UC_ARM_REG_R0)
        r1 = cpu.reg_read(UC_ARM_REG_R1)
        if address == variant['screen_size']:
            calls['screen_size'] += 1
            cpu.mem_write(r0, struct.pack('<II', width, height))
        elif address == variant['size_equal']:
            calls['size_equal'] += 1
            cpu.reg_write(UC_ARM_REG_R0, int(cpu.mem_read(r0, 8) == cpu.mem_read(r1, 8)))
        elif address == variant['rect']:
            calls['rect'] += 1
            r2, r3 = cpu.reg_read(UC_ARM_REG_R2), cpu.reg_read(UC_ARM_REG_R3)
            bottom = struct.unpack('<I', cpu.mem_read(cpu.reg_read(UC_ARM_REG_SP), 4))[0]
            cpu.mem_write(r0, struct.pack('<IIII', r1, r2, r3, bottom))
        else:
            return
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    for address in [variant['screen_size'], variant['size_equal'], variant['rect']]:
        cpu.hook_add(UC_HOOK_CODE, helper, begin=address, end=address)
    cpu.emu_start(variant['start'], variant['stop'], count=1000)
    if cpu.reg_read(UC_ARM_REG_PC) != variant['stop'] or calls['rect'] != 1:
        raise RuntimeError(f'Selector did not finish for {width}x{height}: {calls}')
    x, y, right, bottom = struct.unpack('<IIII', cpu.mem_read(obj + 0x78, 16))
    return {'screen': [width, height], 'rectangle': [x, y, right, bottom],
            'buffer': [right - x, bottom - y], 'helper_calls': calls}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('code', type=Path, nargs='+', help='Unrelocated code dumps')
    args = parser.parse_args()
    results = []
    for path in args.code:
        code = path.read_bytes()
        digest = hashlib.sha256(code).hexdigest()
        if digest not in VARIANTS:
            parser.error(f'Unrecognized code SHA-256: {digest}')
        variant = VARIANTS[digest]
        results.append({'code_sha256': digest, 'code_bytes': len(code), **variant,
                        'results': [select(code, variant, *size) for size in SIZES]})
    print(json.dumps({'unicorn_version': unicorn.__version__, 'variants': results}, indent=2))


if __name__ == '__main__':
    main()
