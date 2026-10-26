#include "alu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace simplerisc {

// ============================================================================
// 1. 32-bit Carry-Lookahead Adder (CLA) with Generate, Propagate, and Kill
// (GPK)
// ============================================================================
word_t ALU::claAdd32(word_t a, word_t b, bool cin, bool &cout, bool &vout) {
  // Bit-level GPK computation:
  // G_i = A_i & B_i        (Generate: forces carry-out C_{i+1} = 1 immediately)
  // P_i = A_i ^ B_i        (Propagate: passes carry-in C_i to carry-out C_{i+1})
  // K_i = (~A_i) & (~B_i)  (Kill: both bits 0; immediately kills carry, forcing C_{i+1} = 0)
  uint32_t G = a & b;
  uint32_t P = a ^ b;
  uint32_t K = (~a) & (~b); // Explicit Kill signal vector

  // 4-bit Hierarchical Block Lookahead
  // Group Generate (G_blk), Group Propagate (P_blk), and Group Kill (K_blk) for 8 blocks (4 bits each)
  uint8_t G_blk[8];
  uint8_t P_blk[8];
  uint8_t K_blk[8];

  for (int blk = 0; blk < 8; ++blk) {
    int base = blk * 4;
    bool g0 = (G >> base) & 1;
    bool g1 = (G >> (base + 1)) & 1;
    bool g2 = (G >> (base + 2)) & 1;
    bool g3 = (G >> (base + 3)) & 1;

    bool p0 = (P >> base) & 1;
    bool p1 = (P >> (base + 1)) & 1;
    bool p2 = (P >> (base + 2)) & 1;
    bool p3 = (P >> (base + 3)) & 1;

    G_blk[blk] = g3 | (p3 & g2) | (p3 & p2 & g1) | (p3 & p2 & p1 & g0);
    P_blk[blk] = p3 & p2 & p1 & p0;
    // Explicit Block Kill: when block neither generates nor propagates, carry is killed immediately
    K_blk[blk] = !(G_blk[blk] | P_blk[blk]);
  }

  // High-level Block Carries: C_blk[0..8] where C_blk[k] is carry into bit 4*k
  // Explicit GPK Carry Resolution:
  // - If K_blk is 1: send 0 immediately (carry killed, zero waiting for C_in)
  // - If G_blk is 1: send 1 immediately (carry generated, zero waiting for C_in)
  // - If P_blk is 1: propagate C_blk[blk] through
  bool C_blk[9];
  C_blk[0] = cin;
  for (int blk = 0; blk < 8; ++blk) {
    if (K_blk[blk]) {
      C_blk[blk + 1] = 0; // Carry killed at block barrier
    } else if (G_blk[blk]) {
      C_blk[blk + 1] = 1; // Carry generated
    } else {
      C_blk[blk + 1] = C_blk[blk]; // Carry propagated
    }
  }

  // Internal bit-level carries computation using explicit bit-level Kill (K_i)
  uint32_t C = 0; // Bit carries: C[i] is carry into bit i
  for (int blk = 0; blk < 8; ++blk) {
    int base = blk * 4;
    bool c0 = C_blk[blk];

    bool g0 = (G >> base) & 1;
    bool g1 = (G >> (base + 1)) & 1;
    bool g2 = (G >> (base + 2)) & 1;

    bool k0 = (K >> base) & 1;
    bool k1 = (K >> (base + 1)) & 1;
    bool k2 = (K >> (base + 2)) & 1;

    // Bit 0 carry-out into bit 1: if k0 kill -> 0; if g0 generate -> 1; else propagate c0
    bool c1 = k0 ? 0 : (g0 ? 1 : c0);
    // Bit 1 carry-out into bit 2: if k1 kill -> 0; if g1 generate -> 1; else propagate c1
    bool c2 = k1 ? 0 : (g1 ? 1 : c1);
    // Bit 2 carry-out into bit 3: if k2 kill -> 0; if g2 generate -> 1; else propagate c2
    bool c3 = k2 ? 0 : (g2 ? 1 : c2);

    if (c0)
      C |= (1U << base);
    if (c1)
      C |= (1U << (base + 1));
    if (c2)
      C |= (1U << (base + 2));
    if (c3)
      C |= (1U << (base + 3));
  }

  // Final sum generation: Sum_i = P_i ^ C_i
  word_t sum = P ^ C;
  cout = C_blk[8];

  // Signed overflow: V = C_in_MSB ^ C_out_MSB
  bool c_in_msb = (C >> 31) & 1;
  vout = c_in_msb ^ cout;

  return sum;
}

