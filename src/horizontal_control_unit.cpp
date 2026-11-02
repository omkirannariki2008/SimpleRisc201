#include "horizontal_control_unit.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace simplerisc {

HorizontalControlUnit::HorizontalControlUnit() {
    reset();
    initHorizontalMicrocode();
}

void HorizontalControlUnit::reset() {
    regs_.fill(0);
    regs_[SP] = 16380; // Default stack pointer
    pc_ = 0;
    flags_ = Flags{};
    mem_.fill(0);
    internal_regs_.fill(0);
    upc_ = 0;
    shared_bus_ = 0;
    total_micro_cycles_ = 0;
    instructions_retired_ = 0;

    // Reset exception state
    has_exception_ = false;
    exception_msg_.clear();
    faulting_pc_ = 0;
    faulting_upc_ = 0;
}

void HorizontalControlUnit::loadProgram(const std::vector<word_t>& words, addr_t start_pc) {
    pc_ = start_pc;
    for (size_t i = 0; i < words.size() && (start_pc / 4 + i) < mem_.size(); ++i) {
        mem_[start_pc / 4 + i] = words[i];
    }
}

word_t HorizontalControlUnit::getRegister(reg_t r) const {
    if (r < NUM_REGISTERS) return regs_[r];
    return 0;
}

void HorizontalControlUnit::setRegister(reg_t r, word_t val) {
    if (r < NUM_REGISTERS) regs_[r] = val;
}

