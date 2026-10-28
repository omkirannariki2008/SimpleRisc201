#ifndef SIMPLERISC_PIPELINE_VISUALIZER_HPP
#define SIMPLERISC_PIPELINE_VISUALIZER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace simplerisc {

struct PipelineStageSnapshot {
    std::string stage_name;
    addr_t pc{0};
    word_t raw{0};
    std::string instruction_text;
    bool valid{false};
    bool bubble{false};
    bool is_bubble{false};
    bool is_stalled{false};
    uint64_t inst_id{0};
    std::string stall_reason;
    std::string forwarding_info;
    std::string details;
};

struct CycleSnapshot {
    uint64_t cycle{0};
    std::vector<PipelineStageSnapshot> stages;
    bool had_stall{false};
    bool had_flush{false};
    std::string telemetry;
};

/**
 * Pipeline Visualizer rendering cycle-by-cycle pipeline execution state,
 * stage occupancy, space-time timing diagrams,
 * hazard telemetry, and architectural comparisons.
 */
class PipelineVisualizer {
public:
    static std::string truncateStr(const std::string& str, size_t maxLen) {
        if (str.size() <= maxLen) return str;
        if (maxLen <= 2) return str.substr(0, maxLen);
        return str.substr(0, maxLen - 2) + "..";
    }

    // Render active stage occupancy box
    static std::string renderStages(
        const std::vector<PipelineStageSnapshot>& stages,
        uint64_t cycle,
        const std::string& pipelineType = "4-Stage",
        bool last_stall = false,
        bool last_flush = false,
        double cpi = 0.0,
        double ipc = 0.0
    );

    // Render complete cycle-by-cycle stage table and space-time timing diagram
    static std::string renderTrace(
        const std::vector<CycleSnapshot>& history,
        const std::string& pipelineType = "4-Stage"
    );

    // Render space-time timing diagram (Instruction vs Cycles)
    static std::string renderSpaceTimeDiagram(
        const std::vector<CycleSnapshot>& history
    );

    // Render hazard telemetry box
    static std::string renderHazards(
        uint64_t stalls,
        uint64_t flushes,
        uint64_t fwd_hits,
        uint64_t load_use,
        uint64_t structural_avoided = 0
    );

    // Render forwarding telemetry box
    static std::string renderForwarding(
        uint64_t fwd_hits,
        uint64_t store_load_forwards
    );

    // Render 4-Stage vs 6-Stage architectural comparative table
    static std::string renderComparison(
        uint64_t cycles4, uint64_t retired4, uint64_t stalls4, uint64_t flushes4, double cpi4,
        uint64_t cycles6, uint64_t retired6, uint64_t stalls6, uint64_t flushes6, double cpi6,
        double freq4_ghz = 0.5, double freq6_ghz = 0.8
    );
};

} // namespace simplerisc

#endif // SIMPLERISC_PIPELINE_VISUALIZER_HPP