// ============================================================================
// 2. 32-bit Radix-4 Modified Booth's Multiplier (16 iterations)
// ============================================================================
word_t ALU::boothMultiply32(sword_t a, sword_t b, bool &vout) {
  // Radix-4 Booth recoding reduces 32 partial products to 16 iterations.
  // 3-bit sliding window: (b_{2i+1}, b_{2i}, b_{2i-1}) with b_{-1} = 0
  uint64_t b_window =
      (static_cast<uint64_t>(static_cast<uint32_t>(b)) << 1); // bit 0 is b_{-1}
  int64_t accumulator = 0;

  for (int i = 0; i < 16; ++i) {
    uint8_t code = (b_window >> (2 * i)) & 0x7;
    int64_t term = 0;

    switch (code) {
    case 0b000: // 0
    case 0b111: // 0
      term = 0;
      break;
    case 0b001: // +1 * A
    case 0b010: // +1 * A
      term = static_cast<int64_t>(a);
      break;
    case 0b011: // +2 * A
      term = static_cast<int64_t>(a) << 1;
      break;
    case 0b100: // -2 * A
      term = -(static_cast<int64_t>(a) << 1);
      break;
    case 0b101: // -1 * A
    case 0b110: // -1 * A
      term = -static_cast<int64_t>(a);
      break;
    }

    accumulator += (term << (2 * i));
  }

  word_t result = static_cast<word_t>(accumulator & 0xFFFFFFFFULL);

  // Overflow check: accumulator exceeds 32-bit signed range
  int64_t expected_ext = static_cast<int64_t>(static_cast<sword_t>(result));
  vout = (accumulator != expected_ext);

  return result;
}

// ============================================================================
// 3. 32-bit Double-Precision Newton-Raphson Integer Divider & Modulo
// ============================================================================
sword_t ALU::newtonRaphsonDivide32(sword_t a, sword_t b, sword_t &rem_out,
                                   bool &div0_out) {
  if (b == 0) {
    div0_out = true;
    rem_out = 0;
    return 0;
  }
  div0_out = false;

  bool negative = (a < 0) ^ (b < 0);
  uint64_t abs_a = (a < 0) ? static_cast<uint64_t>(-static_cast<int64_t>(a))
                           : static_cast<uint64_t>(a);
  uint64_t abs_b = (b < 0) ? static_cast<uint64_t>(-static_cast<int64_t>(b))
                           : static_cast<uint64_t>(b);

  if (abs_a < abs_b) {
    rem_out = a;
    return 0;
  }

  // High-precision reciprocal of divisor |b| using Newton-Raphson iteration
  // x_{k+1} = x_k * (2 - D * x_k)
  double D = static_cast<double>(abs_b);
  int exp;
  double mant = std::frexp(D, &exp);
  // Initial optimal linear seed
  double X = (48.0 / 17.0 - (32.0 / 17.0) * mant) * std::ldexp(1.0, -exp);

  // 5 iterations in double precision guarantee 53 bits of precision
  // (quadratically converging)
  for (int k = 0; k < 5; ++k) {
    X = X * (2.0 - D * X);
  }

  double q_approx = static_cast<double>(abs_a) * X;
  uint64_t q_int = static_cast<uint64_t>(q_approx + 1e-11);

  // Exact single-cycle hardware boundary adjustment
  while ((q_int + 1) * abs_b <= abs_a) {
    q_int++;
  }
  while (q_int * abs_b > abs_a) {
    q_int--;
  }

  sword_t q_signed =
      negative ? -static_cast<sword_t>(q_int) : static_cast<sword_t>(q_int);
  // Modulo formula: Remainder R = A - (Q * B)
  rem_out = static_cast<sword_t>(static_cast<int64_t>(a) -
                                 static_cast<int64_t>(q_signed) *
                                     static_cast<int64_t>(b));

  return q_signed;
}

