#!/usr/bin/env python3
"""Relink a private diagnostic overlay without modifying a live build tree."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('--jobs', type=int, default=2)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
build = root / 'build-wasm'
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
snapshot = out / 'source'
snapshot.mkdir()
shutil.copy2(Path(__file__).with_name('census.h'), snapshot / 'census.h')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
manifest = {'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            'sources': {}, 'base_inputs': {}}
(out / 'source-state.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary'], cwd=root))

def replace(s, old, new):
    if s.count(old) != 1:
        raise RuntimeError(f'Expected one patch anchor, found {s.count(old)}: {old[:100]}')
    return s.replace(old, new)

edits = {}
name = 'src/emu/kernel/src/svc.cpp'
s = (root / name).read_text()
s = replace(s, '        const int result = sync ? ss->send_receive_sync', '''        const auto trace_thread = kern->crr_thread()->unique_id();
        const std::string trace_op = "ipc:" + server_name + ":" + std::to_string(ord);
        overlap::scope trace_scope(trace_op + (sync ? ":sync" : ":async"));
        const auto trace_request = overlap::issue(trace_thread, status.ptr_address(), trace_op, sync);
        const int result = sync ? ss->send_receive_sync''')
s = replace(s, '''            ss->get_server()->process_accepted_msg();
        }

        return result;''', '''            ss->get_server()->process_accepted_msg();
        }
        if (status) {
            const auto *trace_status = status.get(crr_pr);
            if (trace_status && trace_status->status != epoc::request_status::pending_status)
                overlap::complete(trace_thread, status.ptr_address(), trace_status->status);
        }
        if (result < 0) overlap::complete(trace_thread, status.ptr_address(), result);
        overlap::returned(trace_thread, status.ptr_address(), trace_request);
        return result;''')
s = replace(s, '        kern->call_ipc_complete_callbacks(msg, val);', '''        overlap::complete(msg->own_thr->unique_id(), msg->request_sts.ptr_address(), val);
        kern->call_ipc_complete_callbacks(msg, val);''')
s = replace(s, '        kern->call_ipc_complete_callbacks(msg, dup_handle);', '''        overlap::complete(msg->own_thr->unique_id(), msg->request_sts.ptr_address(), dup_handle);
        kern->call_ipc_complete_callbacks(msg, dup_handle);''')
edits[name] = s
name = 'src/emu/services/src/context.cpp'
s = (root / name).read_text()
s = replace(s, '        void ipc_context::complete(int res) {', '''        void ipc_context::complete(int res) {
            if (msg->own_thr) overlap::complete(msg->own_thr->unique_id(), msg->request_sts.ptr_address(), res);''')
edits[name] = s
name = 'src/emu/kernel/src/thread.cpp'
s = (root / name).read_text()
s = replace(s, '        void thread::wait_for_any_request() {', '''        void thread::wait_for_any_request() {
            overlap::wait(unique_id(), request_sema->count() <= 0);''')
s = replace(s, '''        void notify_info::complete(int err_code) {
            if (sts.ptr_address() == 0)''', '''        void notify_info::complete(int err_code) {
            if (sts.ptr_address() && requester) overlap::complete(requester->unique_id(), sts.ptr_address(), err_code);
            if (sts.ptr_address() == 0)''')
edits[name] = s
name = 'src/emu/system/src/epoc.cpp'
s = (root / name).read_text()
s = replace(s, '''        if (to_run != nullptr) {
            common::performance::scope run_scope''', '''        if (to_run != nullptr) {
            overlap::enter(to_run->unique_id(), to_run->owning_process()->name() + ":" + to_run->name());
            common::performance::scope run_scope''')
s = replace(s, '            to_run->add_ticks(cpu->get_num_instruction_executed());', '''            to_run->add_ticks(cpu->get_num_instruction_executed());
            overlap::ran(to_run->unique_id(), cpu->get_num_instruction_executed());''')
s += '''
#include <emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE int eka2l1_overlap_configure(int enabled, unsigned pc) {
    eka2l1::overlap::enabled = enabled != 0;
    eka2l1::overlap::decompressor_pc = pc & ~1u;
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *eka2l1_overlap_report() {
    static std::string result;
    result = eka2l1::overlap::report();
    return result.c_str();
}
'''
edits[name] = s
name = 'src/emu/dispatch/src/dispatcher.cpp'
s = (root / name).read_text()
s = replace(s, '        dispatch_find_result->second.first(sys, sys->get_kernel_system()->crr_process(), sys->get_cpu());', '''        overlap::scope trace_scope(std::string("hle:") + std::to_string(function_ord));
        dispatch_find_result->second.first(sys, sys->get_kernel_system()->crr_process(), sys->get_cpu());''')
edits[name] = s
name = 'src/emu/dispatch/src/audio.cpp'
s = (root / name).read_text()
s = replace(s, '        stream->copied_info_ = epoc::notify_info{ req, sys->get_kernel_system()->crr_thread() };', '''        const auto trace_thread = sys->get_kernel_system()->crr_thread()->unique_id();
        const auto trace_request = overlap::issue(trace_thread, req.ptr_address(), "audio:buffer_ready", false);
        stream->copied_info_ = epoc::notify_info{ req, sys->get_kernel_system()->crr_thread() };
        overlap::returned(trace_thread, req.ptr_address(), trace_request);''')
edits[name] = s
name = 'src/emu/services/src/fs/files.cpp'
s = (root / name).read_text()
s = replace(s, '        size_t read_finish_len = vfs_file->read_file(read_data.data(), 1, read_len);', '''        overlap::scope trace_scope("fs:read", read_len);
        size_t read_finish_len = vfs_file->read_file(read_data.data(), 1, read_len);''')
edits[name] = s
name = 'src/emu/cpu/src/aot/aot_runtime.cpp'
s = (root / name).read_text()
s = replace(s, '    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;', '''    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;
    overlap::observe_pc(pc, cpu->Reg[14]);''')
edits[name] = s

commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'eka2l1_wasm'], text=True).splitlines()
link = shlex.split(commands[-1])[2:-2]
assert link[0].endswith('em++') and '-o' in link
inputs = sorted({x for x in link if x.endswith(('.a', '.o'))})
for name in inputs:
    src = build / name
    dest = out / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    before = sha(src)
    shutil.copy2(src, dest)
    if before != sha(src) or before != sha(dest):
        raise RuntimeError(f'Build input changed while being copied: {src}')
    manifest['base_inputs'][name] = before
for name in ['src/emu/wasm/shell.html', 'src/emu/drivers/resources/gles', 'src/emu/drivers/resources/upscale']:
    src, dest = root / name, snapshot / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    if src.is_dir(): shutil.copytree(src, dest)
    else: shutil.copy2(src, dest)
shutil.copytree(build / 'bin/patch', out / 'bin/patch')
app = out / 'src/emu/wasm'
app.mkdir(parents=True, exist_ok=True)
for n in ['audio.js', 'audio-worklet.js']:
    shutil.copy2(build / 'src/emu/wasm' / n, app / n)
for i, value in enumerate(link):
    if str(root) in value:
        link[i] = value.replace(str(build / 'bin/patch'), str(out / 'bin/patch')).replace(str(root), str(snapshot))

jobs = []
archives = {'kernel': 'epockern', 'services': 'epocservs', 'system': 'epoc', 'dispatch': 'epocdispatch', 'cpu': 'cpu'}
for name, changed in edits.items():
    original = root / name
    source = snapshot / name
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text('#include "census.h"\n' + changed)
    manifest['sources'][name] = {'original_sha256': sha(original), 'diagnostic_sha256': sha(source)}
    (source.parent / (source.name + '.original')).write_bytes(original.read_bytes())
    matches = [shlex.split(c) for c in commands if c.endswith(' -c ' + str(original))]
    if len(matches) != 1: raise RuntimeError(f'Compile command not unique: {name}')
    cmd = matches[0]
    for index, arg in enumerate(cmd):
        if arg.startswith('-I' + str(root)):
            inc = Path(arg[2:]).resolve()
            rel = inc.relative_to(root)
            dest = snapshot / rel
            if inc.is_dir() and not dest.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(inc, dest)
            cmd[index] = '-I' + str(dest)
    cmd.insert(1, '-I' + str(snapshot))
    cmd[-1] = str(source)
    obj = Path(cmd[cmd.index('-o') + 1])
    (out / obj).parent.mkdir(parents=True, exist_ok=True)
    module = Path(name).parts[2]
    archive = out / 'src/emu' / module / ('lib' + archives[module] + '.a')
    jobs.append((name, cmd, archive, out / obj))

manifest['link_command'] = link
manifest['compile_commands'] = [cmd for _, cmd, _, _ in jobs]
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
def compile_one(job):
    name, cmd, archive, obj = job
    print('Compiling', name, flush=True)
    subprocess.run(cmd, cwd=out, check=True)
    return archive, obj
with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
    results = list(pool.map(compile_one, jobs))
emar = str(Path(link[0]).with_name('emar'))
for archive, obj in results:
    subprocess.run([emar, 'r', str(archive), str(obj)], check=True)
print('Linking private diagnostic', flush=True)
subprocess.run(link, cwd=out, check=True)
manifest['wasm_sha256'] = sha(app / 'eka2l1.wasm')
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(app, flush=True)
