#include "interpreter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace Brise {

namespace {

bool isKeyword(std::string_view word) {
    static const std::vector<std::string> keywords = {
        "say", "set", "calc", "if", "Include", "List", "Command", "random", "solve", "true", "false", "Say", "to", "everyone"
    };
    return std::find(keywords.begin(), keywords.end(), word) != keywords.end();
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool isComparator(const std::string& op) {
    return op == "==" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=";
}

} // namespace

RuntimeError::RuntimeError(ErrorContext context, std::string message)
    : std::runtime_error("[Error at line " + std::to_string(context.line) + " in " + context.file + "]: " + message),
      context_(std::move(context)) {}

std::vector<Token> Lexer::tokenize(std::string_view line) const {
    std::vector<Token> tokens;

    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t start = i;
            while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
            std::string text(line.substr(start, i - start));
            tokens.push_back({isKeyword(text) ? TokenType::Keyword : TokenType::Identifier, std::move(text)});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const std::size_t start = i;
            while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            tokens.push_back({TokenType::Number, std::string(line.substr(start, i - start))});
            continue;
        }

        if (c == '"') {
            ++i;
            const std::size_t start = i;
            while (i < line.size() && line[i] != '"') ++i;
            tokens.push_back({TokenType::String, std::string(line.substr(start, i - start))});
            if (i < line.size() && line[i] == '"') ++i;
            continue;
        }

        if (c == '(') { tokens.push_back({TokenType::LParen, "("}); ++i; continue; }
        if (c == ')') { tokens.push_back({TokenType::RParen, ")"}); ++i; continue; }
        if (c == ',') { tokens.push_back({TokenType::Comma, ","}); ++i; continue; }
        if (c == ':') { tokens.push_back({TokenType::Colon, ":"}); ++i; continue; }

        if (c == '=' || c == '!' || c == '<' || c == '>') {
            if (i + 1 < line.size() && line[i + 1] == '=') {
                tokens.push_back({TokenType::Operator, std::string(line.substr(i, 2))});
                i += 2;
            } else {
                tokens.push_back({TokenType::Operator, std::string(1, c)});
                ++i;
            }
            continue;
        }

        if (c == '+' || c == '-' || c == '*' || c == '/') {
            tokens.push_back({TokenType::Operator, std::string(1, c)});
            ++i;
            continue;
        }

        ++i;
    }

    tokens.push_back({TokenType::End, ""});
    return tokens;
}

void Environment::setVariable(const std::string& name, Value value) { variables_[name] = std::move(value); }

std::optional<Value> Environment::getVariable(std::string_view name) const {
    auto it = variables_.find(std::string(name));
    if (it == variables_.end()) return std::nullopt;
    return it->second;
}

void Environment::setCommand(const std::string& name, const std::vector<Token>& body) { commands_[name] = body; }

std::optional<std::vector<Token>> Environment::getCommand(std::string_view name) const {
    auto it = commands_.find(std::string(name));
    if (it == commands_.end()) return std::nullopt;
    return it->second;
}

void Environment::setList(const std::string& name, std::vector<std::string> items) { lists_[name] = std::move(items); }

const std::unordered_map<std::string, std::vector<std::string>>& Environment::allLists() const noexcept { return lists_; }

void Interpreter::registerHandlers() {
    if (!handlers_.empty()) return;

    handlers_["say"] = [this](const TokenList& t, const ErrorContext& c) { executeSay(t, c); };
    handlers_["set"] = [this](const TokenList& t, const ErrorContext& c) { executeSet(t, c); };
    handlers_["calc"] = [this](const TokenList& t, const ErrorContext& c) { executeCalc(t, c); };
    handlers_["if"] = [this](const TokenList& t, const ErrorContext& c) { executeIf(t, c); };
    handlers_["Include"] = [this](const TokenList& t, const ErrorContext& c) { executeInclude(t, c); };
    handlers_["List"] = [this](const TokenList& t, const ErrorContext& c) { executeList(t, c); };
    handlers_["Command"] = [this](const TokenList& t, const ErrorContext& c) { executeCommandDefinition(t, c); };
    handlers_["random"] = [this](const TokenList& t, const ErrorContext& c) { executeRandom(t, c); };
    handlers_["solve"] = [this](const TokenList& t, const ErrorContext& c) { executeSolve(t, c); };
}