// ============================================================================
// 4. Bit-Level IEEE-754 Double <-> 32-bit Integer Conversions
// ============================================================================
uint32_t ALU::countLeadingZeros32(uint32_t val) {
  if (val == 0)
    return 32;
  uint32_t n = 0;
  if ((val & 0xFFFF0000U) == 0) {
    n += 16;
    val <<= 16;
  }
  if ((val & 0xFF000000U) == 0) {
    n += 8;
    val <<= 8;
  }
  if ((val & 0xF0000000U) == 0) {
    n += 4;
    val <<= 4;
  }
  if ((val & 0xC0000000U) == 0) {
    n += 2;
    val <<= 2;
  }
  if ((val & 0x80000000U) == 0) {
    n += 1;
  }
  return n;
}

uint64_t ALU::intToDoubleBits(sword_t a) {
  if (a == 0)
    return 0ULL;
  uint64_t sign = (a < 0) ? 1ULL : 0ULL;
  uint32_t abs_val = (a < 0) ? static_cast<uint32_t>(-static_cast<int64_t>(a))
                             : static_cast<uint32_t>(a);
  uint32_t lz = countLeadingZeros32(abs_val);
  uint32_t msb_pos = 31 - lz;
  uint64_t exp = 1023ULL + msb_pos;
  uint64_t mantissa_bits = abs_val & ~(1U << msb_pos);
  uint64_t frac = static_cast<uint64_t>(mantissa_bits) << (52 - msb_pos);
  return (sign << 63) | (exp << 52) | frac;
}

sword_t ALU::doubleBitsToInt(uint64_t double_bits) {
  if (double_bits == 0)
    return 0;
  uint64_t sign = (double_bits >> 63) & 1;
  int32_t exp = static_cast<int32_t>((double_bits >> 52) & 0x7FF) - 1023;
  if (exp < 0)
    return 0;
  if (exp >= 31) {
    return sign ? static_cast<sword_t>(0x80000000U) : 0x7FFFFFFF;
  }
  uint64_t frac = double_bits & 0x000FFFFFFFFFFFFFULL;
  uint64_t mantissa = (1ULL << 52) | frac;
  uint64_t val = mantissa >> (52 - exp);
  return sign ? -static_cast<sword_t>(val) : static_cast<sword_t>(val);
}

// ============================================================================
// 5. IEEE-754 Single-Precision Floating-Point Multiplier (Figure 8.18
// Flowchart)
// ============================================================================
word_t ALU::fpMultiply32(word_t a, word_t b, bool &ovf, bool &unf) {
  ovf = false;
  unf = false;

  // 1. Check A == 0 or B == 0 -> Return 0
  if ((a & 0x7FFFFFFFU) == 0 || (b & 0x7FFFFFFFU) == 0) {
    uint32_t s = ((a >> 31) ^ (b >> 31)) & 1;
    return (s << 31);
  }

  // 2. Sign: S = sign(A) ^ sign(B)
  uint32_t S = ((a >> 31) ^ (b >> 31)) & 1;
  int32_t EA = static_cast<int32_t>((a >> 23) & 0xFF);
  int32_t EB = static_cast<int32_t>((b >> 23) & 0xFF);
  uint32_t MA = a & 0x7FFFFFU;
  uint32_t MB = b & 0x7FFFFFU;

  // 3. Exponent: E = EA + EB - bias (bias = 127)
  int32_t E = EA + EB - 127;

  // 4. Early Overflow / Underflow check
  if (E > 254) {
    ovf = true;
    return (S << 31) | 0x7F800000U; // Infinity
  }
  if (E < -24) {
    unf = true;
    return (S << 31); // 0
  }

  // 5. Significand Multiplication: W = PA * PB (implicit leading 1)
  uint64_t PA = (1ULL << 23) | MA;
  uint64_t PB = (1ULL << 23) | MB;
  uint64_t W = PA * PB; // 48-bit product

  // 6. Normalise W and update E
  if (W & (1ULL << 47)) {
    W >>= 1;
    E += 1;
  }

  // 7. Overflow / Underflow check
  if (E > 254) {
    ovf = true;
    return (S << 31) | 0x7F800000U;
  }
  if (E < 1) {
    unf = true;
    return (S << 31);
  }

  // 8. Round W (Round to Nearest Even - RNE)
  bool guard = (W >> 22) & 1;
  bool round_bit = (W >> 21) & 1;
  bool sticky = (W & 0x1FFFFFULL) != 0;
  bool lsb = (W >> 23) & 1;

  if (guard && (round_bit || sticky || lsb)) {
    W += (1ULL << 23);
  }

  // 9. Normalise W and update E (if rounding caused overflow into bit 47)
  if (W & (1ULL << 47)) {
    W >>= 1;
    E += 1;
  }

  // 10. Final Overflow / Underflow check
  if (E > 254) {
    ovf = true;
    return (S << 31) | 0x7F800000U;
  }
  if (E < 1) {
    unf = true;
    return (S << 31);
  }

  // 11. Construct C out of W, E, and S
  uint32_t mantissa = static_cast<uint32_t>((W >> 23) & 0x7FFFFFU);
  word_t C = (S << 31) | ((static_cast<uint32_t>(E) & 0xFF) << 23) | mantissa;
  return C;
}

