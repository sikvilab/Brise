#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Brise {

using Value = std::variant<double, std::string, bool>;

struct ErrorContext {
    std::size_t line{0};
    std::string file;
};

class RuntimeError final : public std::runtime_error {
public:
    RuntimeError(ErrorContext context, std::string message);
    [[nodiscard]] const ErrorContext& context() const noexcept { return context_; }

private:
    ErrorContext context_;
};

enum class TokenType {
    Keyword,
    Identifier,
    String,
    Number,
    Operator,
    LParen,
    RParen,
    Comma,
    Colon,
    End,
};

struct Token {
    TokenType type{TokenType::End};
    std::string text;
};

class Lexer {
public:
    [[nodiscard]] std::vector<Token> tokenize(std::string_view line) const;
};

class Environment {
public:
    void setVariable(const std::string& name, Value value);
    [[nodiscard]] std::optional<Value> getVariable(std::string_view name) const;

    void setCommand(const std::string& name, const std::vector<Token>& body);
    [[nodiscard]] std::optional<std::vector<Token>> getCommand(std::string_view name) const;

    void setList(const std::string& name, std::vector<std::string> items);
    [[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>>& allLists() const noexcept;

private:
    std::unordered_map<std::string, Value> variables_;
    std::unordered_map<std::string, std::vector<Token>> commands_;
    std::unordered_map<std::string, std::vector<std::string>> lists_;
};

class Interpreter {
public:
    void executeFile(const std::string& filename);

private:
    using TokenList = std::vector<Token>;
    using Handler = std::function<void(const TokenList&, const ErrorContext&)>;

    Lexer lexer_;
    Environment env_;
    std::unordered_map<std::string, Handler> handlers_;

    void registerHandlers();

    void executeLine(std::string_view line, ErrorContext context);
    void executeTokens(const TokenList& tokens, const ErrorContext& ctx);

    void executeSay(const TokenList& tokens, const ErrorContext& ctx);
    void executeSet(const TokenList& tokens, const ErrorContext& ctx);
    void executeCalc(const TokenList& tokens, const ErrorContext& ctx);
    void executeIf(const TokenList& tokens, const ErrorContext& ctx);
    void executeInclude(const TokenList& tokens, const ErrorContext& ctx);
    void executeList(const TokenList& tokens, const ErrorContext& ctx);
    void executeLoop(const TokenList& tokens, const ErrorContext& ctx);
    void executeCommandDefinition(const TokenList& tokens, const ErrorContext& ctx);
    void executeRandom(const TokenList& tokens, const ErrorContext& ctx);
    void executeSolve(const TokenList& tokens, const ErrorContext& ctx);

    [[nodiscard]] std::string interpolate(std::string text) const;
    [[nodiscard]] std::string valueToString(const Value& value) const;
    [[nodiscard]] std::optional<double> toNumber(const Value& value) const;
    [[nodiscard]] bool toBool(const Value& value) const;

    Value parseExpression(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx);
    Value parseTerm(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx);
    Value parseFactor(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx);
    Value parsePrimary(const TokenList& tokens, std::size_t& pos, const ErrorContext& ctx);

    [[nodiscard]] bool evaluateCondition(const TokenList& tokens, const ErrorContext& ctx);
    [[nodiscard]] std::size_t findMatchingRParen(const TokenList& tokens, std::size_t lParenPos, const ErrorContext& ctx) const;
    [[nodiscard]] TokenList tokenSlice(const TokenList& tokens, std::size_t begin, std::size_t endExclusive) const;

    [[noreturn]] void fail(const ErrorContext& ctx, const std::string& message) const;
};

} // namespace Brise
