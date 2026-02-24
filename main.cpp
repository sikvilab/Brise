#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <regex>
#include <sstream>
#include <ctime>
#include <cctype>
#include <iomanip>

// Подключаем наши модули из папки libs
#include "libs/brise_math.h"
#include "libs/brise_random.h"

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

bool parse_double_token(const std::string& token, double& result) {
    std::string parsed = trim(token);
    try {
        size_t idx = 0;
        result = std::stod(parsed, &idx);
        return idx == parsed.size();
    } catch (...) {
        return false;
    }
}

bool get_numeric_value(const std::string& token, double& value) {
    if (parse_double_token(token, value)) return true;
    auto it = vars.find(trim(token));
    if (it == vars.end()) return false;
    return parse_double_token(it->second, value);
}

std::string format_number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text.empty() ? "0" : text;
}

int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

bool apply_top_operator(std::vector<double>& values, std::vector<char>& ops) {
    if (values.size() < 2 || ops.empty()) return false;
    double b = values.back(); values.pop_back();
    double a = values.back(); values.pop_back();
    char op = ops.back(); ops.pop_back();

    if (op == '+') values.push_back(a + b);
    else if (op == '-') values.push_back(a - b);
    else if (op == '*') values.push_back(a * b);
    else if (op == '/') {
        if (b == 0.0) return false;
        values.push_back(a / b);
    }
    else return false;
    return true;
}

bool evaluate_expression(const std::string& expression, double& result) {
    std::vector<double> values;
    std::vector<char> ops;

    for (size_t i = 0; i < expression.size();) {
        char ch = expression[i];

        if (std::isspace(static_cast<unsigned char>(ch))) {
            i++;
            continue;
        }

        bool unaryMinus = (ch == '-') && (i == 0 || expression[i - 1] == '(' || expression[i - 1] == '+' || expression[i - 1] == '-' || expression[i - 1] == '*' || expression[i - 1] == '/');
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || unaryMinus) {
            size_t start = i;
            if (ch == '-') i++;
            while (i < expression.size() && (std::isdigit(static_cast<unsigned char>(expression[i])) || expression[i] == '.')) i++;
            double numeric = 0.0;
            if (!parse_double_token(expression.substr(start, i - start), numeric)) return false;
            values.push_back(numeric);
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            size_t start = i;
            while (i < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[i])) || expression[i] == '_')) i++;
            double numeric = 0.0;
            if (!get_numeric_value(expression.substr(start, i - start), numeric)) return false;
            values.push_back(numeric);
            continue;
        }

        if (ch == '(') {
            ops.push_back(ch);
            i++;
            continue;
        }

        if (ch == ')') {
            while (!ops.empty() && ops.back() != '(') {
                if (!apply_top_operator(values, ops)) return false;
            }
            if (ops.empty() || ops.back() != '(') return false;
            ops.pop_back();
            i++;
            continue;
        }

        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            while (!ops.empty() && precedence(ops.back()) >= precedence(ch)) {
                if (!apply_top_operator(values, ops)) return false;
            }
            ops.push_back(ch);
            i++;
            continue;
        }

        return false;
    }

    while (!ops.empty()) {
        if (ops.back() == '(') return false;
        if (!apply_top_operator(values, ops)) return false;
    }

    if (values.size() != 1) return false;
    result = values.back();
    return true;
}

bool evaluate_condition(const std::string& rawCondition) {
    std::string condition = trim(rawCondition);
    const std::vector<std::string> operators = {"==", "!=", ">=", "<=", ">", "<"};

    for (const std::string& op : operators) {
        size_t pos = condition.find(op);
        if (pos == std::string::npos) continue;

        std::string left = trim(condition.substr(0, pos));
        std::string right = trim(condition.substr(pos + op.size()));

        double leftNum = 0.0;
        double rightNum = 0.0;
        bool leftIsNum = get_numeric_value(left, leftNum);
        bool rightIsNum = get_numeric_value(right, rightNum);

        if (leftIsNum && rightIsNum) {
            if (op == "==") return leftNum == rightNum;
            if (op == "!=") return leftNum != rightNum;
            if (op == ">=") return leftNum >= rightNum;
            if (op == "<=") return leftNum <= rightNum;
            if (op == ">") return leftNum > rightNum;
            if (op == "<") return leftNum < rightNum;
        }

        std::string leftText = vars.count(left) ? vars[left] : left;
        std::string rightText = vars.count(right) ? vars[right] : right;

        if (op == "==") return leftText == rightText;
        if (op == "!=") return leftText != rightText;
        if (op == ">=") return leftText >= rightText;
        if (op == "<=") return leftText <= rightText;
        if (op == ">") return leftText > rightText;
        if (op == "<") return leftText < rightText;
    }

    return false;
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
        size_t s = line.find("\"");
        size_t e = line.rfind("\"");
        if (s != std::string::npos && e != std::string::npos && e > s) {
            std::cout << process_text(line.substr(s + 1, e - s - 1)) << std::endl;
        } else {
            std::cout << process_text(trim(line.substr(4))) << std::endl;
        }
    }
    else if (line.substr(0, 4) == "set:") {
        size_t eq = line.find("=");
        std::string name = trim(line.substr(4, eq - 4));
        std::string val = trim(line.substr(eq + 1));
        if (!val.empty() && val.front() == '"' && val.back() == '"') val = val.substr(1, val.length() - 2);
        vars[name] = process_text(val);
    }
    else if (line.substr(0, 5) == "calc:") {
        size_t eq = line.find("=");
        std::string name = trim(line.substr(5, eq - 5));
        std::string expr = trim(line.substr(eq + 1));
        double result = 0.0;
        if (evaluate_expression(expr, result)) {
            vars[name] = format_number(result);
        } else {
            vars[name] = "Error";
        }
    }
    // --- НОВОЕ: Вызов библиотек из libs ---
    else if (line.substr(0, 6) == "solve:") {
        size_t s = line.find("\"") + 1;
        size_t e = line.rfind("\"");
        if (s != 0 && e != std::string::npos) {
            BriseMath::solve_simple(process_text(line.substr(s, e - s)), vars);
        }
    }
    else if (line.substr(0, 7) == "random:") {
        BriseRandom::quick_rand(process_text(line.substr(7)), vars);
    }
    else if (line.substr(0, 3) == "if:") {
        size_t s_bracket = line.find("(");
        size_t e_bracket = line.rfind(")");
        if (s_bracket != std::string::npos && e_bracket != std::string::npos && e_bracket > s_bracket) {
            std::string condition = line.substr(3, s_bracket - 3);
            if (evaluate_condition(condition)) {
                execute(line.substr(s_bracket + 1, e_bracket - s_bracket - 1));
            }
        }
    }
    // --- КОНЕЦ НОВЫХ КОМАНД ---
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
    srand(time(0)); // Инициализация рандома
    if (argc < 2) {
        std::cout << "brise v0.2.0 Alpha | Sikvilab" << std::endl;
        std::cout << "Usage: brise <file.bri>" << std::endl;
        system("pause");
        return 0;
    }
    run_file(argv[1]);
    std::cout << "\n----------------------------" << std::endl;
    std::cout << "Program finished." << std::endl;
    system("pause");
    return 0;
}
