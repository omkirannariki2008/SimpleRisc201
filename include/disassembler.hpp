#ifndef SIMPLERISC_DISASSEMBLER_HPP
#define SIMPLERISC_DISASSEMBLER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace simplerisc {

struct DisassembledInstruction {
    addr_t address{0};
    word_t machine_word{0};
    std::string mnemonic;
    std::string operands;
    std::string full_assembly;
    bool valid{false};
};

class Disassembler {
public:
    Disassembler() = default;

    // Disassemble a single 32-bit machine word at a given PC address
    static DisassembledInstruction disassemble(word_t word, addr_t pc = 0);

    // Disassemble a sequence of machine words
    static std::vector<DisassembledInstruction> disassembleProgram(
        const std::vector<word_t>& words, addr_t base_address = 0);

    // Disassemble from hex string (e.g. "0x4c000005" or "4c000005")
    static DisassembledInstruction disassembleHex(const std::string& hex_str, addr_t pc = 0);

    // Disassemble from 32-character binary string (e.g. "010011...")
    static DisassembledInstruction disassembleBinary(const std::string& bin_str, addr_t pc = 0);

    // Format disassembler output into human-readable listing string
    static std::string formatListing(const std::vector<DisassembledInstruction>& insts);

private:
    static std::string regName(reg_t r);
    static std::string opcodeName(Opcode op);
};

} // namespace simplerisc

#endif // SIMPLERISC_DISASSEMBLER_HPP
