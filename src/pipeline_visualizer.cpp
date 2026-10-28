#include "pipeline_visualizer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>

namespace simplerisc {

std::string PipelineVisualizer::renderStages(
    const std::vector<PipelineStageSnapshot>& stages,
    uint64_t cycle,
    const std::string& pipelineType,
    bool last_stall,
    bool last_flush,
    double cpi,
    double ipc) {
    
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|            PIPELINE EXECUTION STATE (Clock Cycle: " << std::setw(6) << std::right << cycle << ")                   |\n";
    oss << "+-----------------------------------------------------------------------------+\n";

    for (const auto& st : stages) {
        oss << "  [" << std::setw(6) << std::left << st.stage_name << "] : ";
        if (st.is_bubble) {
            if (!st.stall_reason.empty()) {
                oss << "[BUBBLE: " << st.stall_reason << "]";
            } else {
                oss << "-- BUBBLE / EMPTY --";
            }
        } else if (st.is_stalled) {
            if (!st.stall_reason.empty()) {
                oss << "[STALL: " << st.stall_reason << "] " << st.instruction_text << " (PC: " << toHex8(st.pc) << ")";
            } else {
                oss << "[STALLED] " << st.instruction_text << " (PC: " << toHex8(st.pc) << ")";
            }
        } else if (!st.instruction_text.empty()) {
            oss << st.instruction_text << " (PC: " << toHex8(st.pc) << ")";
        } else if (st.valid) {
            oss << "(PC: " << toHex8(st.pc) << ")";
        } else {
            oss << "-- EMPTY / INACTIVE --";
        }

        if (!st.forwarding_info.empty()) {
            oss << "\n             ↳ " << st.forwarding_info;
        }
        if (!st.details.empty()) {
            oss << "\n             ↳ " << st.details;
        }
        oss << "\n";
    }

    oss << "+-----------------------------------------------------------------------------+\n";
    oss << "  Telemetry: Stall: " << (last_stall ? "YES" : "NO ")
        << " | Flush: " << (last_flush ? "YES" : "NO ")
        << " | CPI: " << std::fixed << std::setprecision(3) << cpi
        << " | IPC: " << std::fixed << std::setprecision(3) << ipc
        << " | Mode: " << pipelineType << "\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

std::string PipelineVisualizer::renderTrace(
    const std::vector<CycleSnapshot>& history,
    const std::string& pipelineType) {
    
    if (history.empty()) {
        return "\nNo pipeline cycles have been executed yet. Step or run the program (e.g. 's 5' or 'run') to generate a trace.\n";
    }

    std::ostringstream oss;
    oss << "\n========================================================================================\n"
        << "                         RISC201 COMPLETE PIPELINE TRACE\n"
        << "========================================================================================\n"
        << "Total Clock Cycles : " << history.size() << "\n"
        << "Pipeline Depth     : " << pipelineType << "\n\n";

    bool is6Stage = (pipelineType == "6-Stage" || pipelineType == "6" || pipelineType == "SIX_STAGE");

    // -------------------------------------------------------------------------
    // PART 1: Cycle-by-Cycle Stage State Table
    // -------------------------------------------------------------------------
    oss << "--- Cycle-by-Cycle Stage State Table ---\n";
    if (is6Stage) {
        oss << "+-------+-------------------+-------------------+-------------------+-------------------+-------------------+-------------------+\n"
            << "| Cycle | IF1               | IF2               | OF                | EX                | MA                | RW                |\n"
            << "+-------+-------------------+-------------------+-------------------+-------------------+-------------------+-------------------+\n";

        for (const auto& snap : history) {
            oss << "| " << std::setw(5) << std::right << snap.cycle << " | ";
            for (size_t s = 0; s < 6; ++s) {
                std::string cell = "[bubble]";
                if (s < snap.stages.size()) {
                    const auto& st = snap.stages[s];
                    if (st.is_stalled) {
                        cell = st.stall_reason.empty() ? ("[STALL] " + st.instruction_text) : ("[STALL:" + st.stall_reason + "]");
                    } else if (st.is_bubble && !st.stall_reason.empty()) {
                        cell = "[" + st.stall_reason + "]";
                    } else if (!st.is_bubble && !st.instruction_text.empty()) {
                        cell = st.instruction_text;
                    } else if (st.valid && !st.instruction_text.empty()) {
                        cell = st.instruction_text;
                    }
                }
                oss << std::setw(17) << std::left << truncateStr(cell, 17) << " | ";
            }
            oss << "\n";
        }
        oss << "+-------+-------------------+-------------------+-------------------+-------------------+-------------------+-------------------+\n\n";
    } else {
        oss << "+-------+-------------------------+-------------------------+-------------------------+-------------------------+\n"
            << "| Cycle | IF                      | ID/RR                   | EX                      | MEM/WB                  |\n"
            << "+-------+-------------------------+-------------------------+-------------------------+-------------------------+\n";

        for (const auto& snap : history) {
            oss << "| " << std::setw(5) << std::right << snap.cycle << " | ";
            for (size_t s = 0; s < 4; ++s) {
                std::string cell = "[bubble]";
                if (s < snap.stages.size()) {
                    const auto& st = snap.stages[s];
                    if (st.is_stalled) {
                        cell = st.stall_reason.empty() ? ("[STALL] " + st.instruction_text) : ("[STALL:" + st.stall_reason + "]");
                    } else if (st.is_bubble && !st.stall_reason.empty()) {
                        cell = "[" + st.stall_reason + "]";
                    } else if (!st.is_bubble && !st.instruction_text.empty()) {
                        cell = st.instruction_text;
                    } else if (st.valid && !st.instruction_text.empty()) {
                        cell = st.instruction_text;
                    }
                }
                oss << std::setw(23) << std::left << truncateStr(cell, 23) << " | ";
            }
            oss << "\n";
        }
        oss << "+-------+-------------------------+-------------------------+-------------------------+-------------------------+\n\n";
    }

    // -------------------------------------------------------------------------
    // PART 2: Space-Time Timing Diagram
    // -------------------------------------------------------------------------
    oss << renderSpaceTimeDiagram(history);
    oss << "========================================================================================\n\n";
    return oss.str();
}

std::string PipelineVisualizer::renderSpaceTimeDiagram(
    const std::vector<CycleSnapshot>& history) {
    
    if (history.empty()) {
        return "\nNo cycle snapshots available for space-time timing diagram.\n";
    }

    struct InstTrace {
        uint64_t id{0};
        addr_t pc{0};
        std::string text;
        std::map<uint64_t, std::string> stage_map; // cycle -> short stage name
    };

    std::map<uint64_t, InstTrace> inst_map;
    std::vector<uint64_t> inst_order;

    for (const auto& snap : history) {
        for (const auto& st : snap.stages) {
            if (st.inst_id > 0 && !st.is_bubble && !st.instruction_text.empty()) {
                if (inst_map.find(st.inst_id) == inst_map.end()) {
                    InstTrace it;
                    it.id = st.inst_id;
                    it.pc = st.pc;
                    it.text = st.instruction_text;
                    inst_map[st.inst_id] = it;
                    inst_order.push_back(st.inst_id);
                }
                std::string shortName = st.stage_name;
                if (shortName == "ID/RR") shortName = "ID";
                if (shortName == "MEM/WB") shortName = "WB";
                if (st.is_stalled) shortName += "*";
                inst_map[st.inst_id].stage_map[snap.cycle] = shortName;
            }
        }
    }

    if (inst_order.empty()) {
        return "\n(No non-bubble instruction instances identified in trace history)\n";
    }

    std::ostringstream oss;
    oss << "--- Pipeline Space-Time Timing Diagram ---\n";
    uint64_t minCycle = history.front().cycle;
    uint64_t maxCycle = history.back().cycle;
    const uint64_t CHUNK_SIZE = 15;

    for (uint64_t chunkStart = minCycle; chunkStart <= maxCycle; chunkStart += CHUNK_SIZE) {
        uint64_t chunkEnd = std::min(chunkStart + CHUNK_SIZE - 1, maxCycle);

        std::string headerLabel = "Instruction";
        oss << std::setw(32) << std::left << headerLabel << " | ";
        for (uint64_t c = chunkStart; c <= chunkEnd; ++c) {
            std::string cLabel = "C" + std::to_string(c);
            oss << std::setw(4) << std::left << cLabel << "| ";
        }
        oss << "\n";

        std::string sep(33, '-');
        oss << sep << "+-";
        for (uint64_t c = chunkStart; c <= chunkEnd; ++c) {
            (void)c;
            oss << "-----+";
        }
        oss << "\n";

        for (uint64_t iId : inst_order) {
            const auto& it = inst_map[iId];
            bool activeInChunk = false;
            for (uint64_t c = chunkStart; c <= chunkEnd; ++c) {
                if (it.stage_map.find(c) != it.stage_map.end()) {
                    activeInChunk = true;
                    break;
                }
            }
            if (!activeInChunk) continue;

            std::string fullLabel = toHex8(it.pc) + ": " + it.text;
            oss << std::setw(32) << std::left << truncateStr(fullLabel, 32) << " | ";
            for (uint64_t c = chunkStart; c <= chunkEnd; ++c) {
                auto fit = it.stage_map.find(c);
                std::string val = (fit != it.stage_map.end()) ? fit->second : "  ";
                oss << std::setw(4) << std::left << val << "| ";
            }
            oss << "\n";
        }
        oss << "\n";
    }

    return oss.str();
}

std::string PipelineVisualizer::renderHazards(
    uint64_t stalls,
    uint64_t flushes,
    uint64_t fwd_hits,
    uint64_t load_use,
    uint64_t structural_avoided) {
    
    std::ostringstream oss;
    oss << "+-----------------------------------------------------------------------------+\n";
    oss << "|                        HAZARD UNIT HARDWARE TELEMETRY                       |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    auto row = [](const std::string& lbl, uint64_t v) {
        std::ostringstream r;
        r << "| " << std::left << std::setw(38) << lbl << ": "
          << std::right << std::setw(35) << v << " |\n";
        return r.str();
    };
    oss << row("Pipeline Stall Cycles (Interlocks)", stalls);
    oss << row("Branch Flush Penalties (Flushes)", flushes);
    oss << row("Load-Use Data Hazards Trapped", load_use);
    oss << row("Data Forwarding Bypass Events", fwd_hits);
    if (structural_avoided > 0) {
        oss << row("Structural Memory Hazards Avoided", structural_avoided);
    }
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

std::string PipelineVisualizer::renderForwarding(
    uint64_t fwd_hits,
    uint64_t store_load_forwards) {
    
    std::ostringstream oss;
    oss << "+-----------------------------------------------------------------------------+\n";
    oss << "|                     FORWARDING UNIT HARDWARE TELEMETRY                      |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    auto row = [](const std::string& lbl, const std::string& v) {
        std::ostringstream r;
        r << "| " << std::left << std::setw(38) << lbl << ": "
          << std::right << std::setw(35) << v << " |\n";
        return r.str();
    };
    oss << row("EX/MEM & MEM/WB Bypass Events", std::to_string(fwd_hits));
    oss << row("Store-to-Load Buffer Bypasses", std::to_string(store_load_forwards));
    oss << row("Forwarding Interlock Mode", "Hardware-Assisted Active Bypass");
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

std::string PipelineVisualizer::renderComparison(
    uint64_t cycles4, uint64_t retired4, uint64_t stalls4, uint64_t flushes4, double cpi4,
    uint64_t cycles6, uint64_t retired6, uint64_t stalls6, uint64_t flushes6, double cpi6,
    double freq4_ghz, double freq6_ghz) {
    
    double time4_us = (cycles4 / (freq4_ghz * 1000.0));
    double time6_us = (cycles6 / (freq6_ghz * 1000.0));
    double speedup = (time6_us > 0.0) ? (time4_us / time6_us) : 1.0;

    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|             4-STAGE VS 6-STAGE PIPELINE ARCHITECTURAL COMPARISON            |\n";
    oss << "+------------------------------+-----------------------+----------------------+\n";
    oss << "| Architectural Metric         | 4-Stage Pipeline      | 6-Stage Pipeline     |\n";
    oss << "+------------------------------+-----------------------+----------------------+\n";

    auto row = [](const std::string& m, const std::string& v4, const std::string& v6) {
        std::ostringstream r;
        r << "| " << std::left << std::setw(28) << m << " | "
          << std::setw(21) << v4 << " | "
          << std::setw(20) << v6 << " |\n";
        return r.str();
    };

    oss << row("Stage Partitioning", "IF-ID/RR-EX-MEM/WB", "IF1-IF2-OF-EX-MA-RW");
    oss << row("Target Clock Frequency", "500 MHz (2.00 ns)", "800 MHz (1.25 ns)");
    oss << row("Total Clock Cycles", std::to_string(cycles4) + " cycles", std::to_string(cycles6) + " cycles");
    oss << row("Instructions Retired", std::to_string(retired4) + " insts", std::to_string(retired6) + " insts");

    std::ostringstream scpi4, scpi6, sipc4, sipc6;
    scpi4 << std::fixed << std::setprecision(3) << cpi4;
    scpi6 << std::fixed << std::setprecision(3) << cpi6;
    oss << row("Cycles Per Inst (CPI)", scpi4.str(), scpi6.str());

    double ipc4 = (cpi4 > 0.0) ? (1.0 / cpi4) : 0.0;
    double ipc6 = (cpi6 > 0.0) ? (1.0 / cpi6) : 0.0;
    sipc4 << std::fixed << std::setprecision(3) << ipc4;
    sipc6 << std::fixed << std::setprecision(3) << ipc6;
    oss << row("Inst Per Cycle (IPC)", sipc4.str(), sipc6.str());

    oss << row("Pipeline Stalls / Flushes",
               std::to_string(stalls4) + " / " + std::to_string(flushes4),
               std::to_string(stalls6) + " / " + std::to_string(flushes6));

    std::ostringstream st4, st6;
    st4 << std::fixed << std::setprecision(3) << time4_us << " us";
    st6 << std::fixed << std::setprecision(3) << time6_us << " us";
    oss << row("Wall-Clock Execution Time", st4.str(), st6.str());
    oss << "+------------------------------+-----------------------+----------------------+\n";

    std::ostringstream sspd;
    sspd << std::fixed << std::setprecision(2) << speedup << "x ("
         << (speedup >= 1.0 ? "6-Stage is faster due to higher clock frequency" : "4-Stage is faster on this code") << ")";
    oss << "| Clock-Adjusted Speedup       : " << std::left << std::setw(44) << sspd.str() << " |\n";
    oss << "+-----------------------------------------------------------------------------+\n";

    return oss.str();
}

} // namespace simplerisc
