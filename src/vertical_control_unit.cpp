#include "vertical_control_unit.hpp"
#include <sstream>
#include <iomanip>

namespace simplerisc {

VerticalControlUnit::VerticalControlUnit() {
    initMicrocode();
    reset();
}

void VerticalControlUnit::reset() {
    regs_.fill(0);
    regs_[SP] = 0x00003FFC; // Top of 16KB local memory
    pc_ = TEXT_BASE;
    flags_ = Flags{};
    mem_.fill(0);

    internal_regs_.fill(0);
    upc_ = 0;
    shared_bus_ = 0;
    signals_ = BusSignals{};

    // Reset telemetry
    total_micro_cycles_ = 0;
    instructions_retired_ = 0;

    // Reset exception state
    has_exception_ = false;
    exception_msg_.clear();
    faulting_pc_ = 0;
    faulting_upc_ = 0;
}

void VerticalControlUnit::loadProgram(const std::vector<word_t>& words, addr_t start_pc) {
    reset();
    pc_ = start_pc;
    for (size_t i = 0; i < words.size() && (start_pc / 4 + i) < mem_.size(); ++i) {
        mem_[(start_pc / 4) + i] = words[i];
    }
}

word_t VerticalControlUnit::getRegister(reg_t r) const {
    if (r < NUM_REGISTERS) return regs_[r];
    return 0;
}

void VerticalControlUnit::setRegister(reg_t r, word_t val) {
    if (r < NUM_REGISTERS) regs_[r] = val;
}

word_t VerticalControlUnit::getInternalReg(InternalReg reg) const {
    size_t idx = static_cast<size_t>(reg);
    if (idx < NUM_INTERNAL_REGISTERS) return internal_regs_[idx];
    return 0;
}

void VerticalControlUnit::initMicrocode() {
    micro_rom_.clear();
    opcode_switch_table_.fill(0);

    auto emit = [&](MicroInstruction mi) -> uint16_t {
        uint16_t addr = static_cast<uint16_t>(micro_rom_.size());
        micro_rom_.push_back(mi);
        return addr;
    };

    auto emitMov = [&](InternalReg dest, InternalReg src, uint16_t args = 0) {
        MicroInstruction mi;
        mi.type = MicroOp::MMOV;
        mi.dest = dest;
        mi.src = src;
        mi.args = args;
        return emit(mi);
    };

    auto emitMovi = [&](InternalReg dest, int16_t imm) {
        MicroInstruction mi;
        mi.type = MicroOp::MMOVI;
        mi.dest = dest;
        mi.immediate = imm;
        return emit(mi);
    };

    auto emitAdd = [&](InternalReg dest, int16_t imm) {
        MicroInstruction mi;
        mi.type = MicroOp::MADD;
        mi.dest = dest;
        mi.immediate = imm;
        return emit(mi);
    };

    auto emitBeq = [&](InternalReg reg, int16_t imm, uint16_t target) {
        MicroInstruction mi;
        mi.type = MicroOp::MBEQ;
        mi.dest = reg;
        mi.immediate = imm;
        mi.branch_target = target;
        return emit(mi);
    };

    auto emitMb = [&](uint16_t target) {
        MicroInstruction mi;
        mi.type = MicroOp::MB;
        mi.branch_target = target;
        return emit(mi);
    };

    // 0..3: Common Fetch & Decode (Table 9.8)
    MicroInstruction mi_load; mi_load.type = MicroOp::MLOAD_IR; emit(mi_load);
    emitAdd(InternalReg::PC, 4);
    MicroInstruction mi_dec; mi_dec.type = MicroOp::MDECODE; emit(mi_dec);
    MicroInstruction mi_sw; mi_sw.type = MicroOp::MSWITCH; emit(mi_sw);

    // ALU template
    auto emitAluOp = [&](Opcode op, ALUOp alu_op) {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[op] = start;

        emitMov(InternalReg::REG_SRC, InternalReg::RS1);
        emitMov(InternalReg::A, InternalReg::REG_VAL);

        uint16_t branch_idx = static_cast<uint16_t>(micro_rom_.size());
        emitBeq(InternalReg::I_BIT, 1, 0);

        emitMov(InternalReg::REG_SRC, InternalReg::RS2);
        emitMov(InternalReg::B, InternalReg::REG_VAL);
        uint16_t to_exec = static_cast<uint16_t>(micro_rom_.size());
        emitMb(0);

        uint16_t imm_target = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::B, InternalReg::IMMX);

        uint16_t exec_target = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::ALU_RESULT, InternalReg::A, static_cast<uint16_t>(alu_op));
        emitMov(InternalReg::REG_SRC, InternalReg::RD);
        emitMov(InternalReg::REG_DATA, InternalReg::ALU_RESULT);
        emitMb(0);

