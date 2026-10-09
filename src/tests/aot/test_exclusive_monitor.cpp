#include <cpu/12l1r/exclusive_monitor.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using eka2l1::arm::r12l1::exclusive_monitor;
static unsigned checks;
static void require(bool ok) {
    ++checks;
    if (!ok) { std::cerr << "FAIL reservation check " << checks << '\n'; std::exit(1); }
}

static void compare_sequences() {
    for (unsigned processors : {1u, 2u, 8u}) {
        exclusive_monitor monitor(processors);
        std::vector<exclusive_monitor::reservation_snapshot> reference(processors);
        for (auto &entry : reference) entry.address = 0xdeaddead;
        unsigned random = 192837;
        for (unsigned step = 0; step < 20000; ++step) {
            random = random * 1664525u + 1013904223u;
            const unsigned id = (random >> 8) % processors;
            const unsigned address = 0x1000 + ((random >> 16) % 8) * 4;
            const unsigned value = random ^ 0xabcdef;
            switch ((random >> 4) % 5) {
            case 0:
                require(monitor.read_and_mark<unsigned>(id, address, [&] { return value; }) == value);
                reference[id].address = address;
                std::memcpy(reference[id].value.data(), &value, sizeof(value));
                break;
            case 1: {
                const bool expected = reference[id].address == address;
                const auto saved = reference[id].value;
                if (expected) for (auto &entry : reference) if (entry.address == address) entry.address = 0xdeaddead;
                bool called = false;
                const bool actual = monitor.do_exclusive_operation<unsigned>(id, address, [&](unsigned old) {
                    called = true; unsigned want; std::memcpy(&want, saved.data(), sizeof(want));
                    require(old == want); return (value & 1) != 0;
                });
                require(called == expected && actual == (expected && (value & 1)));
                break;
            }
            case 2:
                monitor.clear_exclusive(); monitor.clear_exclusive();
                for (auto &entry : reference) entry.address = 0xdeaddead;
                break;
            case 3:
                monitor.clear_processor(id); reference[id].address = 0xdeaddead;
                break;
            case 4:
                reference[id] = {address, {value, value ^ 0x12345678u}};
                monitor.restore(id, reference[id]);
                break;
            }
            for (unsigned i = 0; i < processors; ++i) require(monitor.snapshot(i) == reference[i]);
        }
    }
}

static void cross_thread_clear() {
    exclusive_monitor monitor(1);
    std::atomic<unsigned> turn{0};
    std::thread clearer([&] {
        for (unsigned i = 0; i < 10000; ++i) {
            while (turn.load(std::memory_order_acquire) != 1) {}
            monitor.clear_exclusive(); monitor.clear_exclusive();
            turn.store(2, std::memory_order_release);
        }
    });
    for (unsigned i = 0; i < 10000; ++i) {
        monitor.read_and_mark<unsigned>(0, 0x1000, [&] { return i; });
        turn.store(1, std::memory_order_release);
        while (turn.load(std::memory_order_acquire) != 2) {}
        require(!monitor.do_exclusive_operation<unsigned>(0, 0x1000, [](unsigned) { return true; }));
        turn.store(0, std::memory_order_release);
    }
    clearer.join();
}

int main() {
    compare_sequences();
    cross_thread_clear();
    std::cout << "PASS " << checks << " reservation state and cross-thread clear checks\n";
}
