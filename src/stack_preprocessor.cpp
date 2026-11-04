#include "stack_preprocessor.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace simplerisc {

std::string StackPreprocessor::trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(static_cast<unsigned char>(*end)));
    return std::string(start, end + 1);
}

std::vector<std::string> StackPreprocessor::splitTokens(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        std::string t = trim(item);
        if (!t.empty()) {
            tokens.push_back(t);
        }
    }
    return tokens;
}

std::vector<std::string> StackPreprocessor::expandPush(const std::string& args) {
    // Accepts formats like: "r1", "r1, r2, r3", or "{r1, r2, ra}"
    std::string clean = trim(args);
    if (!clean.empty() && clean.front() == '{' && clean.back() == '}') {
        clean = clean.substr(1, clean.size() - 2);
    }
    std::vector<std::string> regs = splitTokens(clean, ',');
    if (regs.empty()) return {};

    std::vector<std::string> expanded;

    if (regs.size() == 1) {
        // Single register push: standard full-descending
        expanded.push_back("sub sp, sp, 4");
        expanded.push_back("st " + regs[0] + ", 0[sp]");
    } else {
        // Multi-register push optimization (Solution 1: Consolidated Stack Frame Allocation)
        // Subtract total bytes once, then store using positive indexed offsets
        int total_bytes = static_cast<int>(regs.size() * 4);
        expanded.push_back("sub sp, sp, " + std::to_string(total_bytes));
        for (size_t i = 0; i < regs.size(); ++i) {
            int offset = static_cast<int>((regs.size() - 1 - i) * 4);
            expanded.push_back("st " + regs[i] + ", " + std::to_string(offset) + "[sp]");
        }
    }
    return expanded;
}

std::vector<std::string> StackPreprocessor::expandPop(const std::string& args) {
    // Accepts formats like: "r1", "r1, r2, r3", or "{r1, r2, ra}"
    std::string clean = trim(args);
    if (!clean.empty() && clean.front() == '{' && clean.back() == '}') {
        clean = clean.substr(1, clean.size() - 2);
    }
    std::vector<std::string> regs = splitTokens(clean, ',');
    if (regs.empty()) return {};

    std::vector<std::string> expanded;

    if (regs.size() == 1) {
        // Single register pop
        expanded.push_back("ld " + regs[0] + ", 0[sp]");
        expanded.push_back("add sp, sp, 4");
    } else {
        // Multi-register pop optimization (Solution 1: Consolidated Stack Frame Deallocation)
        // Pop in reverse order (LIFO) using indexed offsets, then adjust SP once
        int total_bytes = static_cast<int>(regs.size() * 4);
        std::reverse(regs.begin(), regs.end());
        for (size_t i = 0; i < regs.size(); ++i) {
            int offset = static_cast<int>(i * 4);
            expanded.push_back("ld " + regs[i] + ", " + std::to_string(offset) + "[sp]");
        }
        expanded.push_back("add sp, sp, " + std::to_string(total_bytes));
    }
    return expanded;
}

std::vector<std::string> StackPreprocessor::expandEnter(const std::string& args) {
    // Allocates stack frame: e.g. "enter 16" -> "sub sp, sp, 16"
    std::string bytes_str = trim(args);
    if (bytes_str.empty()) bytes_str = "0";
    return {"sub sp, sp, " + bytes_str};
}

std::vector<std::string> StackPreprocessor::expandLeave(const std::string& args) {
    // Deallocates stack frame: e.g. "leave 16" -> "add sp, sp, 16"
    std::string bytes_str = trim(args);
    if (bytes_str.empty()) bytes_str = "0";
    return {"add sp, sp, " + bytes_str};
}

PreprocessorReport StackPreprocessor::process(const std::string& source) {
    PreprocessorReport rep;
    std::istringstream stream(source);
    std::ostringstream out;
    std::string line;

    while (std::getline(stream, line)) {
        rep.original_lines++;
        std::string clean_line = trim(line);

        // Preserve empty lines and comments
        if (clean_line.empty() || clean_line[0] == ';' || clean_line[0] == '#') {
            out << line << "\n";
            rep.generated_lines++;
            continue;
        }

        // Check if there is a label preceding a macro
        std::string label_part;
        size_t colon_pos = clean_line.find(':');
        if (colon_pos != std::string::npos) {
            label_part = clean_line.substr(0, colon_pos + 1);
            clean_line = trim(clean_line.substr(colon_pos + 1));
        }

        if (clean_line.empty()) {
            out << label_part << "\n";
            rep.generated_lines++;
            continue;
        }

        // Parse mnemonic and arguments
        std::stringstream line_ss(clean_line);
        std::string mnemonic;
        line_ss >> mnemonic;
        std::string args;
        std::getline(line_ss, args);
        args = trim(args);

        // Convert mnemonic to lowercase
        std::string lower_mnemonic = mnemonic;
        std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), lower_mnemonic.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        std::vector<std::string> expanded_insts;
        bool is_macro = false;

        if (lower_mnemonic == "push") {
            expanded_insts = expandPush(args);
            is_macro = true;
        } else if (lower_mnemonic == "pop") {
            expanded_insts = expandPop(args);
            is_macro = true;
        } else if (lower_mnemonic == "enter") {
            expanded_insts = expandEnter(args);
            is_macro = true;
        } else if (lower_mnemonic == "leave") {
            expanded_insts = expandLeave(args);
            is_macro = true;
        }

        if (is_macro) {
            rep.macros_expanded++;
            if (!label_part.empty()) {
                out << label_part << "\n";
                rep.generated_lines++;
            }
            out << "  ; [Expanded macro: " << mnemonic << " " << args << "]\n";
            for (const auto& inst : expanded_insts) {
                out << "  " << inst << "\n";
                rep.generated_lines++;
            }
        } else {
            // Not a stack macro, keep original line
            if (!label_part.empty()) {
                out << label_part << " " << clean_line << "\n";
            } else {
                out << line << "\n";
            }
            rep.generated_lines++;
        }
    }

    rep.expanded_source = out.str();
    rep.success = true;
    return rep;
}

PreprocessorReport StackPreprocessor::processFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        PreprocessorReport rep;
        rep.success = false;
        rep.error_message = "Cannot open source file: " + filepath;
        return rep;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return process(buffer.str());
}

} // namespace simplerisc
