#ifndef BRISE_MATH_H
#define BRISE_MATH_H

#include <string>
#include <map>

namespace BriseMath {
    void solve_simple(std::string expr, std::map<std::string, std::string>& vars) {
        size_t plusPos = expr.find("+");
        if (plusPos != std::string::npos) {
            try {
                double n1 = std::stod(expr.substr(0, plusPos));
                double n2 = std::stod(expr.substr(plusPos + 1));
                vars["answer"] = std::to_string((int)(n1 + n2)); 
            } catch (...) { vars["answer"] = "Error"; }
        }
    }
}
#endif
