#ifndef SIMPLERISC_STACK_VISUALIZER_HPP
#define SIMPLERISC_STACK_VISUALIZER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace simplerisc {

struct StackFrameEntry {
    addr_t address{0};
    int32_t offset_from_sp{0};
    word_t value{0};
    std::string annotation; // e.g., "<== SP (TOS)", "Saved RA", "Saved R4", etc.
};

class StackVisualizer {
public:
    StackVisualizer() = default;

    // Render ASCII representation of the active stack frame
    static std::string render(
        const std::vector<word_t>& memory,
        addr_t sp,
        addr_t stack_base = 0x00003FFC,
        addr_t stack_limit = 0x00002000,
        size_t max_depth_words = 12
    );

    // Render active stack frame with explicit register context
    static std::string renderWithContext(
        const std::vector<word_t>& memory,
        addr_t sp,
        addr_t pc,
        addr_t ra,
        addr_t stack_base = 0x00003FFC,
        addr_t stack_limit = 0x00002000,
        size_t max_depth_words = 12
    );
};

} // namespace simplerisc

#endif // SIMPLERISC_STACK_VISUALIZER_HPP
