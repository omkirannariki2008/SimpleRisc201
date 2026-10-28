#ifndef SIMPLERISC_TYPES_HPP
#define SIMPLERISC_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace simplerisc {

// ============================================================================
// 1. Fundamental Datatypes (32-bit Architecture)
// ============================================================================
using word_t   = uint32_t; // 32-bit unsigned word (instructions and data)
using sword_t  = int32_t;  // 32-bit signed word (signed arithmetic)
using byte_t   = uint8_t;   // 8-bit byte
using addr_t   = uint32_t; // 32-bit byte-addressed memory address
using upc_t    = uint16_t; // Micro-PC (word-addressed micro-ROM index)
using reg_t    = uint8_t;  // 4-bit register index (0 to 15)

// Memory Constants
constexpr size_t MEMORY_SIZE = 65536;      // 64 KB memory space for Eval 1
constexpr addr_t TEXT_BASE   = 0x00000000; // Text segment base address

// 16 Architectural Registers (r0 - r15)
// r0 - r13: General Purpose Registers (all readable and writable)
// r14: Stack Pointer (sp)
// r15: Return Address Register (ra)
constexpr reg_t R0  = 0;
constexpr reg_t R1  = 1;
constexpr reg_t R2  = 2;
constexpr reg_t R3  = 3;
constexpr reg_t R4  = 4;
constexpr reg_t R5  = 5;
constexpr reg_t R6  = 6;
constexpr reg_t R7  = 7;
constexpr reg_t R8  = 8;
constexpr reg_t R9  = 9;
constexpr reg_t R10 = 10;
constexpr reg_t R11 = 11;
constexpr reg_t R12 = 12;
constexpr reg_t R13 = 13;
constexpr reg_t R14 = 14; // SP
constexpr reg_t R15 = 15; // RA
constexpr reg_t SP  = R14;
constexpr reg_t RA  = R15;
constexpr size_t NUM_REGISTERS = 16;

// ============================================================================
// 2. Condition Flags (SimpleRISC: Flags E and GT set exclusively by CMP, plus microarchitecture telemetry)
// ============================================================================
struct Flags {
    bool E{false};       // Equality flag (A == B)
    bool GT{false};      // Greater Than flag (signed A > signed B)
    // Additional microarchitecture flags for arithmetic telemetry
    bool C{false};       // Carry flag (unsigned overflow / borrow)
    bool V{false};       // Overflow flag (signed 2's complement overflow)
    bool N{false};       // Negative / Sign flag (MSB == 1)
    bool Z{false};       // Zero flag (Result == 0)
    bool DIV0{false};    // Divide-by-Zero exception flag
    bool FP_OVF{false};  // Floating-point overflow flag
    bool FP_UNF{false};  // Floating-point underflow flag
    bool FP_INV{false};  // Floating-point invalid operation flag (NaN)

    // Helper queries
    bool zero() const { return Z; }
    bool negative() const { return N; }
    bool carry() const { return C; }
    bool overflow() const { return V; }
};

// ============================================================================
// 3. 24 SimpleRISC Instructions & Opcodes (ISA Specification + Extended FP)
// 5-bit opcodes (00000 to 10111)
// ============================================================================
enum Opcode : uint8_t {
    OP_ADD  = 0b00000, // 0:  add rd, rs1, (rs2/imm)
    OP_SUB  = 0b00001, // 1:  sub rd, rs1, (rs2/imm)
    OP_MUL  = 0b00010, // 2:  mul rd, rs1, (rs2/imm)
    OP_DIV  = 0b00011, // 3:  div rd, rs1, (rs2/imm)
    OP_MOD  = 0b00100, // 4:  mod rd, rs1, (rs2/imm)
    OP_CMP  = 0b00101, // 5:  cmp rs1, (rs2/imm) -> sets flags.E, flags.GT
    OP_AND  = 0b00110, // 6:  and rd, rs1, (rs2/imm)
    OP_OR   = 0b00111, // 7:  or rd, rs1, (rs2/imm)
    OP_NOT  = 0b01000, // 8:  not rd, (rs2/imm)
    OP_MOV  = 0b01001, // 9:  mov rd, (rs2/imm)
    OP_LSL  = 0b01010, // 10: lsl rd, rs1, (rs2/imm)
    OP_LSR  = 0b01011, // 11: lsr rd, rs1, (rs2/imm)
    OP_ASR  = 0b01100, // 12: asr rd, rs1, (rs2/imm)
    OP_NOP  = 0b01101, // 13: nop
    OP_LD   = 0b01110, // 14: ld rd, imm[rs1]
    OP_ST   = 0b01111, // 15: st rd, imm[rs1]
    OP_BEQ  = 0b10000, // 16: beq offset
    OP_BGT  = 0b10001, // 17: bgt offset
    OP_B    = 0b10010, // 18: b offset
    OP_CALL = 0b10011, // 19: call offset
    OP_RET  = 0b10100, // 20: ret
    OP_I2D  = 0b10101, // 21: i2d rd, rs1 (Convert Integer to Double bits)
    OP_D2I  = 0b10110, // 22: d2i rd, rs1 (Convert Double bits to Integer)
    OP_FDIV = 0b10111  // 23: fdiv rd, rs1, (rs2/imm) (Fused Newton-Raphson Division)
};
constexpr size_t NUM_OPCODES = 24;

