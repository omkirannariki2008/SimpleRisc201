#ifndef SIMPLERISC_HORIZONTAL_CONTROL_UNIT_HPP
#define SIMPLERISC_HORIZONTAL_CONTROL_UNIT_HPP

#include "types.hpp"
#include "alu.hpp"
#include <array>
#include <vector>
#include <string>

namespace simplerisc {

// Horizontal Microinstruction: Wide unencoded control word
// Each bit directly drives hardware gates without an intermediate micro-decoder.
struct HorizontalMicroInstruction {
    // 1. Bus Output Enables (Tri-state gates driving the shared bus)
    bool pc_out{false};
    bool ir_out{false};
    bool regVal_out{false};
    bool aluResult_out{false};
    bool ldResult_out{false};
    bool imm_out{false};

    // 2. Bus Input Enables (Clock enables for destination latches)
    bool pc_in{false};
    bool ir_in{false};
    bool regSrc_in{false};
    bool regData_in{false};
    bool A_in{false};
    bool B_in{false};
    bool mar_in{false};
    bool mdr_in{false};

    // 3. Functional Unit Control
    bool alu_trigger{false};
    ALUOp alu_op{ALUOp::ADD};
    bool mem_read{false};
    bool mem_write{false};
    bool rf_write{false};

    // 4. Microsequencing
    enum class SeqOp : uint8_t {
        NEXT,       // uPC <- uPC + 1
        BRANCH,     // uPC <- next_upc
        BRANCH_COND,// if (cond) uPC <- next_upc else uPC + 1
        SWITCH,     // uPC <- opcode_table[opcode]
        RETURN_ZERO // uPC <- 0 (instruction retired)
    } seq_op{SeqOp::NEXT};

    upc_t next_upc{0};
    int16_t immediate_val{0};
    std::string micro_comment;
};

class HorizontalControlUnit {
public:
    HorizontalControlUnit();
    ~HorizontalControlUnit() = default;

    void reset();
    void loadProgram(const std::vector<word_t>& words, addr_t start_pc = 0);

    // Microprogram execution
    // Returns true when a macro-instruction completes (uPC returns to 0)
    bool stepMicroCycle();
    uint32_t stepInstruction();

    // Architectural state
    word_t getRegister(reg_t r) const;
    void setRegister(reg_t r, word_t val);
    addr_t getPC() const { return pc_; }
    Flags getFlags() const { return flags_; }
    upc_t getUPC() const { return upc_; }
    word_t getSharedBus() const { return shared_bus_; }
    uint64_t getTotalMicroCycles() const { return total_micro_cycles_; }

    // Telemetry
    uint64_t getInstructionsRetired() const { return instructions_retired_; }
    double getMicroCPI() const {
        return (instructions_retired_ > 0)
            ? static_cast<double>(total_micro_cycles_) / static_cast<double>(instructions_retired_)
            : 0.0;
    }

    // Exception accessors
    bool hasException() const { return has_exception_; }
    const std::string& getExceptionMessage() const { return exception_msg_; }
    addr_t getFaultingPC() const { return faulting_pc_; }
    upc_t getFaultingUPC() const { return faulting_upc_; }
    void clearException() {
        has_exception_ = false;
        exception_msg_.clear();
        faulting_pc_ = 0;
        faulting_upc_ = 0;
    }

    // Inspection
    std::string dumpControlState() const;
    std::string dumpMicroinstruction(upc_t addr) const;

    // Architectural comparisons
    static size_t getControlWordBitWidth() { return 64; } // 64-bit unencoded
    static size_t getDecoderGateDelayNs() { return 0; }   // Direct gate drive

private:
    void initHorizontalMicrocode();

    std::array<word_t, NUM_REGISTERS> regs_;
    addr_t pc_{0};
    Flags flags_;
    std::array<word_t, 4096> mem_;

    std::array<word_t, NUM_INTERNAL_REGISTERS> internal_regs_;
    upc_t upc_{0};
    word_t shared_bus_{0};
    uint64_t total_micro_cycles_{0};
    uint64_t instructions_retired_{0};

    // Hardware Exception Trapping
    bool has_exception_{false};
    std::string exception_msg_;
    addr_t faulting_pc_{0};
    upc_t faulting_upc_{0};

    std::vector<HorizontalMicroInstruction> horizontal_rom_;
    std::array<upc_t, 32> opcode_switch_table_;
};

} // namespace simplerisc

#endif // SIMPLERISC_HORIZONTAL_CONTROL_UNIT_HPP