        micro_rom_[branch_idx].branch_target = imm_target;
        micro_rom_[to_exec].branch_target = exec_target;
    };

    emitAluOp(OP_ADD, ALUOp::ADD);
    emitAluOp(OP_SUB, ALUOp::SUB);
    emitAluOp(OP_MUL, ALUOp::MUL);
    emitAluOp(OP_DIV, ALUOp::DIV);
    emitAluOp(OP_MOD, ALUOp::MOD);
    emitAluOp(OP_AND, ALUOp::AND);
    emitAluOp(OP_OR,  ALUOp::OR);
    emitAluOp(OP_LSL, ALUOp::LSL);
    emitAluOp(OP_LSR, ALUOp::LSR);
    emitAluOp(OP_ASR, ALUOp::ASR);
    emitAluOp(OP_FDIV, ALUOp::FDIV);

    // Unary Conversion Routines: I2D, D2I, and NOT
    auto emitUnaryOp = [&](Opcode op, ALUOp alu_op) {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[op] = start;
        emitMov(InternalReg::REG_SRC, InternalReg::RS1);
        emitMov(InternalReg::A, InternalReg::REG_VAL);
        emitMov(InternalReg::ALU_RESULT, InternalReg::A, static_cast<uint16_t>(alu_op));
        emitMov(InternalReg::REG_SRC, InternalReg::RD);
        emitMov(InternalReg::REG_DATA, InternalReg::ALU_RESULT);
        emitMb(0);
    };
    emitUnaryOp(OP_I2D, ALUOp::I2D);
    emitUnaryOp(OP_D2I, ALUOp::D2I);
    emitUnaryOp(OP_NOT, ALUOp::NOT);

    // CMP
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_CMP] = start;
        emitMov(InternalReg::REG_SRC, InternalReg::RS1);
        emitMov(InternalReg::A, InternalReg::REG_VAL);
        uint16_t b_idx = static_cast<uint16_t>(micro_rom_.size());
        emitBeq(InternalReg::I_BIT, 1, 0);
        emitMov(InternalReg::REG_SRC, InternalReg::RS2);
        emitMov(InternalReg::B, InternalReg::REG_VAL);
        uint16_t to_ex = static_cast<uint16_t>(micro_rom_.size());
        emitMb(0);
        uint16_t imm_t = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::B, InternalReg::IMMX);
        uint16_t ex_t = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::ALU_RESULT, InternalReg::A, static_cast<uint16_t>(ALUOp::CMP));
        emitMb(0);

        micro_rom_[b_idx].branch_target = imm_t;
        micro_rom_[to_ex].branch_target = ex_t;
    }

    // MOV
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_MOV] = start;
        uint16_t b_idx = static_cast<uint16_t>(micro_rom_.size());
        emitBeq(InternalReg::I_BIT, 1, 0);
        emitMov(InternalReg::REG_SRC, InternalReg::RS2);
        emitMov(InternalReg::B, InternalReg::REG_VAL);
        uint16_t to_ex = static_cast<uint16_t>(micro_rom_.size());
        emitMb(0);
        uint16_t imm_t = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::B, InternalReg::IMMX);
        uint16_t ex_t = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::ALU_RESULT, InternalReg::B, static_cast<uint16_t>(ALUOp::MOV));
        emitMov(InternalReg::REG_SRC, InternalReg::RD);
        emitMov(InternalReg::REG_DATA, InternalReg::ALU_RESULT);
        emitMb(0);

        micro_rom_[b_idx].branch_target = imm_t;
        micro_rom_[to_ex].branch_target = ex_t;
    }

    // NOP
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_NOP] = start;
        emitMb(0);
    }

    // LD
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_LD] = start;
        emitMov(InternalReg::REG_SRC, InternalReg::RS1);
        emitMov(InternalReg::A, InternalReg::REG_VAL);
        emitMov(InternalReg::B, InternalReg::IMMX);
        emitMov(InternalReg::MAR, InternalReg::A, static_cast<uint16_t>(ALUOp::ADD));
        emitMov(InternalReg::LD_RESULT, InternalReg::MAR, 1);
        emitMov(InternalReg::REG_SRC, InternalReg::RD);
        emitMov(InternalReg::REG_DATA, InternalReg::LD_RESULT);
        emitMb(0);
    }

    // ST
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_ST] = start;
        emitMov(InternalReg::REG_SRC, InternalReg::RS1);
        emitMov(InternalReg::A, InternalReg::REG_VAL);
        emitMov(InternalReg::B, InternalReg::IMMX);
        emitMov(InternalReg::MAR, InternalReg::A, static_cast<uint16_t>(ALUOp::ADD));
        emitMov(InternalReg::REG_SRC, InternalReg::RD);
        emitMov(InternalReg::MDR, InternalReg::REG_VAL);
        emitMov(InternalReg::MAR, InternalReg::MDR, 2);
        emitMb(0);
    }

    // BEQ
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_BEQ] = start;
        uint16_t beq_idx = static_cast<uint16_t>(micro_rom_.size());
        emitBeq(InternalReg::FLAGS_E, 1, 0);
        emitMb(0);
        uint16_t taken = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::PC, InternalReg::BRANCH_TGT);
        emitMb(0);
        micro_rom_[beq_idx].branch_target = taken;
    }

    // BGT
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_BGT] = start;
        uint16_t bgt_idx = static_cast<uint16_t>(micro_rom_.size());
        emitBeq(InternalReg::FLAGS_GT, 1, 0);
        emitMb(0);
        uint16_t taken = static_cast<uint16_t>(micro_rom_.size());
        emitMov(InternalReg::PC, InternalReg::BRANCH_TGT);
        emitMb(0);
        micro_rom_[bgt_idx].branch_target = taken;
    }

    // B
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_B] = start;
        emitMov(InternalReg::PC, InternalReg::BRANCH_TGT);
        emitMb(0);
    }

    // CALL
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_CALL] = start;
        emitMovi(InternalReg::REG_SRC, 15);
        emitMov(InternalReg::REG_DATA, InternalReg::PC);
        emitMov(InternalReg::PC, InternalReg::BRANCH_TGT);
        emitMb(0);
    }

    // RET
    {
        uint16_t start = static_cast<uint16_t>(micro_rom_.size());
        opcode_switch_table_[OP_RET] = start;
        emitMovi(InternalReg::REG_SRC, 15);
        emitMov(InternalReg::PC, InternalReg::REG_VAL);
        emitMb(0);
    }
}