// Instruction Formats (ISA Specification + Extended UNARY_REG)
enum class InstFormat {
    BRANCH,    // [op: 31-27 | offset: 26-0] (27-bit signed offset)
    REGISTER,  // [op: 31-27 | I=0: 26 | rd: 25-22 | rs1: 21-18 | rs2: 17-14 | unused: 13-0]
    IMMEDIATE, // [op: 31-27 | I=1: 26 | rd: 25-22 | rs1: 21-18 | mod: 17-16 | imm16: 15-0]
    UNARY_REG  // [op: 31-27 | I=0: 26 | rd: 25-22 | rs1: 21-18 | unused: 17-0] (i2d, d2i)
};

// Immediate Modifiers
enum class ImmModifier : uint8_t {
    DEFAULT = 0b00, // Sign-extended 16-bit
    UPPER   = 0b01, // Upper half: imm16 << 16
    HIGH    = 0b10  // Zero-extended 16-bit
};

// ============================================================================
// 4. 19 Internal Registers for Microprogramming (Table 9.7)
// ============================================================================
enum class InternalReg : uint8_t {
    PC           = 0,  // Program counter
    IR           = 1,  // Instruction register
    I_BIT        = 2,  // Immediate bit
    RD           = 3,  // Destination reg ID
    RS1          = 4,  // Source 1 reg ID
    RS2          = 5,  // Source 2 reg ID
    IMMX         = 6,  // Processed immediate
    BRANCH_TGT   = 7,  // Branch target address
    REG_SRC      = 8,  // Reg file port selector
    REG_DATA     = 9,  // Reg file write data
    REG_VAL      = 10, // Reg file read data
    A            = 11, // ALU operand A
    B            = 12, // ALU operand B
    FLAGS_E      = 13, // Equality flag latch
    FLAGS_GT     = 14, // Greater-than flag latch
    ALU_RESULT   = 15, // ALU output latch
    MAR          = 16, // Memory address register
    MDR          = 17, // Memory data register
    LD_RESULT    = 18  // Memory load result latch
};
constexpr size_t NUM_INTERNAL_REGISTERS = 19;

// ============================================================================
// 5. 8 Microinstructions (Table 9.8)
// ============================================================================
enum class MicroOp : uint8_t {
    MLOAD_IR = 0, // ir <- [pc]
    MDECODE  = 1, // populate decode registers
    MSWITCH  = 2, // jump to micro-PC corresponding to opcode
    MMOV     = 3, // reg1 <- reg2
    MMOVI    = 4, // reg1 <- imm
    MADD     = 5, // reg1 <- reg1 + imm
    MBEQ     = 6, // if (reg1 == imm) upc <- target
    MB       = 7  // upc <- target
};

// 45-bit Vertical Microinstruction Encoding (Figure 9.24)
// [type: 3 | src: 5 | dest: 5 | immediate: 12 | branchTarget: 10 | args: 10]
struct MicroInstruction {
    MicroOp type{MicroOp::MMOV};
    InternalReg src{InternalReg::PC};
    InternalReg dest{InternalReg::PC};
    int16_t immediate{0};
    uint16_t branch_target{0};
    uint16_t args{0};
    uint64_t encode() const {
        uint64_t w = 0;
        w |= (static_cast<uint64_t>(type) & 0x7) << 42;
        w |= (static_cast<uint64_t>(src) & 0x1F) << 37;
        w |= (static_cast<uint64_t>(dest) & 0x1F) << 32;
        w |= (static_cast<uint64_t>(immediate) & 0xFFF) << 20;
        w |= (static_cast<uint64_t>(branch_target) & 0x3FF) << 10;
        w |= (static_cast<uint64_t>(args) & 0x3FF);
        return w;
    }

    static MicroInstruction decode(uint64_t w) {
        MicroInstruction mi;
        mi.type = static_cast<MicroOp>((w >> 42) & 0x7);
        mi.src = static_cast<InternalReg>((w >> 37) & 0x1F);
        mi.dest = static_cast<InternalReg>((w >> 32) & 0x1F);
        uint16_t raw_imm = (w >> 20) & 0xFFF;
        mi.immediate = (raw_imm & 0x800) ? static_cast<int16_t>(raw_imm | 0xF000) : static_cast<int16_t>(raw_imm);
        mi.branch_target = (w >> 10) & 0x3FF;
        mi.args = w & 0x3FF;
        return mi;
    }
};

// ============================================================================
// 6. Decoded 32-bit Instruction Structure & Silicon Decoder
// ============================================================================
struct Instruction {
    word_t raw{0};
    Opcode opcode{OP_NOP};
    bool I{false};
    reg_t rd{0};
    reg_t rs1{0};
    reg_t rs2{0};
    word_t immx{0};
    int32_t branch_offset{0};
    addr_t branch_target{0};
    InstFormat format{InstFormat::REGISTER};
    bool valid{false};

