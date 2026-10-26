#ifndef SIMPLERISC_EXCEPTION_FRAMEWORK_HPP
#define SIMPLERISC_EXCEPTION_FRAMEWORK_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <functional>

namespace simplerisc {

enum class ExceptionType {
    NONE = 0,
    STACK_OVERFLOW,           // sp grown downward past safe boundary (collision with heap/data)
    STACK_UNDERFLOW,          // sp incremented past top of initial stack
    STACK_ALIGNMENT_FAULT,    // sp is not 4-byte word aligned
    DIVISION_BY_ZERO,         // division or modulo by 0
    ILLEGAL_INSTRUCTION,      // unknown opcode or invalid instruction format
    MEMORY_OUT_OF_BOUNDS,     // address exceeds 64KB memory space
    UNALIGNED_MEMORY_ACCESS   // word read/write address not aligned to 4 bytes
};

struct CPUException {
    ExceptionType type{ExceptionType::NONE};
    addr_t faulting_pc{0};
    addr_t faulting_sp{0};
    addr_t faulting_address{0};
    uint64_t cycle{0};
    std::string message;
};

struct StackBounds {
    addr_t stack_top{0x00003FFC};   // Highest allowable stack address (16380)
    addr_t stack_bottom{0x00002000}; // Lowest allowable stack limit (8192) - 8KB stack zone
    bool enforce_alignment{true};
};

class ExceptionFramework {
public:
    explicit ExceptionFramework(const StackBounds& bounds = StackBounds{});

    void setBounds(const StackBounds& bounds) { bounds_ = bounds; }
    const StackBounds& getBounds() const { return bounds_; }

    // Boundary checks
    bool checkStackPointer(addr_t sp, addr_t pc, uint64_t cycle = 0);
    bool checkMemoryAccess(addr_t addr, addr_t pc, uint64_t cycle = 0);
    bool checkDivision(word_t divisor, addr_t pc, uint64_t cycle = 0);
    bool checkInstruction(Opcode op, addr_t pc, uint64_t cycle = 0);

    // Manual raise
    void raiseException(const CPUException& ex);

    // Queries
    bool hasException() const { return has_fault_; }
    const CPUException& getLatestException() const { return last_fault_; }
    const std::vector<CPUException>& getExceptionHistory() const { return history_; }
    void clear();

    // Formatting & Diagnostics
    std::string dumpExceptionReport() const;
    static std::string exceptionName(ExceptionType type);

private:
    StackBounds bounds_;
    bool has_fault_{false};
    CPUException last_fault_;
    std::vector<CPUException> history_;
};

} // namespace simplerisc

#endif // SIMPLERISC_EXCEPTION_FRAMEWORK_HPP
