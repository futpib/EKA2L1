#pragma once

#include <common/diagnostics.h>
#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

namespace eka2l1::common::guest_profile {
    // Guest-thread-only counters, consumed after the performance phase handoff.
    inline diagnostics::flag enabled = false;
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
        std::map<std::string, std::uint64_t> aot_events;
        using event_key = std::tuple<std::string, std::uint32_t, std::uint32_t, std::uint32_t>;
        std::map<event_key, std::uint64_t> aot_samples;
        std::uint64_t dropped_aot_samples = 0;
        std::uint64_t compiled_blocks = 0, memory_calls[6]{};
        std::map<unsigned, std::uint64_t> block_lengths;
        using edge_key = std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,unsigned,std::uint32_t>;
        std::map<edge_key,std::uint64_t> edges;
        std::uint64_t dropped_edges = 0;
        void edge(std::uint32_t from, std::uint32_t to, std::uint32_t asid, unsigned length, std::uint32_t last) {
            edge_key k{from,to,asid,length,last};
            auto it = edges.find(k);
            if (it != edges.end()) ++it->second;
            else if (edges.size() < 131072) edges.emplace(k,1);
            else ++dropped_edges;
        }
        void event(const char *reason, std::uint32_t pc_mode, std::uint32_t space = 0, std::uint32_t opcode = 0) {
            if (++aot_events[reason] % stride) return;
            event_key key{reason,pc_mode,space,opcode};
            auto it = aot_samples.find(key);
            if (it != aot_samples.end()) ++it->second;
            else if (aot_samples.size() < 131072) aot_samples.emplace(key,1);
            else ++dropped_aot_samples;
        }
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
            out << "],\"aot_events\":{";
            first = true;
            for (const auto &[reason,count] : aot_events) {
                if (!first) out << ','; first = false;
                out << quote(reason) << ':' << count;
            }
            out << "},\"dropped_aot_samples\":" << dropped_aot_samples << ",\"aot_samples\":[";
            first = true;
            for (const auto &[key,count] : aot_samples) {
                if (!first) out << ','; first = false;
                const auto &[reason,pc,space,opcode] = key;
                out << "{\"reason\":" << quote(reason) << ",\"pc_mode\":" << pc
                    << ",\"asid\":" << space << ",\"opcode\":" << opcode << ",\"count\":" << count << '}';
            }
            out << "],\"compiled_blocks\":" << compiled_blocks << ",\"dropped_edges\":" << dropped_edges << ",\"memory_calls\":[";
            for (unsigned i=0;i<6;++i) { if(i) out << ','; out << memory_calls[i]; }
            out << "],\"block_lengths\":["; first=true;
            for (const auto &[length,count] : block_lengths) {
                if(!first) out << ','; first=false;
                out << '[' << length << ',' << count << ']';
            }
            out << "],\"edges\":["; first=true;
            for (const auto &[k,count] : edges) {
                if(!first) out << ','; first=false;
                const auto &[from,to,asid,length,last] = k;
                out << "{\"from\":" << from << ",\"to\":" << to << ",\"asid\":" << asid
                    << ",\"length\":" << length << ",\"last_opcode\":" << last << ",\"count\":" << count << '}';
            }
            out << "]}";
            return out.str();
        }
    };
    inline histogram state;
}