    static Instruction decode(word_t word, addr_t current_pc = 0) {
        Instruction inst;
        inst.raw = word;
        uint8_t op_val = (word >> 27) & 0x1F;
        inst.opcode = static_cast<Opcode>(op_val);

        if (op_val >= OP_BEQ && op_val <= OP_CALL) {
            inst.format = InstFormat::BRANCH;
            uint32_t raw_offset = word & 0x07FFFFFF;
            if (raw_offset & 0x04000000) {
                inst.branch_offset = static_cast<int32_t>(raw_offset | 0xF8000000);
            } else {
                inst.branch_offset = static_cast<int32_t>(raw_offset);
            }
            inst.branch_target = current_pc + static_cast<addr_t>(inst.branch_offset * 4);
            inst.valid = true;
            return inst;
        }

        if (inst.opcode == OP_RET || inst.opcode == OP_NOP) {
            inst.format = InstFormat::REGISTER;
            inst.valid = true;
            return inst;
        }

        // Unary Register conversions: i2d (21), d2i (22)
        if (inst.opcode == OP_I2D || inst.opcode == OP_D2I) {
            inst.format = InstFormat::UNARY_REG;
            inst.I = false;
            inst.rd = (word >> 22) & 0xF;
            inst.rs1 = (word >> 18) & 0xF;
            inst.rs2 = 0;
            inst.immx = 0;
            inst.valid = true;
            return inst;
        }

        // Standard Register or Immediate formats (including OP_FDIV)
        inst.I   = ((word >> 26) & 0x1) != 0;
        inst.rd  = (word >> 22) & 0xF;
        inst.rs1 = (word >> 18) & 0xF;

        if (!inst.I) {
            inst.format = InstFormat::REGISTER;
            inst.rs2 = (word >> 14) & 0xF;
        } else {
            inst.format = InstFormat::IMMEDIATE;
            uint8_t mod = (word >> 16) & 0x3;
            uint16_t imm16 = word & 0xFFFF;
            if (mod == 0b00) {
                inst.immx = (imm16 & 0x8000) ? (0xFFFF0000 | imm16) : imm16;
            } else if (mod == 0b01) {
                inst.immx = static_cast<word_t>(imm16) << 16;
            } else {
                inst.immx = imm16;
            }
        }

        inst.valid = (op_val <= OP_FDIV);
        return inst;
    }

    bool writesRd() const {
        if (!valid) return false;
        if (opcode == OP_CMP) return false;
        if (opcode == OP_ST) return false;
        if (opcode == OP_NOP || opcode == OP_RET || opcode == OP_BEQ || opcode == OP_BGT || opcode == OP_B || opcode == OP_CALL) return false;
        return true;
    }

    bool readsRs1() const {
        if (!valid) return false;
        if (opcode == OP_NOP || opcode == OP_RET || opcode == OP_BEQ || opcode == OP_BGT || opcode == OP_B || opcode == OP_CALL) return false;
        if (opcode == OP_MOV || opcode == OP_NOT) return false;
        return true;
    }

    bool readsRs2() const {
        if (!valid) return false;
        if (I) return false;
        if (format == InstFormat::BRANCH || format == InstFormat::UNARY_REG) return false;
        if (opcode == OP_NOP || opcode == OP_RET || opcode == OP_LD) return false;
        if (opcode == OP_ST) return false;
        return true;
    }

    bool readsStoreRd() const {
        return valid && (opcode == OP_ST);
    }

    bool isLoad() const {
        return valid && (opcode == OP_LD);
    }

    bool isStore() const {
        return valid && (opcode == OP_ST);
    }

    bool isBranch() const {
        return valid && (opcode == OP_BEQ || opcode == OP_BGT || opcode == OP_B || opcode == OP_CALL || opcode == OP_RET);
    }

    bool readsRA() const {
        return valid && (opcode == OP_RET);
    }
};

inline std::string toHex8(word_t val) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", val);
    return std::string(buf);
}

inline std::string regName(reg_t r) {
    if (r == 14) return "r14 (sp)";
    if (r == 15) return "r15 (ra)";
    return "r" + std::to_string(r);
}

// Exception Hierarchy
class SimpleRISCException : public std::runtime_error {
public:
    explicit SimpleRISCException(const std::string& msg) : std::runtime_error(msg) {}
};

class DivisionByZeroException : public SimpleRISCException {
public:
    explicit DivisionByZeroException(addr_t pc = 0)
        : SimpleRISCException("Division by Zero at PC " + toHex8(pc)) {}
};

class IllegalInstructionException : public SimpleRISCException {
public:
    IllegalInstructionException(word_t word, addr_t pc)
        : SimpleRISCException("Illegal Instruction " + toHex8(word) + " at PC " + toHex8(pc)) {}
};

} // namespace simplerisc

namespace risc201 = simplerisc;

#endif // SIMPLERISC_TYPES_HPP
