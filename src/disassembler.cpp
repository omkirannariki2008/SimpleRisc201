#include "disassembler.hpp"
#include <sstream>
#include <iomanip>
#include <bitset>
#include <algorithm>

namespace simplerisc {

std::string Disassembler::regName(reg_t r) {
    if (r == 14) return "sp";
    if (r == 15) return "ra";
    return "r" + std::to_string(r);
}

std::string Disassembler::opcodeName(Opcode op) {
    switch (op) {
        case OP_ADD:  return "add";
        case OP_SUB:  return "sub";
        case OP_MUL:  return "mul";
        case OP_DIV:  return "div";
        case OP_MOD:  return "mod";
        case OP_CMP:  return "cmp";
        case OP_AND:  return "and";
        case OP_OR:   return "or";
        case OP_NOT:  return "not";
        case OP_MOV:  return "mov";
        case OP_LSL:  return "lsl";
        case OP_LSR:  return "lsr";
        case OP_ASR:  return "asr";
        case OP_NOP:  return "nop";
        case OP_LD:   return "ld";
        case OP_ST:   return "st";
        case OP_BEQ:  return "beq";
        case OP_BGT:  return "bgt";
        case OP_B:    return "b";
        case OP_CALL: return "call";
        case OP_RET:  return "ret";
        case OP_I2D:  return "i2d";
        case OP_D2I:  return "d2i";
        case OP_FDIV: return "fdiv";
        default:      return ".word";
    }
}

DisassembledInstruction Disassembler::disassemble(word_t word, addr_t pc) {
    DisassembledInstruction d;
    d.address = pc;
    d.machine_word = word;
    d.valid = true;

    Instruction decoded = Instruction::decode(word, pc);
    d.mnemonic = opcodeName(decoded.opcode);

    std::ostringstream ops;

    if (decoded.opcode == OP_NOP) {
        d.operands = "";
        d.full_assembly = "nop";
        return d;
    }

    if (decoded.opcode == OP_RET) {
        d.operands = "";
        d.full_assembly = "ret";
        return d;
    }

    if (decoded.format == InstFormat::UNARY_REG || decoded.opcode == OP_I2D || decoded.opcode == OP_D2I) {
        std::string rd_str = regName(decoded.rd);
        std::string rs1_str = regName(decoded.rs1);
        d.operands = rd_str + ", " + rs1_str;
        d.full_assembly = d.mnemonic + " " + d.operands;
        return d;
    }

    if (decoded.format == InstFormat::BRANCH) {
        std::ostringstream oss;
        oss << ".L_" << std::hex << std::setw(8) << std::setfill('0') << decoded.branch_target;
        d.operands = oss.str();
        d.full_assembly = d.mnemonic + " " + d.operands;
        return d;
    }

    if (decoded.opcode == OP_LD || decoded.opcode == OP_ST) {
        std::string rd_str = regName(decoded.rd);
        std::string rs1_str = regName(decoded.rs1);
        std::ostringstream oss;
        oss << rd_str << ", " << static_cast<int32_t>(decoded.immx) << "[" << rs1_str << "]";
        d.operands = oss.str();
        d.full_assembly = d.mnemonic + " " + d.operands;
        return d;
    }

    if (decoded.opcode == OP_NOT || decoded.opcode == OP_MOV) {
        std::string rd_str = regName(decoded.rd);
        std::string src_str;
        if (decoded.I) {
            src_str = std::to_string(static_cast<int32_t>(decoded.immx));
        } else {
            src_str = regName(decoded.rs2);
        }
        d.operands = rd_str + ", " + src_str;
        d.full_assembly = d.mnemonic + " " + d.operands;
        return d;
    }

    if (decoded.opcode == OP_CMP) {
        std::string rs1_str = regName(decoded.rs1);
        std::string src2_str;
        if (decoded.I) {
            src2_str = std::to_string(static_cast<int32_t>(decoded.immx));
        } else {
            src2_str = regName(decoded.rs2);
        }
        d.operands = rs1_str + ", " + src2_str;
        d.full_assembly = d.mnemonic + " " + d.operands;
        return d;
    }

    // Standard 3-operand ALU instructions: add, sub, mul, div, mod, and, or, lsl, lsr, asr
    std::string rd_str = regName(decoded.rd);
    std::string rs1_str = regName(decoded.rs1);
    std::string src2_str;
    if (decoded.I) {
        src2_str = std::to_string(static_cast<int32_t>(decoded.immx));
    } else {
        src2_str = regName(decoded.rs2);
    }

    d.operands = rd_str + ", " + rs1_str + ", " + src2_str;
    d.full_assembly = d.mnemonic + " " + d.operands;
    return d;
}

std::vector<DisassembledInstruction> Disassembler::disassembleProgram(
    const std::vector<word_t>& words, addr_t base_address) {
    std::vector<DisassembledInstruction> list;
    list.reserve(words.size());
    addr_t cur_pc = base_address;
    for (word_t w : words) {
        list.push_back(disassemble(w, cur_pc));
        cur_pc += 4;
    }
    return list;
}

DisassembledInstruction Disassembler::disassembleHex(const std::string& hex_str, addr_t pc) {
    std::string clean = hex_str;
    if (clean.size() >= 2 && clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) {
        clean = clean.substr(2);
    }
    word_t val = 0;
    try {
        val = static_cast<word_t>(std::stoul(clean, nullptr, 16));
    } catch (...) {
        DisassembledInstruction invalid;
        invalid.valid = false;
        invalid.full_assembly = "; invalid hex: " + hex_str;
        return invalid;
    }
    return disassemble(val, pc);
}

DisassembledInstruction Disassembler::disassembleBinary(const std::string& bin_str, addr_t pc) {
    std::string clean;
    for (char c : bin_str) {
        if (c == '0' || c == '1') clean.push_back(c);
    }
    if (clean.size() != 32) {
        DisassembledInstruction invalid;
        invalid.valid = false;
        invalid.full_assembly = "; invalid binary string length (expected 32 bits)";
        return invalid;
    }
    word_t val = 0;
    for (char c : clean) {
        val = (val << 1) | (c - '0');
    }
    return disassemble(val, pc);
}

std::string Disassembler::formatListing(const std::vector<DisassembledInstruction>& insts) {
    std::ostringstream oss;
    oss << "+--------------+--------------+-----------------------------------------------+\n";
    oss << "| Address      | Machine Hex  | Assembly Instruction                          |\n";
    oss << "+--------------+--------------+-----------------------------------------------+\n";
    for (const auto& item : insts) {
        oss << "|  0x" << std::right << std::hex << std::setw(8) << std::setfill('0') << item.address << "  "
            << "|  0x" << std::right << std::hex << std::setw(8) << std::setfill('0') << item.machine_word << "  "
            << "| " << std::left << std::setw(45) << std::setfill(' ') << item.full_assembly
            << " |\n";
    }
    oss << "+--------------+--------------+-----------------------------------------------+\n";
    return oss.str();
}

} // namespace simplerisc