void Interpreter::fail(const ErrorContext& ctx, const std::string& message) const { throw RuntimeError(ctx, message); }

std::string Interpreter::valueToString(const Value& value) const {
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    std::ostringstream oss;
    oss << std::get<double>(value);
    return oss.str();
}

std::optional<double> Interpreter::toNumber(const Value& value) const {
    if (std::holds_alternative<double>(value)) return std::get<double>(value);
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? 1.0 : 0.0;

    try {
        const std::string& s = std::get<std::string>(value);
        std::size_t idx = 0;
        double d = std::stod(s, &idx);
        if (idx == s.size()) return d;
    } catch (...) {
    }
    return std::nullopt;
}

bool Interpreter::toBool(const Value& value) const {
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    return !std::get<std::string>(value).empty();
}

std::string Interpreter::interpolate(std::string text) const {
    for (std::size_t pos = 0; (pos = text.find('(', pos)) != std::string::npos;) {
        const std::size_t end = text.find(')', pos);
        if (end == std::string::npos) break;

        const std::string name = text.substr(pos + 1, end - pos - 1);
        if (auto value = env_.getVariable(name)) {
            const std::string replacement = valueToString(*value);
            text.replace(pos, end - pos + 1, replacement);
            pos += replacement.size();
        } else {
            pos = end + 1;
        }
    }
    return text;
}

Interpreter::TokenList Interpreter::tokenSlice(const TokenList& tokens, std::size_t begin, std::size_t endExclusive) const {
    TokenList part(tokens.begin() + static_cast<long>(begin), tokens.begin() + static_cast<long>(endExclusive));
    if (part.empty() || part.back().type != TokenType::End) part.push_back({TokenType::End, ""});
    return part;
}

std::size_t Interpreter::findMatchingRParen(const TokenList& tokens, std::size_t lParenPos, const ErrorContext& ctx) const {
    int depth = 0;
    for (std::size_t i = lParenPos; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LParen) ++depth;
        if (tokens[i].type == TokenType::RParen) {
            --depth;
            if (depth == 0) return i;
        }
    }
    fail(ctx, "Unmatched '(' token");
}

Value Interpreter::parsePrimary(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx) {
    const auto& tk = tokens.at(pos);

    if (tk.type == TokenType::Number) {
        ++pos;
        return std::stod(tk.text);
    }
    if (tk.type == TokenType::String) {
        ++pos;
        return interpolate(tk.text);
    }
    if (tk.type == TokenType::Keyword && (tk.text == "true" || tk.text == "false")) {
        ++pos;
        return tk.text == "true";
    }
    if (tk.type == TokenType::Identifier || tk.type == TokenType::Keyword) {
        ++pos;
        auto value = env_.getVariable(tk.text);
        if (!value) fail(ctx, "Unknown variable: " + tk.text);
        return *value;
    }
    if (tk.type == TokenType::LParen) {
        ++pos;
        Value v = parseExpression(tokens, pos, ctx);
        if (tokens.at(pos).type != TokenType::RParen) fail(ctx, "Expected ')' in expression");
        ++pos;
        return v;
    }

    fail(ctx, "Unexpected token in expression: " + tk.text);
}

Value Interpreter::parseFactor(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx) {
    if (tokens.at(pos).type == TokenType::Operator && tokens.at(pos).text == "-") {
        ++pos;
        Value v = parseFactor(tokens, pos, ctx);
        auto n = toNumber(v);
        if (!n) fail(ctx, "Unary minus expects numeric value");
        return -*n;
    }
    return parsePrimary(tokens, pos, ctx);
}

Value Interpreter::parseTerm(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx) {
    Value left = parseFactor(tokens, pos, ctx);

    while (tokens.at(pos).type == TokenType::Operator && (tokens.at(pos).text == "*" || tokens.at(pos).text == "/")) {
        const std::string op = tokens.at(pos++).text;
        Value right = parseFactor(tokens, pos, ctx);
        auto ln = toNumber(left);
        auto rn = toNumber(right);
        if (!ln || !rn) fail(ctx, "Arithmetic expects numeric values");
        if (op == "/" && *rn == 0.0) fail(ctx, "Division by zero");
        if (op == "*") {
            left = Value(*ln * *rn);
        } else {
            left = Value(*ln / *rn);
        }
    }

    return left;
}