// ============================================================================
// 6. IEEE-754 Single-Precision Floating-Point Divider (Newton-Raphson)
// ============================================================================
word_t ALU::fpDivide32(word_t a, word_t b, bool &ovf, bool &unf, bool &div0) {
  ovf = false;
  unf = false;
  div0 = false;

  // Check divide by zero
  if ((b & 0x7FFFFFFFU) == 0) {
    div0 = true;
    ovf = true;
    uint32_t s = ((a >> 31) ^ (b >> 31)) & 1;
    return (s << 31) | 0x7F800000U; // Infinity
  }

  // Check dividend zero
  if ((a & 0x7FFFFFFFU) == 0) {
    uint32_t s = ((a >> 31) ^ (b >> 31)) & 1;
    return (s << 31);
  }

  uint32_t S = ((a >> 31) ^ (b >> 31)) & 1;
  int32_t EA = static_cast<int32_t>((a >> 23) & 0xFF);
  int32_t EB = static_cast<int32_t>((b >> 23) & 0xFF);
  uint32_t MA = a & 0x7FFFFFU;
  uint32_t MB = b & 0x7FFFFFU;

  // Normalize divisor mantissa D in [1.0, 2.0)
  double D = 1.0 + static_cast<double>(MB) / (1ULL << 23);
  // Minimax linear seed for 1/D in range [1.0, 2.0]
  double X = (24.0 / 17.0) - (8.0 / 17.0) * D;

  // Newton-Raphson Reciprocal Iteration: X_{k+1} = X_k * (2 - D * X_k)
  for (int k = 0; k < 5; ++k) {
    X = X * (2.0 - D * X);
  }

  double N = 1.0 + static_cast<double>(MA) / (1ULL << 23);
  double W_fp = N * X;
  int32_t E = EA - EB + 127;

  // Normalization into [1.0, 2.0)
  if (W_fp >= 2.0) {
    W_fp /= 2.0;
    E += 1;
  } else if (W_fp < 1.0) {
    W_fp *= 2.0;
    E -= 1;
  }

  // Rounding and mantissa extraction
  double frac = W_fp - 1.0;
  uint32_t mant_rounded =
      static_cast<uint32_t>(std::round(frac * (1ULL << 23)));
  if (mant_rounded >= (1U << 23)) {
    mant_rounded = 0;
    E += 1;
  }
  uint32_t mantissa = mant_rounded & 0x7FFFFFU;

  if (E > 254) {
    ovf = true;
    return (S << 31) | 0x7F800000U;
  }
  if (E < 1) {
    unf = true;
    return (S << 31);
  }

  return (S << 31) | ((static_cast<uint32_t>(E) & 0xFF) << 23) | mantissa;
}

// ============================================================================
// 7. IEEE-754 Single-Precision Floating-Point Addition & Subtraction
// ============================================================================
word_t ALU::fpAdd32(word_t a, word_t b, bool &ovf, bool &unf) {
  float fa, fb;
  std::memcpy(&fa, &a, sizeof(float));
  std::memcpy(&fb, &b, sizeof(float));
  float fc = fa + fb;
  word_t res;
  std::memcpy(&res, &fc, sizeof(word_t));
  uint32_t exp = (res >> 23) & 0xFF;
  ovf = (exp == 255);
  unf = (exp == 0 && (res & 0x7FFFFFFFU) != 0);
  return res;
}

