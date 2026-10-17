#include <cpu/aot/memory_access_profile.h>

#ifdef EKA2L1_MEMORY_ACCESS_PROFILE
#include <array>
#include <mutex>
#include <sstream>
#include <vector>

namespace eka2l1::arm::aot::memory_access_profile {
    std::uint64_t counters[spaces][buckets][2]{};
    std::uintptr_t active_row = reinterpret_cast<std::uintptr_t>(counters[0]);
    static std::mutex mutex;
    static std::array<std::string, spaces> names;
    struct allocation_event {
        const char *kind;
        unsigned space;
        std::uint32_t address;
        std::uint64_t size, committed;
        bool local, direct;
        std::uintptr_t host;
    };
    static std::vector<allocation_event> events;

    void process(unsigned space, const std::string &name) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (space < spaces) names[space] = name;
    }

    void event(const char *kind, unsigned space, std::uint32_t address,
            std::uint64_t size, std::uint64_t committed, bool local, bool direct,
            std::uintptr_t host) {
        const std::lock_guard<std::mutex> lock(mutex);
        events.push_back({kind, space, address, size, committed, local, direct, host});
    }

    std::string report() {
        const std::lock_guard<std::mutex> lock(mutex);
        std::ostringstream out;
        out << "{\"bucket_bytes\":1048576,\"rows\":[";
        bool comma = false;
        for (unsigned space = 0; space < spaces; ++space) {
            bool used = !names[space].empty();
            for (unsigned bucket = 0; bucket < buckets && !used; ++bucket)
                used = counters[space][bucket][0] || counters[space][bucket][1];
            if (!used) continue;
            if (comma) out << ',';
            comma = true;
            out << "{\"asid\":" << space << ",\"name\":\"";
            for (auto c : names[space]) {
                if (c == '\\' || c == '"') out << '\\';
                if (static_cast<unsigned char>(c) >= 32) out << c;
            }
            out << "\",\"bins\":[";
            bool bin_comma = false;
            for (unsigned bucket = 0; bucket < buckets; ++bucket) {
                const auto &bin = counters[space][bucket];
                if (!bin[0] && !bin[1]) continue;
                if (bin_comma) out << ',';
                bin_comma = true;
                out << '[' << bucket << ',' << bin[0] << ',' << bin[1] << ']';
            }
            out << "]}";
        }
        out << "],\"events\":[";
        for (unsigned i = 0; i < events.size(); ++i) {
            const auto &e = events[i];
            if (i) out << ',';
            out << "{\"kind\":\"" << e.kind << "\",\"asid\":" << e.space
                << ",\"address\":" << e.address << ",\"size\":" << e.size
                << ",\"committed\":" << e.committed << ",\"local\":" << e.local
                << ",\"direct\":" << e.direct << ",\"host\":" << e.host << '}';
        }
        out << "]}";
        return out.str();
    }
}
#endif
