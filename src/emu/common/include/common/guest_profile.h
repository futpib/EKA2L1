#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

namespace eka2l1::common::guest_profile {
    // Guest-thread-only counters, consumed after the performance phase handoff.
    inline bool enabled = false;
    inline std::string quote(const std::string &s) {
        std::ostringstream out;
        out << '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
            else out << c;
        }
        out << '"';
        return out.str();
    }
    struct histogram {
        enum kind { executed, decoded };
        static constexpr unsigned handlers = 205;
        unsigned stride = 1021;
        std::uint64_t total[2]{};
        std::uint64_t types[2][2][handlers]{};
        std::uint64_t dropped[2]{};
        // kind, Thumb, handler, ASID, PC, original opcode, mapped, process, module.
        using key = std::tuple<unsigned, unsigned, unsigned, std::uint32_t,
            std::uint32_t, std::uint32_t, bool, std::string, std::string>;
        std::map<key, std::uint64_t> samples;
        bool record(unsigned k, unsigned thumb, unsigned handler) {
            if (k > 1 || thumb > 1 || handler >= handlers) return false;
            ++types[k][thumb][handler];
            return ++total[k] % stride == 0;
        }
        void sample(const key &k) {
            auto it = samples.find(k);
            if (it != samples.end()) ++it->second;
            else if (samples.size() < 131072) samples.emplace(k, 1);
            else ++dropped[std::get<0>(k)];
        }
        template<class Names> std::string report(Names name) const {
            std::ostringstream out;
            out << "{\"stride\":" << stride << ",\"executed\":" << total[0]
                << ",\"decoded\":" << total[1] << ",\"dropped_samples\":[" << dropped[0] << ',' << dropped[1]
                << "],\"types\":[";
            bool first = true;
            for (unsigned k = 0; k < 2; ++k) for (unsigned t = 0; t < 2; ++t)
                for (unsigned h = 0; h < handlers; ++h) if (types[k][t][h]) {
                    if (!first) out << ','; first = false;
                    out << "{\"kind\":" << k << ",\"thumb\":" << t << ",\"handler\":" << quote(name(h))
                        << ",\"count\":" << types[k][t][h] << '}';
                }
            out << "],\"samples\":[";
            first = true;
            for (const auto &[k, count] : samples) {
                if (!first) out << ','; first = false;
                const auto &[kind, thumb, handler, asid, pc, opcode, mapped, process, module] = k;
                out << "{\"kind\":" << kind << ",\"thumb\":" << thumb << ",\"handler\":" << quote(name(handler))
                    << ",\"asid\":" << asid << ",\"pc\":" << pc << ",\"opcode\":" << opcode
                    << ",\"mapped\":" << (mapped ? "true" : "false") << ",\"process\":" << quote(process)
                    << ",\"module\":" << quote(module) << ",\"count\":" << count << '}';
            }
            out << "]}";
            return out.str();
        }
    };
    inline histogram state;
}