void HorizontalControlUnit::initHorizontalMicrocode() {
    horizontal_rom_.clear();
    opcode_switch_table_.fill(0);

    // =========================================================================
    // Instruction Fetch Micro-routine (uPC 0 - 2)
    // =========================================================================
    // uPC 0: Transfer PC to Memory Address Register (MAR)
    {
        HorizontalMicroInstruction mi;
        mi.pc_out = true;
        mi.mar_in = true;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
        mi.micro_comment = "Fetch: mar <- pc";
        horizontal_rom_.push_back(mi);
    }
    // uPC 1: Read memory into MDR and increment PC by 4
    {
        HorizontalMicroInstruction mi;
        mi.mem_read = true;
        mi.alu_trigger = true;
        mi.alu_op = ALUOp::ADD;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
        mi.micro_comment = "Fetch: mdr <- mem[mar], pc <- pc + 4";
        horizontal_rom_.push_back(mi);
    }
    // uPC 2: Load Instruction Register (IR) from MDR and switch on Opcode
    {
        HorizontalMicroInstruction mi;
        mi.ldResult_out = true;
        mi.ir_in = true;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::SWITCH;
        mi.micro_comment = "Fetch: ir <- mdr, uPC <- opcode_switch_table[op]";
        horizontal_rom_.push_back(mi);
    }

    // =========================================================================
    // Opcode Handlers
    // =========================================================================
    auto addAluRoutine = [&](Opcode op, ALUOp alu_op, const std::string& name) {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[op] = start;

        // Step 1: Read RS1 and load into Operand A
        {
            HorizontalMicroInstruction mi;
            mi.regVal_out = true;
            mi.A_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = name + ": A <- [rs1]";
            horizontal_rom_.push_back(mi);
        }
        // Step 2: Read RS2 or Imm and load into Operand B
        {
            HorizontalMicroInstruction mi;
            mi.imm_out = true;
            mi.B_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = name + ": B <- rs2/imm";
            horizontal_rom_.push_back(mi);
        }
        // Step 3: Trigger ALU and write back result to destination register RD
        {
            HorizontalMicroInstruction mi;
            mi.alu_trigger = true;
            mi.alu_op = alu_op;
            mi.aluResult_out = true;
            mi.regData_in = true;
            mi.rf_write = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
            mi.micro_comment = name + ": rd <- alu_res, retire instruction";
            horizontal_rom_.push_back(mi);
        }
    };

    addAluRoutine(OP_ADD, ALUOp::ADD, "ADD");
    addAluRoutine(OP_SUB, ALUOp::SUB, "SUB");
    addAluRoutine(OP_MUL, ALUOp::MUL, "MUL");
    addAluRoutine(OP_DIV, ALUOp::DIV, "DIV");
    addAluRoutine(OP_MOD, ALUOp::MOD, "MOD");
    addAluRoutine(OP_AND, ALUOp::AND, "AND");
    addAluRoutine(OP_OR,  ALUOp::OR,  "OR");
    addAluRoutine(OP_NOT, ALUOp::NOT, "NOT");
    addAluRoutine(OP_MOV, ALUOp::MOV, "MOV");
    addAluRoutine(OP_LSL, ALUOp::LSL, "LSL");
    addAluRoutine(OP_LSR, ALUOp::LSR, "LSR");
    addAluRoutine(OP_ASR, ALUOp::ASR, "ASR");
    addAluRoutine(OP_FDIV, ALUOp::FDIV, "FDIV");

    // =========================================================================
    // Unary Routines: I2D and D2I (single-source, no second operand)
    // =========================================================================
    auto addUnaryRoutine = [&](Opcode op, ALUOp alu_op, const std::string& name) {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[op] = start;

        // Step 1: Read RS1 and load into Operand A
        {
            HorizontalMicroInstruction mi;
            mi.regVal_out = true;
            mi.A_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = name + ": A <- [rs1]";
            horizontal_rom_.push_back(mi);
        }
        // Step 2: Trigger ALU (unary) and write back result to destination register RD
        {
            HorizontalMicroInstruction mi;
            mi.alu_trigger = true;
            mi.alu_op = alu_op;
            mi.aluResult_out = true;
            mi.regData_in = true;
            mi.rf_write = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
            mi.micro_comment = name + ": rd <- alu_res(A), retire instruction";
            horizontal_rom_.push_back(mi);
        }
    };

    addUnaryRoutine(OP_I2D, ALUOp::I2D, "I2D");
    addUnaryRoutine(OP_D2I, ALUOp::D2I, "D2I");

    // CMP Handler: does not write back to RD, only updates condition flags
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_CMP] = start;
        {
            HorizontalMicroInstruction mi;
            mi.regVal_out = true;
            mi.A_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "CMP: A <- [rs1]";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.imm_out = true;
            mi.B_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "CMP: B <- rs2/imm";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.alu_trigger = true;
            mi.alu_op = ALUOp::CMP;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
            mi.micro_comment = "CMP: update flags.E & flags.GT, retire";
            horizontal_rom_.push_back(mi);
        }
    }

    // NOP Handler: immediate return
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_NOP] = start;
        HorizontalMicroInstruction mi;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "NOP: retire";
        horizontal_rom_.push_back(mi);
    }

    // Branch (B) Handler
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_B] = start;
        HorizontalMicroInstruction mi;
        mi.pc_in = true;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "B: pc <- branchTarget, retire";
        horizontal_rom_.push_back(mi);
    }

    // Conditional Branches (BEQ, BGT)
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_BEQ] = start;
        HorizontalMicroInstruction mi;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "BEQ: if (flags.E) pc <- branchTarget, retire";
        horizontal_rom_.push_back(mi);
    }
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_BGT] = start;
        HorizontalMicroInstruction mi;
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "BGT: if (flags.GT) pc <- branchTarget, retire";
        horizontal_rom_.push_back(mi);
    }

    // Load (LD) Handler
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_LD] = start;
        {
            HorizontalMicroInstruction mi;
            mi.regVal_out = true;
            mi.A_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "LD: A <- [rs1]";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.imm_out = true;
            mi.B_in = true;
            mi.alu_trigger = true;
            mi.alu_op = ALUOp::ADD;
            mi.mar_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "LD: mar <- A + immx";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.mem_read = true;
            mi.rf_write = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
            mi.micro_comment = "LD: rd <- mem[mar], retire";
            horizontal_rom_.push_back(mi);
        }
    }

    // Store (ST) Handler
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_ST] = start;
        {
            HorizontalMicroInstruction mi;
            mi.regVal_out = true;
            mi.A_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "ST: A <- [rs1]";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.imm_out = true;
            mi.B_in = true;
            mi.alu_trigger = true;
            mi.alu_op = ALUOp::ADD;
            mi.mar_in = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::NEXT;
            mi.micro_comment = "ST: mar <- A + immx";
            horizontal_rom_.push_back(mi);
        }
        {
            HorizontalMicroInstruction mi;
            mi.mem_write = true;
            mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
            mi.micro_comment = "ST: mem[mar] <- [rd], retire";
            horizontal_rom_.push_back(mi);
        }
    }

    // CALL & RET Handlers
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_CALL] = start;
        HorizontalMicroInstruction mi;
        mi.rf_write = true; // saves ra
        mi.pc_in = true;    // updates pc
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "CALL: ra <- pc, pc <- branchTarget, retire";
        horizontal_rom_.push_back(mi);
    }
    {
        upc_t start = static_cast<upc_t>(horizontal_rom_.size());
        opcode_switch_table_[OP_RET] = start;
        HorizontalMicroInstruction mi;
        mi.pc_in = true; // pc <- ra
        mi.seq_op = HorizontalMicroInstruction::SeqOp::RETURN_ZERO;
        mi.micro_comment = "RET: pc <- ra, retire";
        horizontal_rom_.push_back(mi);
    }
}