Value Interpreter::parseExpression(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx) {
    Value left = parseTerm(tokens, pos, ctx);

    while (tokens.at(pos).type == TokenType::Operator && (tokens.at(pos).text == "+" || tokens.at(pos).text == "-")) {
        const std::string op = tokens.at(pos++).text;
        Value right = parseTerm(tokens, pos, ctx);

        if (op == "+" && (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right))) {
            left = valueToString(left) + valueToString(right);
            continue;
        }

        auto ln = toNumber(left);
        auto rn = toNumber(right);
        if (!ln || !rn) fail(ctx, "Arithmetic expects numeric values");
        if (op == "+") {
            left = Value(*ln + *rn);
        } else {
            left = Value(*ln - *rn);
        }
    }

    return left;
}

bool Interpreter::evaluateCondition(const TokenList& tokens, const ErrorContext& ctx) {
    const auto cmpIt = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) {
        return t.type == TokenType::Operator && isComparator(t.text);
    });

    if (cmpIt == tokens.end()) {
        std::size_t pos = 0;
        return toBool(parseExpression(tokens, pos, ctx));
    }

    const std::size_t cmpPos = static_cast<std::size_t>(std::distance(tokens.begin(), cmpIt));
    TokenList left = tokenSlice(tokens, 0, cmpPos);
    TokenList right = tokenSlice(tokens, cmpPos + 1, tokens.size() - 1);

    std::size_t lp = 0;
    std::size_t rp = 0;
    Value lv = parseExpression(left, lp, ctx);
    Value rv = parseExpression(right, rp, ctx);

    const std::string& op = cmpIt->text;
    auto ln = toNumber(lv);
    auto rn = toNumber(rv);

    if (ln && rn) {
        if (op == "==") return *ln == *rn;
        if (op == "!=") return *ln != *rn;
        if (op == ">") return *ln > *rn;
        if (op == "<") return *ln < *rn;
        if (op == ">=") return *ln >= *rn;
        if (op == "<=") return *ln <= *rn;
    }

    const std::string ls = valueToString(lv);
    const std::string rs = valueToString(rv);
    if (op == "==") return ls == rs;
    if (op == "!=") return ls != rs;
    if (op == ">") return ls > rs;
    if (op == "<") return ls < rs;
    if (op == ">=") return ls >= rs;
    if (op == "<=") return ls <= rs;

    return false;
}

void Interpreter::executeSay(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() <= 2) {
        std::cout << "\n";
        return;
    }

    TokenList expr = tokenSlice(tokens, 2, tokens.size() - 1);
    std::size_t pos = 0;
    Value v = parseExpression(expr, pos, ctx);
    std::cout << interpolate(valueToString(v)) << '\n';
}

void Interpreter::executeSet(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 5 || tokens[2].type != TokenType::Identifier || tokens[3].text != "=") {
        fail(ctx, "Invalid set syntax");
    }
    TokenList expr = tokenSlice(tokens, 4, tokens.size() - 1);
    std::size_t pos = 0;
    env_.setVariable(tokens[2].text, parseExpression(expr, pos, ctx));
}

void Interpreter::executeCalc(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 5 || tokens[2].type != TokenType::Identifier || tokens[3].text != "=") {
        fail(ctx, "Invalid calc syntax");
    }
    TokenList expr = tokenSlice(tokens, 4, tokens.size() - 1);
    std::size_t pos = 0;
    env_.setVariable(tokens[2].text, parseExpression(expr, pos, ctx));
}

void Interpreter::executeIf(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 6) fail(ctx, "Invalid if syntax");

    std::size_t lParen = 0;
    bool found = false;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LParen) {
            lParen = i;
            found = true;
            break;
        }
    }
    if (!found) fail(ctx, "if requires action block in parentheses");

    const std::size_t rParen = findMatchingRParen(tokens, lParen, ctx);

    TokenList cond = tokenSlice(tokens, 2, lParen);
    if (!evaluateCondition(cond, ctx)) return;

    TokenList action = tokenSlice(tokens, lParen + 1, rParen);
    executeTokens(action, ctx);
}

void Interpreter::executeInclude(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 4 || tokens[2].type != TokenType::String) fail(ctx, "Include expects string filename");
    executeFile(tokens[2].text);
}

