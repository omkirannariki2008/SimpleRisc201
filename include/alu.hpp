#ifndef SIMPLERISC_ALU_HPP
#define SIMPLERISC_ALU_HPP

#include "types.hpp"
#include <string>

namespace simplerisc {

enum class ALUOp : uint8_t {
    ADD,    // a + b (Hierarchical GPK CLA)
    SUB,    // a - b (Hierarchical GPK CLA with Cin=1)
    MUL,    // a * b (Radix-4 Modified Booth Multiplier)
    DIV,    // a / b (Newton-Raphson Double-Precision Divider)
    MOD,    // a % b (Hardware Remainder: R = A - Q * B)
    CMP,    // compare a and b -> sets flags.E, flags.GT, flags.C, flags.V, flags.N, flags.Z
    AND,    // a & b
    OR,     // a | b
    NOT,    // ~b (or ~a)
    MOV,    // b
    LSL,    // a << (b & 0x1F)
    LSR,    // a >> (b & 0x1F) (logical)
    ASR,    // a >> (b & 0x1F) (arithmetic)
    PASS_A, // a
    PASS_B, // b
    // Extended Conversion & Fused Arithmetic Operations
    I2D,    // Convert 32-bit Integer to Double bit pattern
    D2I,    // Convert Double bit pattern to 32-bit Integer
    FDIV,   // Fused Double Newton-Raphson Division
    // Floating-Point Single-Precision Operations (IEEE-754)
    FADD,   // FP a + b
    FSUB,   // FP a - b
    FMUL,   // FP a * b (Figure 8.18 Flowchart)
    FCMP    // FP compare a and b
};

struct ALUResult {
    word_t result{0};
    Flags flags;      // E, GT, C, V, N, Z, DIV0, FP_OVF, FP_UNF, FP_INV

    // Convenience flag helpers
    bool zero() const { return flags.Z; }
    bool negative() const { return flags.N; }
    bool carry() const { return flags.C; }
    bool overflow() const { return flags.V; }
};

class ALU {
public:
    // Core ALU Execution dispatcher
    static ALUResult execute(ALUOp op, word_t a, word_t b, const Flags& current_flags = Flags{});
    static ALUOp fromOpcode(Opcode op);
    static std::string opName(ALUOp op);

    // ========================================================================
    // Hardware-Accurate Arithmetic Subsystems
    // ========================================================================

    // 1. 32-bit Carry-Lookahead Adder (CLA) with Generate (G), Propagate (P), and Kill (K) logic
    static word_t claAdd32(word_t a, word_t b, bool cin, bool& cout, bool& vout);

    // 2. 32-bit Radix-4 Modified Booth's Multiplier (16 iterations, 3-bit window recoding)
    static word_t boothMultiply32(sword_t a, sword_t b, bool& vout);

    // 3. 32-bit Double-Precision Newton-Raphson Integer Divider & Modulo
    static sword_t newtonRaphsonDivide32(sword_t a, sword_t b, sword_t& rem_out, bool& div0_out);

    // 4. Bit-Level IEEE-754 Double <-> 32-bit Integer Conversions (CLZ Priority Encoder)
    static uint32_t countLeadingZeros32(uint32_t val);
    static uint64_t intToDoubleBits(sword_t a);
    static sword_t doubleBitsToInt(uint64_t double_bits);

    // 5. IEEE-754 Single-Precision Floating-Point Multiplier (Figure 8.18 Architecture)
    static word_t fpMultiply32(word_t a, word_t b, bool& ovf, bool& unf);

    // 6. IEEE-754 Single-Precision Floating-Point Divider (Newton-Raphson Reciprocal)
    static word_t fpDivide32(word_t a, word_t b, bool& ovf, bool& unf, bool& div0);

    // 7. IEEE-754 Single-Precision Floating-Point Addition / Subtraction
    static word_t fpAdd32(word_t a, word_t b, bool& ovf, bool& unf);
    static word_t fpSub32(word_t a, word_t b, bool& ovf, bool& unf);
};

} // namespace simplerisc

#endif // SIMPLERISC_ALU_HPP