bool HorizontalControlUnit::stepMicroCycle() {
    if (upc_ >= horizontal_rom_.size()) {
        upc_ = 0;
        return true;
    }

    const auto& mi = horizontal_rom_[upc_];
    total_micro_cycles_++;

    // Decode current instruction when fetching completes
    word_t ir = internal_regs_[static_cast<size_t>(InternalReg::IR)];
    Instruction decoded = Instruction::decode(ir, pc_);

    // Bus arbitration: single driver
    if (mi.pc_out) {
        shared_bus_ = pc_;
    } else if (mi.imm_out) {
        shared_bus_ = decoded.immx;
    } else if (mi.regVal_out) {
        reg_t src = (upc_ == 3) ? decoded.rs1 : decoded.rs2;
        shared_bus_ = regs_[src];
    } else if (mi.aluResult_out) {
        shared_bus_ = internal_regs_[static_cast<size_t>(InternalReg::ALU_RESULT)];
    } else if (mi.ldResult_out) {
        shared_bus_ = internal_regs_[static_cast<size_t>(InternalReg::LD_RESULT)];
    }

    // Bus load latches
    if (mi.mar_in) {
        internal_regs_[static_cast<size_t>(InternalReg::MAR)] = shared_bus_;
    }
    if (mi.A_in) {
        internal_regs_[static_cast<size_t>(InternalReg::A)] = shared_bus_;
    }
    if (mi.B_in) {
        internal_regs_[static_cast<size_t>(InternalReg::B)] = shared_bus_;
    }
    if (mi.ir_in) {
        internal_regs_[static_cast<size_t>(InternalReg::IR)] = shared_bus_;
    }

    // Functional unit operations
    if (mi.mem_read) {
        addr_t addr = internal_regs_[static_cast<size_t>(InternalReg::MAR)];
        word_t val = 0;
        if (addr / 4 < mem_.size()) {
            val = mem_[addr / 4];
        }
        internal_regs_[static_cast<size_t>(InternalReg::LD_RESULT)] = val;
    }
    if (mi.mem_write) {
        addr_t addr = internal_regs_[static_cast<size_t>(InternalReg::MAR)];
        if (addr / 4 < mem_.size()) {
            mem_[addr / 4] = regs_[decoded.rd];
        }
    }
    if (mi.alu_trigger) {
        if (upc_ == 1) {
            // PC increment during fetch
            pc_ += 4;
        } else {
            word_t a = internal_regs_[static_cast<size_t>(InternalReg::A)];
            word_t b = (decoded.I) ? decoded.immx : internal_regs_[static_cast<size_t>(InternalReg::B)];
            try {
                ALUResult res = ALU::execute(mi.alu_op, a, b, flags_);
                internal_regs_[static_cast<size_t>(InternalReg::ALU_RESULT)] = res.result;
                if (mi.alu_op == ALUOp::CMP) {
                    flags_ = res.flags;
                }
            } catch (const SimpleRISCException& ex) {
                has_exception_ = true;
                exception_msg_ = ex.what();
                faulting_pc_ = pc_;
                faulting_upc_ = upc_;
                // Retire the faulting instruction gracefully
                upc_ = 0;
                instructions_retired_++;
                return true;
            } catch (const std::exception& ex) {
                has_exception_ = true;
                exception_msg_ = ex.what();
                faulting_pc_ = pc_;
                faulting_upc_ = upc_;
                upc_ = 0;
                instructions_retired_++;
                return true;
            }
        }
    }
    if (mi.rf_write) {
        if (decoded.opcode == OP_CALL) {
            regs_[RA] = pc_;
            pc_ = decoded.branch_target;
        } else if (decoded.opcode == OP_LD) {
            regs_[decoded.rd] = internal_regs_[static_cast<size_t>(InternalReg::LD_RESULT)];
        } else {
            word_t res = internal_regs_[static_cast<size_t>(InternalReg::ALU_RESULT)];
            regs_[decoded.rd] = res;
        }
    }
    if (mi.pc_in) {
        if (decoded.opcode == OP_B) {
            pc_ = decoded.branch_target;
        } else if (decoded.opcode == OP_RET) {
            pc_ = regs_[RA];
        }
    }

    // Microsequencing
    bool instruction_retired = false;
    switch (mi.seq_op) {
        case HorizontalMicroInstruction::SeqOp::NEXT:
            upc_++;
            break;
        case HorizontalMicroInstruction::SeqOp::BRANCH:
            upc_ = mi.next_upc;
            break;
        case HorizontalMicroInstruction::SeqOp::SWITCH: {
            word_t fetched_ir = internal_regs_[static_cast<size_t>(InternalReg::IR)];
            Instruction inst = Instruction::decode(fetched_ir, pc_);
            uint8_t op_idx = static_cast<uint8_t>(inst.opcode);
            upc_ = (op_idx < 32) ? opcode_switch_table_[op_idx] : 0;
            break;
        }
        case HorizontalMicroInstruction::SeqOp::RETURN_ZERO:
            if (decoded.opcode == OP_BEQ && flags_.E) {
                pc_ = decoded.branch_target;
            } else if (decoded.opcode == OP_BGT && flags_.GT) {
                pc_ = decoded.branch_target;
            }
            upc_ = 0;
            instruction_retired = true;
            instructions_retired_++;
            break;
        default:
            upc_ = 0;
            instruction_retired = true;
            instructions_retired_++;
            break;
    }

    return instruction_retired;
}