void Interpreter::executeList(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 6 || tokens[2].type != TokenType::Identifier || tokens[3].type != TokenType::LParen) {
        fail(ctx, "Invalid List syntax");
    }

    const std::size_t rParen = findMatchingRParen(tokens, 3, ctx);
    std::vector<std::string> items;
    for (std::size_t i = 4; i < rParen; ++i) {
        if (tokens[i].type == TokenType::Comma) continue;
        items.push_back(tokens[i].text);
    }
    env_.setList(tokens[2].text, std::move(items));
}

void Interpreter::executeLoop(const TokenList& tokens, const ErrorContext& ctx) {
    std::size_t lParen = 0;
    bool found = false;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LParen) {
            lParen = i;
            found = true;
            break;
        }
    }
    if (!found) fail(ctx, "Say to everyone requires action block");

    const std::size_t rParen = findMatchingRParen(tokens, lParen, ctx);
    TokenList actionTemplate = tokenSlice(tokens, lParen + 1, rParen);

    for (const auto& [_, items] : env_.allLists()) {
        for (const auto& item : items) {
            TokenList action = actionTemplate;
            for (Token& tk : action) {
                if (tk.type == TokenType::Identifier && tk.text == "item") {
                    tk.type = TokenType::String;
                    tk.text = item;
                }
            }
            executeTokens(action, ctx);
        }
    }
}

void Interpreter::executeCommandDefinition(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 6 || tokens[2].type != TokenType::Identifier || tokens[3].type != TokenType::LParen) {
        fail(ctx, "Invalid Command syntax");
    }

    const std::size_t rParen = findMatchingRParen(tokens, 3, ctx);
    TokenList body = tokenSlice(tokens, 4, rParen);
    env_.setCommand(tokens[2].text, body);
}

void Interpreter::executeRandom(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 4) fail(ctx, "random expects min and max");

    TokenList rangeExpr = tokenSlice(tokens, 2, tokens.size() - 1);
    std::size_t pos = 0;
    Value minVal = parseExpression(rangeExpr, pos, ctx);
    Value maxVal = parseExpression(rangeExpr, pos, ctx);

    auto mn = toNumber(minVal);
    auto mx = toNumber(maxVal);
    if (!mn || !mx) fail(ctx, "random range must be numeric");
    if (*mn > *mx) fail(ctx, "random min > max");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(static_cast<int>(*mn), static_cast<int>(*mx));
    env_.setVariable("random", static_cast<double>(dist(gen)));
}

void Interpreter::executeSolve(const TokenList& tokens, const ErrorContext& ctx) {
    if (tokens.size() < 4 || tokens[2].type != TokenType::String) fail(ctx, "solve expects string expression");
    TokenList exprTokens = lexer_.tokenize(tokens[2].text);
    std::size_t pos = 0;
    env_.setVariable("answer", parseExpression(exprTokens, pos, ctx));
}

void Interpreter::executeTokens(const TokenList& tokens, const ErrorContext& ctx) {
    registerHandlers();
    if (tokens.empty() || tokens[0].type == TokenType::End) return;

    if (tokens.size() >= 4 && tokens[0].type == TokenType::Keyword && tokens[0].text == "Say" &&
        tokens[1].type == TokenType::Keyword && tokens[1].text == "to" &&
        tokens[2].type == TokenType::Keyword && tokens[2].text == "everyone" &&
        tokens[3].type == TokenType::Colon) {
        executeLoop(tokens, ctx);
        return;
    }

    if (tokens.size() >= 3 && tokens[0].type == TokenType::Keyword && tokens[1].type == TokenType::Colon) {
        auto it = handlers_.find(tokens[0].text);
        if (it == handlers_.end()) fail(ctx, "Unknown command keyword: " + tokens[0].text);
        it->second(tokens, ctx);
        return;
    }

    if (tokens[0].type == TokenType::Identifier || tokens[0].type == TokenType::Keyword) {
        if (auto commandBody = env_.getCommand(tokens[0].text)) {
            executeTokens(*commandBody, ctx);
            return;
        }
    }

    fail(ctx, "Unknown command");
}

void Interpreter::executeLine(std::string_view line, ErrorContext context) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.rfind("//", 0) == 0) return;
    executeTokens(lexer_.tokenize(trimmed), context);
}

void Interpreter::executeFile(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) throw RuntimeError({0, filename}, "Could not open file");

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        executeLine(line, ErrorContext{lineNumber, filename});
    }
}

} // namespace Brise
