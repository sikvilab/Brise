#include "interpreter.h"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "brise v0.3.0-dev | Sikvilab" << std::endl;
        std::cout << "Usage: brise <file.bri>" << std::endl;
        return 0;
    }

    Brise::Interpreter interpreter;
    try {
        interpreter.executeFile(argv[1]);
        std::cout << "\n----------------------------" << std::endl;
        std::cout << "Program finished." << std::endl;
    } catch (const Brise::RuntimeError& err) {
        std::cerr << err.what() << std::endl;
        return 1;
    } catch (const std::exception& err) {
        std::cerr << "[Fatal]: " << err.what() << std::endl;
        return 2;
    }

    return 0;
}
