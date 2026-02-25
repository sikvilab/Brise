#include "brise_runtime.h"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "brise v0.4.0-dev | C core + C++ launcher" << std::endl;
        std::cout << "Usage: brise <file.bri>" << std::endl;
        return 0;
    }

    BriseRuntime* rt = brise_runtime_create();
    if (!rt) {
        std::cerr << "[Fatal]: cannot create runtime" << std::endl;
        return 2;
    }

    const int ok = brise_execute_file(rt, argv[1]);
    if (!ok) {
        std::cerr << brise_last_error(rt) << std::endl;
        brise_runtime_destroy(rt);
        return 1;
    }

    std::cout << "\n----------------------------" << std::endl;
    std::cout << "Program finished." << std::endl;

    brise_runtime_destroy(rt);
    return 0;
}
