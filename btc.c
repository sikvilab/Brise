#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 2048
#define MAX_SYMBOLS 512
#define MAX_NAME 128
#define MAX_TASKS 128

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
    if (!eq) return buffer_append(out, "    /* BTC skipped invalid assignment */\n");
    *eq = 0;
    char* name = trim(tmp);
    char* expr = trim(eq + 1);
    if (!is_identifier_text(name)) return buffer_append(out, "    /* BTC skipped invalid variable name */\n");

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
    char tmp[MAX_LINE];
    snprintf(tmp, sizeof(tmp), "%s", rest);
    char* open = strchr(tmp, '(');
    char* close = open ? strchr(open, ')') : NULL;
    if (!open || !close) return buffer_append(out, "    /* BTC skipped invalid if block */\n");
    *open = 0;
    *close = 0;
    char* condition = trim(tmp);
    char* action = trim(open + 1);

    if (!buffer_appendf(out, "    if (", condition, ") {\n")) return 0;
    if (!emit_statement(c, out, action)) return 0;
    if (!buffer_append(out, "    }")) return 0;

    char* else_pos = find_else_ci(close + 1);
    if (else_pos) {
        char* else_open = strchr(else_pos, '(');
        char* else_close = else_open ? strrchr(else_open, ')') : NULL;
        if (else_open && else_close) {
            *else_close = 0;
            if (!buffer_append(out, " else {\n")) return 0;
            if (!emit_statement(c, out, trim(else_open + 1))) return 0;
            if (!buffer_append(out, "    }")) return 0;
        }
    }
    return buffer_append(out, "\n");
}

static int emit_task(Compiler* c, const char* rest) {
    char tmp[MAX_LINE];
    snprintf(tmp, sizeof(tmp), "%s", rest);
    char* open = strchr(tmp, '(');
    char* close = open ? strrchr(open, ')') : NULL;
    if (!open || !close) return buffer_append(&c->main_body, "    /* BTC skipped invalid task block */\n");
    *close = 0;

    int id = c->task_count++;
    c->uses_tasks = 1;
    char header[128];
    snprintf(header, sizeof(header), "static void* brise_task_%d(void* unused) {\n    (void)unused;\n", id);
    if (!buffer_append(&c->tasks, header)) return 0;
    if (!emit_statement(c, &c->tasks, trim(open + 1))) return 0;
    if (!buffer_append(&c->tasks, "    return NULL;\n}\n\n")) return 0;

    char call[256];
    snprintf(call, sizeof(call), "    pthread_t brise_task_thread_%d;\n    pthread_create(&brise_task_thread_%d, NULL, brise_task_%d, NULL);\n", id, id, id);
    return buffer_append(&c->main_body, call);
}

static int emit_statement(Compiler* c, Buffer* out, const char* src) {
    char line[MAX_LINE];
    snprintf(line, sizeof(line), "%s", src);
    char* s = trim(line);
    if (*s == 0) return 1;

    char* colon = strchr(s, ':');
    if (!colon) return buffer_append(out, "    /* BTC skipped unsupported line */\n");
    *colon = 0;
    char keyword[64];
    lower_copy(keyword, sizeof(keyword), trim(s));
    char* rest = trim(colon + 1);

    if (equals_ci(keyword, "say")) return emit_say(c, out, rest);
    if (equals_ci(keyword, "set") || equals_ci(keyword, "calc")) return emit_assignment(c, out, rest);
    if (equals_ci(keyword, "if")) return emit_if(c, out, rest);
    return buffer_append(out, "    /* BTC skipped unsupported command */\n");
}

static int compile_file(Compiler* c, const char* input_path, const char* c_path) {
    FILE* in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "BTC: cannot open %s\n", input_path);
        return 0;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        strip_comment(line);
        char copy[MAX_LINE];
        snprintf(copy, sizeof(copy), "%s", line);
        char* s = trim(copy);
        if (*s == 0) continue;
        if (starts_with_ci(s, "task:")) {
            if (!emit_task(c, s + 5)) { fclose(in); return 0; }
        } else if (!emit_statement(c, &c->main_body, s)) {
            fclose(in);
            return 0;
        }
    }
    fclose(in);

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