bool VerticalControlUnit::stepMicroCycle() {
    // Telemetry: count every micro-cycle
    total_micro_cycles_++;

    if (upc_ >= micro_rom_.size()) {
        upc_ = 0;
        instructions_retired_++;
        return true;
    }

    const MicroInstruction& mi = micro_rom_[upc_];
    signals_ = BusSignals{};
    upc_t next_upc = upc_ + 1;

    switch (mi.type) {
        case MicroOp::MLOAD_IR: {
            signals_.pc_out = true;
            signals_.ir_in = true;
            size_t word_idx = (pc_ / 4) % mem_.size();
            word_t word = mem_[word_idx];
            internal_regs_[static_cast<size_t>(InternalReg::PC)] = pc_;
            internal_regs_[static_cast<size_t>(InternalReg::IR)] = word;
            shared_bus_ = word;
            break;
        }

        case MicroOp::MDECODE: {
            word_t word = internal_regs_[static_cast<size_t>(InternalReg::IR)];
            Instruction inst = Instruction::decode(word, pc_ - 4);
            internal_regs_[static_cast<size_t>(InternalReg::I_BIT)]      = inst.I ? 1 : 0;
            internal_regs_[static_cast<size_t>(InternalReg::RD)]         = inst.rd;
            internal_regs_[static_cast<size_t>(InternalReg::RS1)]        = inst.rs1;
            internal_regs_[static_cast<size_t>(InternalReg::RS2)]        = inst.rs2;
            internal_regs_[static_cast<size_t>(InternalReg::IMMX)]       = inst.immx;
            internal_regs_[static_cast<size_t>(InternalReg::BRANCH_TGT)] = inst.branch_target;
            shared_bus_ = word;
            break;
        }

        case MicroOp::MSWITCH: {
            word_t word = internal_regs_[static_cast<size_t>(InternalReg::IR)];
            uint8_t op = (word >> 27) & 0x1F;
            if (op < opcode_switch_table_.size()) {
                next_upc = opcode_switch_table_[op];
            } else {
                next_upc = 0;
            }
            shared_bus_ = next_upc;
            break;
        }

        case MicroOp::MMOV: {
            if (mi.src == InternalReg::REG_VAL) {
                reg_t r = static_cast<reg_t>(internal_regs_[static_cast<size_t>(InternalReg::REG_SRC)] & 0xF);
                shared_bus_ = regs_[r];
                internal_regs_[static_cast<size_t>(InternalReg::REG_VAL)] = shared_bus_;
                signals_.regVal_out = true;
            } else {
                shared_bus_ = internal_regs_[static_cast<size_t>(mi.src)];
            }

            if (mi.dest == InternalReg::REG_DATA) {
                signals_.regData_in = true;
                internal_regs_[static_cast<size_t>(InternalReg::REG_DATA)] = shared_bus_;
                reg_t r = static_cast<reg_t>(internal_regs_[static_cast<size_t>(InternalReg::REG_SRC)] & 0xF);
                regs_[r] = shared_bus_;
            } else if (mi.dest == InternalReg::ALU_RESULT) {
                signals_.alu_trigger = true;
                ALUOp op = static_cast<ALUOp>(mi.args);
                signals_.alu_op = op;
                word_t a = internal_regs_[static_cast<size_t>(InternalReg::A)];
                word_t b = internal_regs_[static_cast<size_t>(InternalReg::B)];
                try {
                    ALUResult alu_res = ALU::execute(op, a, b, flags_);
                    shared_bus_ = alu_res.result;
                    internal_regs_[static_cast<size_t>(InternalReg::ALU_RESULT)] = alu_res.result;

                    if (op == ALUOp::CMP) {
                        internal_regs_[static_cast<size_t>(InternalReg::FLAGS_E)]  = alu_res.flags.E ? 1 : 0;
                        internal_regs_[static_cast<size_t>(InternalReg::FLAGS_GT)] = alu_res.flags.GT ? 1 : 0;
                        flags_ = alu_res.flags;
                    }
                } catch (const SimpleRISCException& ex) {
                    has_exception_ = true;
                    exception_msg_ = ex.what();
                    faulting_pc_ = pc_;
                    faulting_upc_ = upc_;
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
            } else if (mi.dest == InternalReg::MAR && mi.args == static_cast<uint16_t>(ALUOp::ADD)) {
                word_t a = internal_regs_[static_cast<size_t>(InternalReg::A)];
                word_t b = internal_regs_[static_cast<size_t>(InternalReg::B)];
                shared_bus_ = a + b;
                internal_regs_[static_cast<size_t>(InternalReg::MAR)] = shared_bus_;
                signals_.mar_in = true;
            } else if (mi.dest == InternalReg::LD_RESULT && mi.args == 1) {
                addr_t addr = internal_regs_[static_cast<size_t>(InternalReg::MAR)];
                size_t idx = (addr / 4) % mem_.size();
                shared_bus_ = mem_[idx];
                internal_regs_[static_cast<size_t>(InternalReg::LD_RESULT)] = shared_bus_;
            } else if (mi.dest == InternalReg::MAR && mi.args == 2) {
                addr_t addr = internal_regs_[static_cast<size_t>(InternalReg::MAR)];
                word_t val = internal_regs_[static_cast<size_t>(InternalReg::MDR)];
                size_t idx = (addr / 4) % mem_.size();
                mem_[idx] = val;
            } else if (mi.dest == InternalReg::PC) {
                internal_regs_[static_cast<size_t>(InternalReg::PC)] = shared_bus_;
                pc_ = shared_bus_;
                signals_.pc_in = true;
            } else {
                internal_regs_[static_cast<size_t>(mi.dest)] = shared_bus_;
            }
            break;
        }

        case MicroOp::MMOVI: {
            shared_bus_ = static_cast<word_t>(mi.immediate);
            internal_regs_[static_cast<size_t>(mi.dest)] = shared_bus_;
            break;
        }

        case MicroOp::MADD: {
            word_t cur = internal_regs_[static_cast<size_t>(mi.dest)];
            shared_bus_ = cur + mi.immediate;
            internal_regs_[static_cast<size_t>(mi.dest)] = shared_bus_;
            if (mi.dest == InternalReg::PC) {
                pc_ = shared_bus_;
                signals_.pc_in = true;
            }
            break;
        }

        case MicroOp::MBEQ: {
            word_t val = internal_regs_[static_cast<size_t>(mi.dest)];
            if (val == static_cast<word_t>(mi.immediate)) {
                next_upc = mi.branch_target;
            }
            shared_bus_ = val;
            break;
        }

        case MicroOp::MB: {
            next_upc = mi.branch_target;
            shared_bus_ = mi.branch_target;
            break;
        }
    }

    upc_ = next_upc;
    bool retired = (upc_ == 0);
    if (retired) {
        instructions_retired_++;
    }
    return retired;
}

uint32_t VerticalControlUnit::stepInstruction() {
    uint32_t cycles = 0;
    bool completed = false;
    while (!completed && cycles < 100) {
        completed = stepMicroCycle();
        cycles++;
    }
    return cycles;
}

std::string VerticalControlUnit::dumpInternalRegisters() const {
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|          VERTICAL MICROPROGRAMMED DATA-PATH REGISTERS (Table 9.7)           |\n";
    oss << "+-----------------------------------------------------------------------------+\n";

    std::ostringstream l1;
    l1 << "PC  : " << toHex8(internal_regs_[0])
       << " | IR : " << toHex8(internal_regs_[1])
       << " | I_BIT: " << internal_regs_[2]
       << " | uPC: " << std::dec << std::setw(3) << upc_;
    oss << "|  " << std::left << std::setw(73) << l1.str() << " |\n";

    std::ostringstream l2;
    l2 << "RD  : r" << std::dec << std::setw(2) << std::left << internal_regs_[3]
       << " | RS1: r" << std::setw(2) << internal_regs_[4]
       << " | RS2: r" << std::setw(2) << internal_regs_[5]
       << " | IMMX: " << toHex8(internal_regs_[6]);
    oss << "|  " << std::left << std::setw(73) << l2.str() << " |\n";

    std::ostringstream l3;
    l3 << "A   : " << toHex8(internal_regs_[11])
       << " | B  : " << toHex8(internal_regs_[12])
       << " | ALU_OUT: " << toHex8(internal_regs_[15]);
    oss << "|  " << std::left << std::setw(73) << l3.str() << " |\n";

    std::ostringstream l4;
    l4 << "FLAGS: [E=" << internal_regs_[13] << " GT=" << internal_regs_[14] << "]"
       << " | Shared Bus: " << toHex8(shared_bus_)
       << " | Control Word: 45 bits";
    oss << "|  " << std::left << std::setw(73) << l4.str() << " |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

std::string VerticalControlUnit::dumpMicroinstruction(upc_t addr) const {
    if (addr >= micro_rom_.size()) return "INVALID_UPC";
    const MicroInstruction& mi = micro_rom_[addr];
    std::stringstream ss;
    ss << "uPC " << std::setw(3) << addr << ": ";
    switch (mi.type) {
        case MicroOp::MLOAD_IR: ss << "mloadIR"; break;
        case MicroOp::MDECODE:  ss << "mdecode"; break;
        case MicroOp::MSWITCH:  ss << "mswitch"; break;
        case MicroOp::MMOV:     ss << "mmov (args=" << mi.args << ")"; break;
        case MicroOp::MMOVI:    ss << "mmovi " << mi.immediate; break;
        case MicroOp::MADD:     ss << "madd " << mi.immediate; break;
        case MicroOp::MBEQ:     ss << "mbeq -> target " << mi.branch_target; break;
        case MicroOp::MB:       ss << "mb -> target " << mi.branch_target; break;
    }
    return ss.str();
}

std::string VerticalControlUnit::dumpControlState() const {
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|                 VERTICAL MICROPROGRAMMED CONTROL UNIT STATE                 |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    std::ostringstream l1;
    l1 << "uPC: " << std::dec << std::setw(3) << upc_
       << " | PC: " << toHex8(pc_)
       << " | Bus: " << toHex8(shared_bus_)
       << " | Total uCycles: " << std::dec << total_micro_cycles_;
    oss << "|  " << std::left << std::setw(73) << l1.str() << " |\n";

    std::ostringstream l2;
    l2 << "Flags: [E=" << flags_.E << " GT=" << flags_.GT << " C=" << flags_.C << " V=" << flags_.V << "]"
       << " | Instructions Retired: " << std::dec << instructions_retired_;
    oss << "|  " << std::left << std::setw(73) << l2.str() << " |\n";

    std::ostringstream l3;
    l3 << "Micro-CPI: " << std::fixed << std::setprecision(2) << getMicroCPI()
       << " | Control Word Width: 45 bits (Encoded, Gate Decode Required)";
    oss << "|  " << std::left << std::setw(73) << l3.str() << " |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

} // namespace simplerisc
