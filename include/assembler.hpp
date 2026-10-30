#ifndef SIMPLERISC_ASSEMBLER_HPP
#define SIMPLERISC_ASSEMBLER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace simplerisc {

struct AssemblyResult {
    bool success{false};
    std::string error_message;
    std::vector<word_t> machine_words;
    std::unordered_map<std::string, addr_t> symbols;
    uint32_t load_use_slots_optimized{0};
};

class Assembler {
public:
    Assembler() = default;

    AssemblyResult assemble(const std::string& source);
    AssemblyResult assembleFile(const std::string& filepath);

    void setOptimizeLoadUse(bool enable) { optimize_load_use_ = enable; }
    bool isOptimizeLoadUse() const { return optimize_load_use_; }

private:
    bool optimize_load_use_{true};
    std::unordered_map<std::string, addr_t> symbol_table_;

    void reset();
    bool parseRegister(const std::string& token, reg_t& out_reg);
    bool parseImmediate(const std::string& token, int32_t& out_imm);
};

} // namespace simplerisc

#endif // SIMPLERISC_ASSEMBLER_HPP
