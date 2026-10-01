#pragma once

// Diagnostic overlay only: never linked into the ordinary emulator build.
#include <common/deterministic.h>
#include <common/guest_profile.h>
#include <common/performance.h>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace eka2l1::overlap {
using clock = std::chrono::steady_clock;
using id = std::uint64_t;
inline bool enabled = false;
inline std::mutex mutex;
inline id instructions = 0, current_thread = 0, next_request = 0;
inline std::uint32_t decompressor_pc = 0;
inline std::map<id, id> thread_instructions;
inline std::map<std::pair<unsigned, id>, id> phase_instructions;
inline std::map<id, std::string> thread_names;
inline id now() { return common::benchmark::virtual_us.load(); }
inline unsigned phase(id us) { return us < 21000000 ? 0 : us < 78000000 ? 1 : 2; }
inline std::string quote(const std::string &s) { return common::guest_profile::quote(s); }
struct request {
    id serial, thread, status, us, all, own, presentations;
    std::string operation;
    bool sync, returned = false, waited = false;
    id before_wait_all = 0, before_wait_own = 0;
};
struct completion {
    request issued;
    id us, all, own, presentations;
    int result;
};
struct counter { id calls = 0, bytes = 0; double milliseconds = 0; };
inline std::map<std::pair<id, id>, request> pending;
inline std::vector<completion> completed;
inline std::map<std::pair<unsigned, std::string>, counter> counters;
inline id replaced = 0, dropped = 0, waits = 0, blocked_waits = 0;
inline void enter(id thread, const std::string &name) {
    if (!enabled) return;
    std::lock_guard guard(mutex);
    current_thread = thread;
    if (!thread_names.count(thread)) thread_names[thread] = name;
}
inline void ran(id thread, id count) {
    if (!enabled) return;
    std::lock_guard guard(mutex);
    instructions += count;
    thread_instructions[thread] += count;
    phase_instructions[{phase(now()), thread}] += count;
}
inline id issue(id thread, id status, const std::string &operation, bool sync) {
    if (!enabled || !status) return 0;
    std::lock_guard guard(mutex);
    const auto key = std::make_pair(thread, status);
    if (pending.count(key)) ++replaced;
    const auto serial = ++next_request;
    pending[key] = {serial, thread, status, now(), instructions, thread_instructions[thread],
        common::performance::presentations.load(), operation, sync};
    return serial;
}
inline void returned(id thread, id status, id serial) {
    if (!enabled || !serial) return;
    std::lock_guard guard(mutex);
    const auto it = pending.find({thread, status});
    if (it != pending.end() && it->second.serial == serial) it->second.returned = true;
}
inline void complete(id thread, id status, int result) {
    if (!enabled || !status) return;
    std::lock_guard guard(mutex);
    const auto it = pending.find({thread, status});
    if (it == pending.end()) return;
    if (completed.size() < 200000)
        completed.push_back({it->second, now(), instructions - it->second.all,
            thread_instructions[thread] - it->second.own,
            common::performance::presentations.load() - it->second.presentations, result});
    else ++dropped;
    pending.erase(it);
}
inline void wait(id thread, bool blocks) {
    if (!enabled) return;
    std::lock_guard guard(mutex);
    ++waits;
    if (!blocks) return;
    ++blocked_waits;
    for (auto &[key, p] : pending) if (p.thread == thread && !p.waited) {
        p.waited = true;
        p.before_wait_all = instructions - p.all;
        p.before_wait_own = thread_instructions[thread] - p.own;
    }
}
struct scope {
    std::string operation;
    id bytes;
    unsigned period;
    clock::time_point start;
    scope(std::string op, id count = 0) : operation(std::move(op)), bytes(count), period(phase(now())), start(clock::now()) {}
    ~scope() {
        if (!enabled) return;
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
        std::lock_guard guard(mutex);
        auto &c = counters[{period, operation}];
        ++c.calls; c.bytes += bytes; c.milliseconds += ms;
    }
};
struct guest_call { std::uint32_t return_pc; unsigned period; clock::time_point start; };
inline std::map<id, guest_call> decompressions;
inline void observe_pc(std::uint32_t pc, std::uint32_t lr) {
    if (!enabled) return;
    // This is deliberately one cheap guard on region lookup, not an instruction trace.
    if (pc != decompressor_pc && decompressions.empty()) return;
    std::lock_guard guard(mutex);
    auto it = decompressions.find(current_thread);
    if (it != decompressions.end() && pc == it->second.return_pc) {
        auto &c = counters[{it->second.period, "guest:EZLib.DecompressL.return"}];
        ++c.calls;
        c.milliseconds += std::chrono::duration<double, std::milli>(clock::now()-it->second.start).count();
        decompressions.erase(it);
    }
    if (pc == decompressor_pc && !decompressions.count(current_thread)) {
        ++counters[{phase(now()), "guest:EZLib.DecompressL.entry"}].calls;
        decompressions.emplace(current_thread, guest_call{lr & ~1u, phase(now()), clock::now()});
    }
}
inline std::string report() {
    std::lock_guard guard(mutex);
    std::ostringstream o;
    o << "{\"guest_us\":" << now() << ",\"instructions\":" << instructions
      << ",\"issued\":" << next_request << ",\"replaced\":" << replaced
      << ",\"dropped\":" << dropped << ",\"waits\":" << waits << ",\"blocked_waits\":" << blocked_waits
      << ",\"decompressor_pc\":" << decompressor_pc << ",\"open_decompressions\":" << decompressions.size()
      << ",\"threads\":[";
    bool first = true;
    for (auto &[t,n] : thread_names) {
        if (!first) o << ','; first = false;
        o << "{\"id\":" << t << ",\"name\":" << quote(n) << ",\"instructions\":" << thread_instructions[t] << '}';
    }
    o << "],\"thread_phases\":["; first = true;
    for (auto &[key, count] : phase_instructions) {
        if (!first) o << ','; first = false;
        o << "{\"phase\":" << key.first << ",\"thread\":" << key.second << ",\"instructions\":" << count << '}';
    }
    o << "],\"scopes\":["; first = true;
    for (auto &[k,c] : counters) {
        if (!first) o << ','; first = false;
        o << "{\"phase\":" << k.first << ",\"operation\":" << quote(k.second) << ",\"calls\":" << c.calls
          << ",\"bytes\":" << c.bytes << ",\"wall_ms\":" << c.milliseconds << '}';
    }
    o << "],\"completed\":["; first = true;
    for (const auto &c : completed) {
        const auto &p=c.issued;
        if (!first) o << ','; first=false;
        o << "{\"id\":" << p.serial << ",\"thread\":" << p.thread << ",\"status\":" << p.status
          << ",\"operation\":" << quote(p.operation) << ",\"issue_us\":" << p.us << ",\"complete_us\":" << c.us
          << ",\"sync\":" << p.sync << ",\"returned_pending\":" << p.returned << ",\"blocked_while_pending\":" << p.waited
          << ",\"all_instructions\":" << c.all << ",\"own_instructions\":" << c.own
          << ",\"presentations_while_pending\":" << c.presentations
          << ",\"before_wait_all\":" << p.before_wait_all << ",\"before_wait_own\":" << p.before_wait_own
          << ",\"result\":" << c.result << '}';
    }
    o << "],\"pending\":["; first=true;
    for (const auto &[k,p] : pending) {
        if (!first) o << ','; first=false;
        o << "{\"id\":" << p.serial << ",\"thread\":" << p.thread << ",\"operation\":" << quote(p.operation)
          << ",\"issue_us\":" << p.us << ",\"returned_pending\":" << p.returned << '}';
    }
    o << "]}";
    return o.str();
}
}
