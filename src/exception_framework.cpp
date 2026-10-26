#include "exception_framework.hpp"
#include <sstream>
#include <iomanip>
#include <bitset>

namespace simplerisc {

ExceptionFramework::ExceptionFramework(const StackBounds& bounds)
    : bounds_(bounds), has_fault_(false) {}

std::string ExceptionFramework::exceptionName(ExceptionType type) {
    switch (type) {
        case ExceptionType::NONE:                    return "NONE";
        case ExceptionType::STACK_OVERFLOW:         return "STACK_OVERFLOW";
        case ExceptionType::STACK_UNDERFLOW:        return "STACK_UNDERFLOW";
        case ExceptionType::STACK_ALIGNMENT_FAULT:   return "STACK_ALIGNMENT_FAULT";
        case ExceptionType::DIVISION_BY_ZERO:        return "DIVISION_BY_ZERO";
        case ExceptionType::ILLEGAL_INSTRUCTION:     return "ILLEGAL_INSTRUCTION";
        case ExceptionType::MEMORY_OUT_OF_BOUNDS:    return "MEMORY_OUT_OF_BOUNDS";
        case ExceptionType::UNALIGNED_MEMORY_ACCESS: return "UNALIGNED_MEMORY_ACCESS";
        default:                                     return "UNKNOWN_EXCEPTION";
    }
}

bool ExceptionFramework::checkStackPointer(addr_t sp, addr_t pc, uint64_t cycle) {
    if (bounds_.enforce_alignment && (sp % 4 != 0)) {
        CPUException ex;
        ex.type = ExceptionType::STACK_ALIGNMENT_FAULT;
        ex.faulting_pc = pc;
        ex.faulting_sp = sp;
        ex.faulting_address = sp;
        ex.cycle = cycle;
        ex.message = "Unaligned stack pointer (sp = 0x" + std::to_string(sp) + " is not 4-byte aligned)";
        raiseException(ex);
        return false;
    }

    if (sp < bounds_.stack_bottom) {
        CPUException ex;
        ex.type = ExceptionType::STACK_OVERFLOW;
        ex.faulting_pc = pc;
        ex.faulting_sp = sp;
        ex.faulting_address = sp;
        ex.cycle = cycle;
        std::ostringstream oss;
        oss << "Stack Overflow detected! SP (0x" << std::hex << sp 
            << ") exceeded lower safety limit (0x" << bounds_.stack_bottom << ")";
        ex.message = oss.str();
        raiseException(ex);
        return false;
    }

    if (sp > bounds_.stack_top) {
        CPUException ex;
        ex.type = ExceptionType::STACK_UNDERFLOW;
        ex.faulting_pc = pc;
        ex.faulting_sp = sp;
        ex.faulting_address = sp;
        ex.cycle = cycle;
        std::ostringstream oss;
        oss << "Stack Underflow detected! SP (0x" << std::hex << sp 
            << ") popped past initial stack top (0x" << bounds_.stack_top << ")";
        ex.message = oss.str();
        raiseException(ex);
        return false;
    }

    return true;
}

bool ExceptionFramework::checkMemoryAccess(addr_t addr, addr_t pc, uint64_t cycle) {
    if (addr >= MEMORY_SIZE) {
        CPUException ex;
        ex.type = ExceptionType::MEMORY_OUT_OF_BOUNDS;
        ex.faulting_pc = pc;
        ex.faulting_address = addr;
        ex.cycle = cycle;
        std::ostringstream oss;
        oss << "Memory access out of bounds: Address 0x" << std::hex << addr 
            << " >= MEMORY_SIZE (0x" << MEMORY_SIZE << ")";
        ex.message = oss.str();
        raiseException(ex);
        return false;
    }

    if (addr % 4 != 0) {
        CPUException ex;
        ex.type = ExceptionType::UNALIGNED_MEMORY_ACCESS;
        ex.faulting_pc = pc;
        ex.faulting_address = addr;
        ex.cycle = cycle;
        std::ostringstream oss;
        oss << "Unaligned 32-bit memory word access at 0x" << std::hex << addr;
        ex.message = oss.str();
        raiseException(ex);
        return false;
    }

    return true;
}

bool ExceptionFramework::checkDivision(word_t divisor, addr_t pc, uint64_t cycle) {
    if (divisor == 0) {
        CPUException ex;
        ex.type = ExceptionType::DIVISION_BY_ZERO;
        ex.faulting_pc = pc;
        ex.faulting_address = pc;
        ex.cycle = cycle;
        ex.message = "Division or modulo by zero executed at PC 0x" + std::to_string(pc);
        raiseException(ex);
        return false;
    }
    return true;
}

bool ExceptionFramework::checkInstruction(Opcode op, addr_t pc, uint64_t cycle) {
    uint8_t op_val = static_cast<uint8_t>(op);
    if (op_val > OP_RET) {
        CPUException ex;
        ex.type = ExceptionType::ILLEGAL_INSTRUCTION;
        ex.faulting_pc = pc;
        ex.cycle = cycle;
        std::ostringstream oss;
        oss << "Illegal instruction opcode: 0b" << std::bitset<5>(op_val) 
            << " (" << static_cast<int>(op_val) << ") at PC 0x" << std::hex << pc;
        ex.message = oss.str();
        raiseException(ex);
        return false;
    }
    return true;
}

void ExceptionFramework::raiseException(const CPUException& ex) {
    has_fault_ = true;
    last_fault_ = ex;
    history_.push_back(ex);
}

void ExceptionFramework::clear() {
    has_fault_ = false;
    last_fault_ = CPUException{};
    history_.clear();
}

std::string ExceptionFramework::dumpExceptionReport() const {
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|                    RUNTIME HARDWARE EXCEPTION TRAP                          |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    if (!has_fault_) {
        oss << "| Status: NORMAL (No active hardware exceptions trapped)                      |\n";
        oss << "+-----------------------------------------------------------------------------+\n";
        return oss.str();
    }

    auto row = [](const std::string& lbl, const std::string& val) {
        std::ostringstream r;
        r << "| " << std::left << std::setw(20) << lbl << ": "
          << std::left << std::setw(53) << val << " |\n";
        return r.str();
    };

    oss << row("Exception Type", exceptionName(last_fault_.type));

    std::ostringstream spc, ssp;
    spc << toHex8(last_fault_.faulting_pc) << " (" << std::dec << last_fault_.faulting_pc << ")";
    ssp << toHex8(last_fault_.faulting_sp) << " (" << std::dec << last_fault_.faulting_sp << ")";

    oss << row("Faulting PC", spc.str());
    oss << row("Faulting SP", ssp.str());
    oss << row("Clock Cycle", std::to_string(last_fault_.cycle));
    oss << row("Diagnostic Info", last_fault_.message);
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

} // namespace simplerisc
