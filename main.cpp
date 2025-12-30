#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <regex>
#include <sstream>

std::map<std::string, std::string> vars;
std::map<std::string, std::string> commands;
std::map<std::string, std::vector<std::string>> lists;

void execute(std::string line);

std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    return (first == std::string::npos) ? "" : s.substr(first, (s.find_last_not_of(" \t\r\n") - first + 1));
}

std::string process_text(std::string text) {
    for (auto const& [name, val] : vars) {
        std::string tag = "(" + name + ")";
        size_t pos = text.find(tag);
        while (pos != std::string::npos) {
            text.replace(pos, tag.length(), val);
            pos = text.find(tag, pos + val.length());
        }
    }
    return text;
}

void run_file(std::string filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }
    std::string line;
    while (std::getline(file, line)) execute(line);
}

void execute(std::string line) {
    line = trim(line);
    if (line.empty() || line.substr(0, 2) == "//") return;

    if (line.substr(0, 8) == "Include:") {
        size_t s = line.find("\"") + 1;
        size_t e = line.rfind("\"");
        run_file(line.substr(s, e - s));
    }
    else if (line.substr(0, 4) == "say:") {
        size_t s = line.find("\"") + 1;
        size_t e = line.rfind("\"");
        if (s != 0 && e != std::string::npos) {
            std::cout << process_text(line.substr(s, e - s)) << std::endl;
        }
    }
    else if (line.substr(0, 4) == "set:") {
        size_t eq = line.find("=");
        std::string name = trim(line.substr(4, eq - 4));
        std::string val = trim(line.substr(eq + 1));
        if (val.front() == '"' && val.back() == '"') val = val.substr(1, val.length() - 2);
        vars[name] = val;
    }
    else if (line.substr(0, 5) == "List:") {
        std::regex list_regex(R"(List:\s+(\w+)\s+\((.*)\))");
        std::smatch match;
        if (std::regex_search(line, match, list_regex)) {
            std::string name = match[1];
            std::stringstream ss(match[2].str());
            std::string item;
            while (std::getline(ss, item, ',')) lists[name].push_back(trim(item));
        }
    }
    else if (line.substr(0, 16) == "Say to everyone:") {
        size_t s = line.find("(") + 1;
        size_t e = line.rfind(")");
        std::string cmd = line.substr(s, e - s);
        for (auto const& [list_name, items] : lists) {
            for (const std::string& val : items) {
                std::string temp = cmd;
                size_t pos = temp.find("(item)");
                if (pos != std::string::npos) temp.replace(pos, 6, val);
                execute(temp);
            }
        }
    }
    else if (line.substr(0, 8) == "Command:") {
        size_t space = line.find(" ", 8);
        std::string name = line.substr(8, space - 8);
        commands[name] = line.substr(line.find("(") + 1, line.rfind(")") - line.find("(") - 1);
    }
    else if (commands.count(line)) {
        execute(commands[line]);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "brise v0.1.1 Alpha | Usage: brise <file.bri>" << std::endl;
        system("pause"); // Чтобы окно не закрылось, если запустить без аргументов
        return 0;
    }
    
    run_file(argv[1]);

    std::cout << "\n----------------------------" << std::endl;
    std::cout << "Program finished." << std::endl;
    system("pause"); // ТА САМАЯ ПАУЗА
    return 0;
}