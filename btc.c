#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 2048
#define MAX_SYMBOLS 512
#define MAX_NAME 128

typedef enum {
    SYM_NUMBER,
    SYM_STRING
} SymbolType;

typedef struct {
    char name[MAX_NAME];
    SymbolType type;
} Symbol;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Buffer;

typedef struct {
    Symbol symbols[MAX_SYMBOLS];
    size_t symbol_count;
    Buffer tasks;
    Buffer main_body;
    int task_count;
    int uses_tasks;
    const char* input_path;
} Compiler;

static char* xstrdup(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static int buffer_reserve(Buffer* b, size_t need) {
    if (need <= b->cap) return 1;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < need) nc *= 2;
    char* nd = (char*)realloc(b->data, nc);
    if (!nd) return 0;
    b->data = nd;
    b->cap = nc;
    return 1;
}

static int buffer_append(Buffer* b, const char* s) {
    size_t n = strlen(s);
    if (!buffer_reserve(b, b->len + n + 1)) return 0;
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
    return 1;
}

static int buffer_appendf(Buffer* b, const char* a, const char* c, const char* d) {
    return buffer_append(b, a) && buffer_append(b, c) && buffer_append(b, d);
}

static char* trim(char* s) {
    while (isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int starts_with_ci(const char* s, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int equals_ci(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void lower_copy(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;
    if (dst_size == 0) return;
    for (; src[i] && i + 1 < dst_size; ++i) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static int is_identifier_text(const char* s) {
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    s++;
    while (*s) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
        s++;
    }
    return 1;
}

static int is_string_literal(const char* s) {
    size_t n = strlen(s);
    return n >= 2 && s[0] == '"' && s[n - 1] == '"';
}

static Symbol* find_symbol(Compiler* c, const char* name) {
    for (size_t i = 0; i < c->symbol_count; ++i) {
        if (strcmp(c->symbols[i].name, name) == 0) return &c->symbols[i];
    }
    return NULL;
}

static int remember_symbol(Compiler* c, const char* name, SymbolType type) {
    Symbol* old = find_symbol(c, name);
    if (old) {
        old->type = type;
        return 1;
    }
    if (c->symbol_count >= MAX_SYMBOLS) return 0;
    snprintf(c->symbols[c->symbol_count].name, sizeof(c->symbols[c->symbol_count].name), "%s", name);
    c->symbols[c->symbol_count].type = type;
    c->symbol_count++;
    return 1;
}

static void strip_comment(char* line) {
    int in_string = 0;
    for (char* p = line; *p; ++p) {
        if (*p == '"') in_string = !in_string;
        if (!in_string && p[0] == '/' && p[1] == '/') {
            *p = 0;
            return;
        }
    }
}

static char* read_source_without_comments(const char* path) {
    FILE* in = fopen(path, "rb");
    if (!in) return NULL;

    Buffer source = {NULL, 0, 0};
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        strip_comment(line);
        if (!buffer_append(&source, line) || !buffer_append(&source, "\n")) {
            fclose(in);
            free(source.data);
            return NULL;
        }
    }
    fclose(in);
    return source.data ? source.data : xstrdup("");
}

static char* find_matching_paren(char* open) {
    int depth = 0;
    int in_string = 0;
    for (char* p = open; *p; ++p) {
        if (*p == '"') in_string = !in_string;
        if (in_string) continue;
        if (*p == '(') depth++;
        if (*p == ')') {
            depth--;
            if (depth == 0) return p;
        }
    }
    return NULL;
}

static char* find_first_paren(char* s) {
    int in_string = 0;
    for (char* p = s; *p; ++p) {
        if (*p == '"') in_string = !in_string;
        if (!in_string && *p == '(') return p;
    }
    return NULL;
}

static char* find_top_level_colon(char* s) {
    int in_string = 0;
    int depth = 0;
    for (char* p = s; *p; ++p) {
        if (*p == '"') in_string = !in_string;
        if (in_string) continue;
        if (*p == '(') depth++;
        else if (*p == ')' && depth > 0) depth--;
        else if (*p == ':' && depth == 0) return p;
    }
    return NULL;
}

static char* next_statement(char** cursor) {
    char* s = *cursor;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == 0) {
        *cursor = s;
        return NULL;
    }

    int in_string = 0;
    int depth = 0;
    char* start = s;
    for (; *s; ++s) {
        if (*s == '"') in_string = !in_string;
        if (!in_string) {
            if (*s == '(') depth++;
            else if (*s == ')' && depth > 0) depth--;
            else if (*s == '\n' && depth == 0) {
                char* lookahead = s + 1;
                while (*lookahead && isspace((unsigned char)*lookahead)) lookahead++;
                if (starts_with_ci(lookahead, "else:")) continue;
                *s = 0;
                *cursor = s + 1;
                return trim(start);
            }
        }
    }
    *cursor = s;
    return trim(start);
}

static int emit_block(Compiler* c, Buffer* out, const char* src);

static int emit_say(Compiler* c, Buffer* out, const char* expr) {
    char tmp[MAX_LINE];
    snprintf(tmp, sizeof(tmp), "%s", expr);
    char* value = trim(tmp);
    Symbol* sym = is_identifier_text(value) ? find_symbol(c, value) : NULL;

    if (sym && sym->type == SYM_STRING) {
        return buffer_appendf(out, "    printf(\"%s\\n\", ", value, ");\n");
    }
    if (sym && sym->type == SYM_NUMBER) {
        return buffer_appendf(out, "    printf(\"%g\\n\", ", value, ");\n");
    }
    if (is_string_literal(value)) {
        return buffer_appendf(out, "    printf(\"%s\\n\", ", value, ");\n");
    }
    return buffer_appendf(out, "    printf(\"%g\\n\", (double)(", value, "));\n");
}

static int emit_assignment(Compiler* c, Buffer* out, const char* rest) {
    char tmp[MAX_LINE];
    snprintf(tmp, sizeof(tmp), "%s", rest);
    char* eq = strchr(tmp, '=');
    if (!eq) { fprintf(stderr, "BTC: invalid assignment in %s\n", c->input_path ? c->input_path : "<input>"); return 0; }
    *eq = 0;
    char* name = trim(tmp);
    char* expr = trim(eq + 1);
    if (!is_identifier_text(name)) { fprintf(stderr, "BTC: invalid variable name in %s: %s\n", c->input_path ? c->input_path : "<input>", name); return 0; }

    Symbol* old = find_symbol(c, name);
    SymbolType type = is_string_literal(expr) ? SYM_STRING : SYM_NUMBER;
    if (!remember_symbol(c, name, type)) return 0;

    if (!old) {
        if (type == SYM_STRING) return buffer_appendf(out, "    const char* ", name, " = ") && buffer_append(out, expr) && buffer_append(out, ";\n");
        return buffer_appendf(out, "    double ", name, " = ") && buffer_append(out, expr) && buffer_append(out, ";\n");
    }
    return buffer_appendf(out, "    ", name, " = ") && buffer_append(out, expr) && buffer_append(out, ";\n");
}

static int emit_statement(Compiler* c, Buffer* out, const char* src);

static char* find_else_ci(char* s) {
    for (; *s; ++s) {
        if (tolower((unsigned char)s[0]) == 'e' &&
            tolower((unsigned char)s[1]) == 'l' &&
            tolower((unsigned char)s[2]) == 's' &&
            tolower((unsigned char)s[3]) == 'e' &&
            s[4] == ':') return s;
    }
    return NULL;
}

static int emit_if(Compiler* c, Buffer* out, const char* rest) {
    char* tmp = xstrdup(rest);
    if (!tmp) return 0;
    char* open = find_first_paren(tmp);
    char* close = open ? find_matching_paren(open) : NULL;
    if (!open || !close) {
        fprintf(stderr, "BTC: invalid if block in %s\n", c->input_path ? c->input_path : "<input>");
        free(tmp);
        return 0;
    }

    *open = 0;
    *close = 0;
    char* condition = trim(tmp);
    char* action = trim(open + 1);

    if (!buffer_appendf(out, "    if (", condition, ") {\n")) { free(tmp); return 0; }
    if (!emit_block(c, out, action)) { free(tmp); return 0; }
    if (!buffer_append(out, "    }")) { free(tmp); return 0; }

    char* else_pos = find_else_ci(close + 1);
    if (else_pos) {
        char* else_open = find_first_paren(else_pos);
        char* else_close = else_open ? find_matching_paren(else_open) : NULL;
        if (!else_open || !else_close) {
            fprintf(stderr, "BTC: invalid else block in %s\n", c->input_path ? c->input_path : "<input>");
            free(tmp);
            return 0;
        }
        *else_close = 0;
        if (!buffer_append(out, " else {\n")) { free(tmp); return 0; }
        if (!emit_block(c, out, trim(else_open + 1))) { free(tmp); return 0; }
        if (!buffer_append(out, "    }")) { free(tmp); return 0; }
    }
    int ok = buffer_append(out, "\n");
    free(tmp);
    return ok;
}

static int emit_task(Compiler* c, Buffer* out, const char* rest) {
    char* tmp = xstrdup(rest);
    if (!tmp) return 0;
    char* open = find_first_paren(tmp);
    char* close = open ? find_matching_paren(open) : NULL;
    if (!open || !close) {
        fprintf(stderr, "BTC: invalid task block in %s\n", c->input_path ? c->input_path : "<input>");
        free(tmp);
        return 0;
    }
    *close = 0;

    int id = c->task_count++;
    c->uses_tasks = 1;
    char header[128];
    snprintf(header, sizeof(header), "static void* brise_task_%d(void* unused) {\n    (void)unused;\n", id);
    if (!buffer_append(&c->tasks, header)) { free(tmp); return 0; }
    if (!emit_block(c, &c->tasks, trim(open + 1))) { free(tmp); return 0; }
    if (!buffer_append(&c->tasks, "    return NULL;\n}\n\n")) { free(tmp); return 0; }

    char call[256];
    snprintf(call, sizeof(call), "    pthread_t brise_task_thread_%d;\n    pthread_create(&brise_task_thread_%d, NULL, brise_task_%d, NULL);\n", id, id, id);
    int ok = buffer_append(out, call);
    free(tmp);
    return ok;
}

static int emit_statement(Compiler* c, Buffer* out, const char* src) {
    char* line = xstrdup(src);
    if (!line) return 0;
    char* s = trim(line);
    if (*s == 0) { free(line); return 1; }

    char* colon = find_top_level_colon(s);
    if (!colon) {
        fprintf(stderr, "BTC: unsupported syntax in %s: %s\n", c->input_path ? c->input_path : "<input>", s);
        free(line);
        return 0;
    }
    *colon = 0;
    char keyword[64];
    lower_copy(keyword, sizeof(keyword), trim(s));
    char* rest = trim(colon + 1);

    int ok = 0;
    if (equals_ci(keyword, "say")) ok = emit_say(c, out, rest);
    else if (equals_ci(keyword, "set") || equals_ci(keyword, "calc")) ok = emit_assignment(c, out, rest);
    else if (equals_ci(keyword, "if")) ok = emit_if(c, out, rest);
    else if (equals_ci(keyword, "task") || equals_ci(keyword, "async") || equals_ci(keyword, "run")) ok = emit_task(c, out, rest);
    else {
        fprintf(stderr, "BTC: unsupported command '%s' in %s (not silently skipped)\n", keyword, c->input_path ? c->input_path : "<input>");
        ok = 0;
    }

    free(line);
    return ok;
}

static int emit_block(Compiler* c, Buffer* out, const char* src) {
    char* copy = xstrdup(src);
    if (!copy) return 0;
    char* cursor = copy;
    char* stmt;
    while ((stmt = next_statement(&cursor)) != NULL) {
        if (*stmt == 0) continue;
        if (!emit_statement(c, out, stmt)) {
            free(copy);
            return 0;
        }
    }
    free(copy);
    return 1;
}

static int compile_file(Compiler* c, const char* input_path, const char* c_path) {
    c->input_path = input_path;
    char* source = read_source_without_comments(input_path);
    if (!source) {
        fprintf(stderr, "BTC: cannot open %s\n", input_path);
        return 0;
    }

    if (!emit_block(c, &c->main_body, source)) {
        free(source);
        return 0;
    }
    free(source);

    FILE* out = fopen(c_path, "wb");
    if (!out) {
        fprintf(stderr, "BTC: cannot write %s\n", c_path);
        return 0;
    }

    fprintf(out, "/* Generated by BTC (Brise To C). */\n");
    fprintf(out, "#include <stdio.h>\n");
    if (c->uses_tasks) fprintf(out, "#include <pthread.h>\n");
    fprintf(out, "\n");
    if (c->tasks.data) fputs(c->tasks.data, out);
    fprintf(out, "int main(void) {\n");
    if (c->main_body.data) fputs(c->main_body.data, out);
    for (int i = 0; i < c->task_count; ++i) {
        fprintf(out, "    pthread_join(brise_task_thread_%d, NULL);\n", i);
    }
    fprintf(out, "    return 0;\n}\n");
    fclose(out);
    return 1;
}

static char* replace_extension(const char* path, const char* ext) {
    char* out = xstrdup(path);
    if (!out) return NULL;
    char* slash = strrchr(out, '/');
    char* dot = strrchr(out, '.');
    if (dot && (!slash || dot > slash)) *dot = 0;
    size_t need = strlen(out) + strlen(ext) + 1;
    char* grown = (char*)realloc(out, need);
    if (!grown) { free(out); return NULL; }
    strcat(grown, ext);
    return grown;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("BTC (Brise To C) preview\n");
        printf("Usage: btc <file.bri> [output_binary] [--no-build]\n");
        return 0;
    }

    int no_build = 0;
    const char* output_binary = NULL;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--no-build") == 0) no_build = 1;
        else output_binary = argv[i];
    }

    char* c_path = replace_extension(argv[1], ".c");
    char* bin_path = output_binary ? xstrdup(output_binary) : replace_extension(argv[1], "");
    if (!c_path || !bin_path) {
        fprintf(stderr, "BTC: out of memory\n");
        free(c_path);
        free(bin_path);
        return 2;
    }

    Compiler c;
    memset(&c, 0, sizeof(c));
    if (!compile_file(&c, argv[1], c_path)) {
        free(c.tasks.data);
        free(c.main_body.data);
        free(c_path);
        free(bin_path);
        return 1;
    }

    printf("BTC: generated %s\n", c_path);
    if (!no_build) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "gcc \"%s\" -o \"%s\"%s", c_path, bin_path, c.uses_tasks ? " -pthread" : "");
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr, "BTC: build failed: %s\n", cmd);
            free(c.tasks.data);
            free(c.main_body.data);
            free(c_path);
            free(bin_path);
            return 1;
        }
        printf("BTC: built %s\n", bin_path);
    }

    free(c.tasks.data);
    free(c.main_body.data);
    free(c_path);
    free(bin_path);
    return 0;
}
