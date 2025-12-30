#ifndef BRISE_RANDOM_H
#define BRISE_RANDOM_H

#include <string>
#include <map>
#include <cstdlib>
#include <sstream>

namespace BriseRandom {
    void quick_rand(std::string range, std::map<std::string, std::string>& vars) {
        std::stringstream ss(range);
        int min, max;
        if (ss >> min >> max) {
            int res = min + (std::rand() % (max - min + 1));
            vars["random"] = std::to_string(res);
        }
    }
}
#endif
