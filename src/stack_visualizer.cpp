#include "stack_visualizer.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace simplerisc {

std::string StackVisualizer::render(
    const std::vector<word_t>& memory,
    addr_t sp,
    addr_t stack_base,
    addr_t stack_limit,
    size_t max_depth_words) {
    return renderWithContext(memory, sp, 0, 0, stack_base, stack_limit, max_depth_words);
}

std::string StackVisualizer::renderWithContext(
    const std::vector<word_t>& memory,
    addr_t sp,
    addr_t pc,
    addr_t ra,
    addr_t stack_base,
    addr_t stack_limit,
    size_t max_depth_words) {
    
    (void)pc;
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|                     ACTIVE STACK MEMORY FRAME RENDERER                      |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    
    int32_t bytes_used = static_cast<int32_t>(stack_base) - static_cast<int32_t>(sp);
    int32_t words_used = (bytes_used >= 0) ? (bytes_used / 4) : 0;
    int32_t total_capacity_words = (stack_base >= stack_limit) ? (stack_base - stack_limit) / 4 : 0;
    double usage_pct = (total_capacity_words > 0) ? (static_cast<double>(words_used) / total_capacity_words * 100.0) : 0.0;

    oss << "  Stack Base : 0x" << std::hex << std::setw(8) << std::setfill('0') << stack_base
        << " | Current SP : 0x" << std::setw(8) << std::setfill('0') << sp
        << " | Stack Limit : 0x" << std::setw(8) << std::setfill('0') << stack_limit << "\n";

    if (sp > stack_base) {
        oss << "  *** WARNING: STACK UNDERFLOW! SP points above configured stack base ***\n";
    } else if (sp < stack_limit) {
        oss << "  *** CRITICAL ALERT: STACK OVERFLOW! SP breached safe memory boundary limit ***\n";
    } else {
        oss << "  Stack Boundary Status: SECURE & HEALTHY (Used: " << std::dec << words_used
            << " words / " << (words_used * 4) << "B, "
            << std::fixed << std::setprecision(1) << usage_pct << "% capacity)\n";
    }

    oss << "+------------+------------+-------------+--------------+----------------------+\n";
    oss << "| Byte Addr  | Offset(SP) | Value (Hex) | Val (Signed) | Frame Role / Tag     |\n";
    oss << "+------------+------------+-------------+--------------+----------------------+\n";

    if (sp > stack_base) {
        oss << "| [WARNING: Stack Underflow! SP points above configured stack base]           |\n";
        oss << "+-----------------------------------------------------------------------------+\n";
        return oss.str();
    }

    if (sp < stack_limit) {
        oss << "| [DANGER: Stack Overflow! SP breached safe memory boundary limit]            |\n";
        oss << "+-----------------------------------------------------------------------------+\n";
        return oss.str();
    }

    // Iterate from current SP upward to stack_base or max_depth_words
    addr_t cur_addr = sp;
    size_t words_rendered = 0;

    while (cur_addr <= stack_base && words_rendered < max_depth_words) {
        size_t word_idx = cur_addr / 4;
        word_t val = (word_idx < memory.size()) ? memory[word_idx] : 0;
        int32_t offset = static_cast<int32_t>(cur_addr - sp);
        sword_t signed_val = static_cast<sword_t>(val);

        std::string tag = "";
        if (cur_addr == sp) {
            tag = "<== SP [TOS]";
        } else if (val == ra && ra != 0) {
            tag = "[Saved Return Addr]";
        } else if (cur_addr == stack_base) {
            tag = "[Stack Origin / Base]";
        } else if (val > 0 && val < 0x00010000 && (val % 4 == 0)) {
            tag = "Possible Saved RA";
        } else {
            tag = "Local Variable / Spill";
        }

        std::ostringstream s_offset;
        s_offset << "+" << offset << " B";

        oss << "| 0x" << std::right << std::hex << std::setw(8) << std::setfill('0') << cur_addr
            << " | " << std::right << std::dec << std::setw(8) << std::setfill(' ') << s_offset.str() << "   "
            << " | 0x" << std::right << std::hex << std::setw(8) << std::setfill('0') << val << "  "
            << " | " << std::right << std::dec << std::setw(12) << std::setfill(' ') << signed_val << " "
            << " | " << std::left << std::setw(21) << std::setfill(' ') << tag << "|\n";

        cur_addr += 4;
        words_rendered++;
    }

    if (cur_addr <= stack_base) {
        std::ostringstream s_omit;
        s_omit << "... (omitted " << std::dec << ((stack_base - cur_addr) / 4 + 1) << " additional upper stack words) ...";
        oss << "| " << std::left << std::setw(75) << s_omit.str() << " |\n";
    }

    oss << "+------------+------------+-------------+--------------+----------------------+\n";
    oss << "  Stack Growth: High Memory (0x" << std::hex << stack_base << ") ===> Decrementing Downward (SP: 0x" << sp << ")\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

} // namespace simplerisc