uint32_t HorizontalControlUnit::stepInstruction() {
    uint32_t cycles = 0;
    while (!stepMicroCycle()) {
        cycles++;
        if (cycles > 100) break; // infinite loop guard
    }
    cycles++;
    return cycles;
}

std::string HorizontalControlUnit::dumpControlState() const {
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|                HORIZONTAL MICROPROGRAMMED CONTROL UNIT STATE                |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    std::ostringstream l1;
    l1 << "uPC: " << std::dec << std::setw(3) << upc_ 
       << " | PC: " << toHex8(pc_)
       << " | Bus: " << toHex8(shared_bus_)
       << " | Total uCycles: " << std::dec << total_micro_cycles_;
    oss << "|  " << std::left << std::setw(73) << l1.str() << " |\n";

    std::ostringstream l2;
    l2 << "Flags: [E=" << flags_.E << " GT=" << flags_.GT << " C=" << flags_.C << " V=" << flags_.V << "] "
       << "| Control Word Width: 64 bits (Unencoded Direct Gate Drive)";
    oss << "|  " << std::left << std::setw(73) << l2.str() << " |\n";

    std::ostringstream l3;
    l3 << "Instructions Retired: " << std::dec << instructions_retired_
       << " | Micro-CPI: " << std::fixed << std::setprecision(2) << getMicroCPI();
    oss << "|  " << std::left << std::setw(73) << l3.str() << " |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

std::string HorizontalControlUnit::dumpMicroinstruction(upc_t addr) const {
    if (addr >= horizontal_rom_.size()) return "Invalid uPC";
    const auto& mi = horizontal_rom_[addr];
    std::ostringstream oss;
    oss << "[uPC " << std::dec << std::setw(2) << addr << "] " << mi.micro_comment;
    return oss.str();
}

} // namespace simplerisc
