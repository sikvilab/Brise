#ifndef BRISE_RUNTIME_H
#define BRISE_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BRISE_VAL_NUMBER,
    BRISE_VAL_STRING,
    BRISE_VAL_BOOL
} BriseValueType;

typedef struct {
    BriseValueType type;
    double number_value;
    char* string_value;
    int bool_value;
} BriseValue;

typedef struct {
    size_t line;
    const char* file;
} BriseErrorContext;

typedef enum {
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_NUMBER,
    TOK_OPERATOR,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,
    TOK_END
} BriseTokenType;

typedef struct {
    BriseTokenType type;
    char* text;
} BriseToken;

typedef struct {
    BriseToken* items;
    size_t count;
    size_t capacity;
} BriseTokenArray;

typedef struct BriseRuntime BriseRuntime;

BriseRuntime* brise_runtime_create(void);
void brise_runtime_destroy(BriseRuntime* rt);

int brise_execute_file(BriseRuntime* rt, const char* filename);
const char* brise_last_error(const BriseRuntime* rt);

#ifdef __cplusplus
}
#endif

#endif