word_t ALU::fpSub32(word_t a, word_t b, bool &ovf, bool &unf) {
  word_t neg_b = b ^ 0x80000000U; // Invert sign bit
  return fpAdd32(a, neg_b, ovf, unf);
}

// ============================================================================
// Core ALU Execution Dispatcher
// ============================================================================
ALUResult ALU::execute(ALUOp op, word_t a, word_t b,
                       const Flags &current_flags) {
  ALUResult res;
  res.flags = current_flags; // Preserve existing flags unless modified

  sword_t sa = static_cast<sword_t>(a);
  sword_t sb = static_cast<sword_t>(b);

  switch (op) {
  case ALUOp::ADD: {
    bool cout = false;
    bool vout = false;
    res.result = claAdd32(a, b, false, cout, vout);
    res.flags.C = cout;
    res.flags.V = vout;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::SUB: {
    // A - B = A + (~B) + 1
    bool cout = false;
    bool vout = false;
    res.result = claAdd32(a, ~b, true, cout, vout);
    res.flags.C = cout; // cout = 1 means no borrow
    res.flags.V = vout;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::MUL: {
    bool vout = false;
    res.result = boothMultiply32(sa, sb, vout);
    res.flags.V = vout;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::DIV: {
    sword_t rem = 0;
    bool div0 = false;
    if (b == 0) {
      res.flags.DIV0 = true;
      res.result = 0;
      throw DivisionByZeroException(0);
    }
    res.result = static_cast<word_t>(newtonRaphsonDivide32(sa, sb, rem, div0));
    res.flags.DIV0 = div0;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::MOD: {
    sword_t rem = 0;
    bool div0 = false;
    if (b == 0) {
      res.flags.DIV0 = true;
      res.result = 0;
      throw DivisionByZeroException(0);
    }
    newtonRaphsonDivide32(sa, sb, rem, div0);
    res.result = static_cast<word_t>(rem);
    res.flags.DIV0 = div0;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::CMP: {
    // SimpleRISC CMP: Sets E if equal, GT if signed(a) > signed(b)
    bool cout = false;
    bool vout = false;
    res.result = claAdd32(a, ~b, true, cout, vout);
    res.flags.E = (a == b);
    res.flags.GT = (sa > sb);
    res.flags.C = cout;
    res.flags.V = vout;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::AND:
    res.result = a & b;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;

  case ALUOp::OR:
    res.result = a | b;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;

  case ALUOp::NOT:
    res.result = ~b;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;

  case ALUOp::MOV:
    res.result = b;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;

  case ALUOp::LSL: {
    uint32_t shift = b & 0x1F;
    res.result = a << shift;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::LSR: {
    uint32_t shift = b & 0x1F;
    res.result = a >> shift;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::ASR: {
    uint32_t shift = b & 0x1F;
    res.result = static_cast<word_t>(sa >> shift);
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::PASS_A:
    res.result = a;
    break;

  case ALUOp::PASS_B:
    res.result = b;
    break;

  case ALUOp::I2D: {
    uint64_t dbits = intToDoubleBits(sa);
    // Returns upper 32 bits containing sign, exponent, and upper 20 mantissa
    // bits
    res.result = static_cast<word_t>((dbits >> 32) & 0xFFFFFFFFU);
    res.flags.Z = (a == 0);
    res.flags.N = (sa < 0);
    break;
  }

  case ALUOp::D2I: {
    uint64_t dbits = (static_cast<uint64_t>(a) << 32);
    res.result = static_cast<word_t>(doubleBitsToInt(dbits));
    res.flags.Z = (res.result == 0);
    res.flags.N = ((res.result & 0x80000000U) != 0);
    break;
  }

  case ALUOp::FDIV: {
    sword_t rem = 0;
    bool div0 = false;
    if (b == 0) {
      res.flags.DIV0 = true;
      res.result = 0;
      throw DivisionByZeroException(0);
    }
    res.result = static_cast<word_t>(newtonRaphsonDivide32(sa, sb, rem, div0));
    res.flags.DIV0 = div0;
    res.flags.N = ((res.result & 0x80000000U) != 0);
    res.flags.Z = (res.result == 0);
    break;
  }

  case ALUOp::FMUL: {
    bool ovf = false, unf = false;
    res.result = fpMultiply32(a, b, ovf, unf);
    res.flags.FP_OVF = ovf;
    res.flags.FP_UNF = unf;
    res.flags.Z = ((res.result & 0x7FFFFFFFU) == 0);
    res.flags.N = ((res.result & 0x80000000U) != 0);
    break;
  }

  case ALUOp::FADD: {
    bool ovf = false, unf = false;
    res.result = fpAdd32(a, b, ovf, unf);
    res.flags.FP_OVF = ovf;
    res.flags.FP_UNF = unf;
    res.flags.Z = ((res.result & 0x7FFFFFFFU) == 0);
    res.flags.N = ((res.result & 0x80000000U) != 0);
    break;
  }

  case ALUOp::FSUB: {
    bool ovf = false, unf = false;
    res.result = fpSub32(a, b, ovf, unf);
    res.flags.FP_OVF = ovf;
    res.flags.FP_UNF = unf;
    res.flags.Z = ((res.result & 0x7FFFFFFFU) == 0);
    res.flags.N = ((res.result & 0x80000000U) != 0);
    break;
  }

  case ALUOp::FCMP: {
    float fa, fb;
    std::memcpy(&fa, &a, sizeof(float));
    std::memcpy(&fb, &b, sizeof(float));
    res.flags.E = (fa == fb);
    res.flags.GT = (fa > fb);
    res.flags.Z = (fa == fb);
    res.flags.N = (fa < fb);
    res.result = (fa > fb) ? 1 : ((fa == fb) ? 0 : static_cast<word_t>(-1));
    break;
  }
  }

  return res;
}

ALUOp ALU::fromOpcode(Opcode op) {
  switch (op) {
  case OP_ADD:
    return ALUOp::ADD;
  case OP_SUB:
    return ALUOp::SUB;
  case OP_MUL:
    return ALUOp::MUL;
  case OP_DIV:
    return ALUOp::DIV;
  case OP_MOD:
    return ALUOp::MOD;
  case OP_CMP:
    return ALUOp::CMP;
  case OP_AND:
    return ALUOp::AND;
  case OP_OR:
    return ALUOp::OR;
  case OP_NOT:
    return ALUOp::NOT;
  case OP_MOV:
    return ALUOp::MOV;
  case OP_LSL:
    return ALUOp::LSL;
  case OP_LSR:
    return ALUOp::LSR;
  case OP_ASR:
    return ALUOp::ASR;
  case OP_I2D:
    return ALUOp::I2D;
  case OP_D2I:
    return ALUOp::D2I;
  case OP_FDIV:
    return ALUOp::FDIV;
  default:
    return ALUOp::ADD;
  }
}

std::string ALU::opName(ALUOp op) {
  switch (op) {
  case ALUOp::ADD:
    return "ADD";
  case ALUOp::SUB:
    return "SUB";
  case ALUOp::MUL:
    return "MUL";
  case ALUOp::DIV:
    return "DIV";
  case ALUOp::MOD:
    return "MOD";
  case ALUOp::CMP:
    return "CMP";
  case ALUOp::AND:
    return "AND";
  case ALUOp::OR:
    return "OR";
  case ALUOp::NOT:
    return "NOT";
  case ALUOp::MOV:
    return "MOV";
  case ALUOp::LSL:
    return "LSL";
  case ALUOp::LSR:
    return "LSR";
  case ALUOp::ASR:
    return "ASR";
  case ALUOp::PASS_A:
    return "PASS_A";
  case ALUOp::PASS_B:
    return "PASS_B";
  case ALUOp::I2D:
    return "I2D";
  case ALUOp::D2I:
    return "D2I";
  case ALUOp::FDIV:
    return "FDIV";
  case ALUOp::FADD:
    return "FADD";
  case ALUOp::FSUB:
    return "FSUB";
  case ALUOp::FMUL:
    return "FMUL";
  case ALUOp::FCMP:
    return "FCMP";
  }
  return "UNKNOWN";
}

} // namespace simplerisc
