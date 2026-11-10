#include "brise_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <io.h>
#include <fcntl.h>
typedef SOCKET BriseSocket;
typedef int BriseIoSize;
#define BRISE_INVALID_SOCKET INVALID_SOCKET
#define brise_dlopen(name) ((void*)LoadLibraryA(name))
#define brise_dlsym(handle, name) ((void*)GetProcAddress((HMODULE)(handle), name))
#define brise_dlclose(handle) FreeLibrary((HMODULE)(handle))

typedef int (WSAAPI *BriseWSAStartupFn)(WORD, LPWSADATA);
typedef int (WSAAPI *BriseWSACleanupFn)(void);
typedef int (WSAAPI *BriseGetAddrInfoFn)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
typedef void (WSAAPI *BriseFreeAddrInfoFn)(PADDRINFOA);
typedef SOCKET (WSAAPI *BriseSocketFn)(int, int, int);
typedef int (WSAAPI *BriseConnectFn)(SOCKET, const struct sockaddr*, int);
typedef int (WSAAPI *BriseCloseSocketFn)(SOCKET);
typedef int (WSAAPI *BriseSendFn)(SOCKET, const char*, int, int);
typedef int (WSAAPI *BriseRecvFn)(SOCKET, char*, int, int);

static HMODULE brise_ws2_handle = NULL;
static BriseWSAStartupFn brise_wsa_startup_p = NULL;
static BriseWSACleanupFn brise_wsa_cleanup_p = NULL;
static BriseGetAddrInfoFn brise_getaddrinfo_p = NULL;
static BriseFreeAddrInfoFn brise_freeaddrinfo_p = NULL;
static BriseSocketFn brise_socket_p = NULL;
static BriseConnectFn brise_connect_p = NULL;
static BriseCloseSocketFn brise_close_socket_p = NULL;
static BriseSendFn brise_send_p = NULL;
static BriseRecvFn brise_recv_p = NULL;

static int brise_load_winsock(void) {
    if (brise_ws2_handle) return 1;
    brise_ws2_handle = LoadLibraryA("ws2_32.dll");
    if (!brise_ws2_handle) return 0;
    brise_wsa_startup_p = (BriseWSAStartupFn)GetProcAddress(brise_ws2_handle, "WSAStartup");
    brise_wsa_cleanup_p = (BriseWSACleanupFn)GetProcAddress(brise_ws2_handle, "WSACleanup");
    brise_getaddrinfo_p = (BriseGetAddrInfoFn)GetProcAddress(brise_ws2_handle, "getaddrinfo");
    brise_freeaddrinfo_p = (BriseFreeAddrInfoFn)GetProcAddress(brise_ws2_handle, "freeaddrinfo");
    brise_socket_p = (BriseSocketFn)GetProcAddress(brise_ws2_handle, "socket");
    brise_connect_p = (BriseConnectFn)GetProcAddress(brise_ws2_handle, "connect");
    brise_close_socket_p = (BriseCloseSocketFn)GetProcAddress(brise_ws2_handle, "closesocket");
    brise_send_p = (BriseSendFn)GetProcAddress(brise_ws2_handle, "send");
    brise_recv_p = (BriseRecvFn)GetProcAddress(brise_ws2_handle, "recv");
    return brise_wsa_startup_p && brise_wsa_cleanup_p && brise_getaddrinfo_p && brise_freeaddrinfo_p &&
           brise_socket_p && brise_connect_p && brise_close_socket_p && brise_send_p && brise_recv_p;
}
#define brise_wsa_startup(version, data) brise_wsa_startup_p((version), (data))
#define brise_wsa_cleanup() brise_wsa_cleanup_p()
#define brise_getaddrinfo(node, service, hints, res) brise_getaddrinfo_p((node), (service), (hints), (res))
#define brise_freeaddrinfo(res) brise_freeaddrinfo_p((res))
#define brise_socket(af, type, protocol) brise_socket_p((af), (type), (protocol))
#define brise_connect(sock, addr, len) brise_connect_p((sock), (addr), (len))
#define brise_close_socket(sock) brise_close_socket_p((sock))
#define brise_send(sock, buf, len, flags) brise_send_p((sock), (buf), (len), (flags))
#define brise_recv(sock, buf, len, flags) brise_recv_p((sock), (buf), (len), (flags))
#else
#include <dlfcn.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int BriseSocket;
typedef ssize_t BriseIoSize;
#define BRISE_INVALID_SOCKET (-1)
#define brise_close_socket close
#define brise_dlopen(name) dlopen((name), RTLD_NOW)
#define brise_dlsym(handle, name) dlsym((handle), (name))
#define brise_dlclose(handle) dlclose(handle)
#define brise_getaddrinfo getaddrinfo
#define brise_freeaddrinfo freeaddrinfo
#define brise_socket socket
#define brise_connect connect
#define brise_send send
#define brise_recv recv
#endif

#define MAX_ERROR_LEN 512
#define BRISE_MAX_FILE_BYTES (4u * 1024u * 1024u)
#define BRISE_MAX_NETWE_BYTES (4u * 1024u * 1024u)
/* Matrix and manual-memory allocations share a bounded pool of double cells. */
#define BRISE_MAX_MANAGED_CELLS (1024u * 1024u)

typedef struct {
    char* name;
    size_t rows;
    size_t cols;
    double* cells;
} MatrixEntry;

typedef struct {
    char* name;
    size_t count;
    double* cells;
} MemoryEntry;

typedef struct {
    char* key;
    BriseValue value;
} VarEntry;

typedef struct {
    char* name;
    BriseTokenArray body;
} CommandEntry;

typedef struct {
    char* name;
    char** items;
    size_t count;
    size_t capacity;
} ListEntry;

typedef struct {
    size_t line_number;
    BriseTokenArray tokens;
} CachedLine;

typedef struct {
    char* path;
    CachedLine* lines;
    size_t count;
    size_t capacity;
} FileCacheEntry;

typedef int (*BriseSDLSetRenderDrawColorFn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
typedef int (*BriseSDLRenderFillRectFn)(void*, const void*);

typedef struct {
    int x;
    int y;
    int w;
    int h;
} BriseGraphRect;

static const unsigned char* brise_graph_glyph(char c) {
    static const unsigned char unknown[7] = {14,17,1,2,4,0,4};
    static const unsigned char space[7] = {0,0,0,0,0,0,0};
    static const unsigned char glyphs[36][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
    };
    static const unsigned char colon[7] = {0,4,4,0,4,4,0};
    static const unsigned char dash[7] = {0,0,0,31,0,0,0};
    static const unsigned char dot[7] = {0,0,0,0,0,4,4};
    static const unsigned char bang[7] = {4,4,4,4,4,0,4};
    static const unsigned char question[7] = {14,17,1,2,4,0,4};
    if (c == ' ') return space;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyphs[c - 'A'];
    if (c >= '0' && c <= '9') return glyphs[26 + (c - '0')];
    if (c == ':') return colon;
    if (c == '-') return dash;
    if (c == '.') return dot;
    if (c == '!') return bang;
    if (c == '?') return question;
    return unknown;
}

static void brise_graph_draw_text(void* renderer, BriseSDLSetRenderDrawColorFn set_color, BriseSDLRenderFillRectFn fill_rect,
                                  int x, int y, const char* text, unsigned char r, unsigned char g, unsigned char b) {
    const int scale = 2;
    set_color(renderer, r, g, b, 255);
    for (size_t i = 0; text && text[i]; ++i) {
        const unsigned char* glyph = brise_graph_glyph(text[i]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (glyph[row] & (1u << (4 - col))) {
                    BriseGraphRect px = {x + (int)i * 12 + col * scale, y + row * scale, scale, scale};
                    fill_rect(renderer, &px);
                }
            }
        }
    }
}

struct BriseRuntime {
    VarEntry* vars;
    size_t vars_count;
    size_t vars_capacity;

    CommandEntry* commands;
    size_t commands_count;
    size_t commands_capacity;

    ListEntry* lists;
    size_t lists_count;
    size_t lists_capacity;

    MatrixEntry* matrices;
    size_t matrices_count;
    size_t matrices_capacity;

    MemoryEntry* memory;
    size_t memory_count;
    size_t memory_capacity;
    size_t managed_cells;

    FileCacheEntry* cache;
    size_t cache_count;
    size_t cache_capacity;

    char* graph_title;
    char* graph_elements;
    int graph_enabled;

    char last_error[MAX_ERROR_LEN];
};

static MatrixEntry* find_matrix(BriseRuntime* rt, const char* name);
static MemoryEntry* find_memory(BriseRuntime* rt, const char* name);
static int matrix_det_value(BriseRuntime* rt, BriseErrorContext ctx, const MatrixEntry* matrix, double* result);

static char* xstrdup(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static char* xstrndup(const char* s, size_t n) {
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

static void set_error(BriseRuntime* rt, BriseErrorContext ctx, const char* message) {
    snprintf(rt->last_error, sizeof(rt->last_error), "[Error at line %zu in %s]: %s", ctx.line, ctx.file ? ctx.file : "<unknown>", message);
}

static int brise_streq_ci(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void free_value(BriseValue* v) {
    if (v->type == BRISE_VAL_STRING && v->string_value) free(v->string_value);
    v->string_value = NULL;
}

static BriseValue value_number(double n) {
    BriseValue v;
    v.type = BRISE_VAL_NUMBER;
    v.number_value = n;
    v.string_value = NULL;
    v.bool_value = 0;
    return v;
}

static BriseValue value_bool(int b) {
    BriseValue v;
    v.type = BRISE_VAL_BOOL;
    v.number_value = 0.0;
    v.string_value = NULL;
    v.bool_value = b ? 1 : 0;
    return v;
}

static BriseValue value_string(const char* s) {
    BriseValue v;
    v.type = BRISE_VAL_STRING;
    v.number_value = 0.0;
    v.bool_value = 0;
    v.string_value = xstrdup(s ? s : "");
    return v;
}

static BriseValue clone_value(const BriseValue* src) {
    if (src->type == BRISE_VAL_NUMBER) return value_number(src->number_value);
    if (src->type == BRISE_VAL_BOOL) return value_bool(src->bool_value);
    return value_string(src->string_value ? src->string_value : "");
}

static int value_to_number(const BriseValue* v, double* out) {
    if (v->type == BRISE_VAL_NUMBER) {
        *out = v->number_value;
        return 1;
    }
    if (v->type == BRISE_VAL_BOOL) {
        *out = v->bool_value ? 1.0 : 0.0;
        return 1;
    }
    if (v->type == BRISE_VAL_STRING) {
        char* end = NULL;
        double d = strtod(v->string_value ? v->string_value : "", &end);
        if (end && *end == '\0') {
            *out = d;
            return 1;
        }
    }
    return 0;
}

static int value_to_bool(const BriseValue* v) {
    if (v->type == BRISE_VAL_BOOL) return v->bool_value;
    if (v->type == BRISE_VAL_NUMBER) return v->number_value != 0.0;
    return v->string_value && v->string_value[0] != '\0';
}

static char* value_to_string_heap(const BriseValue* v) {
    char buf[128];
    if (v->type == BRISE_VAL_STRING) return xstrdup(v->string_value ? v->string_value : "");
    if (v->type == BRISE_VAL_BOOL) return xstrdup(v->bool_value ? "true" : "false");
    snprintf(buf, sizeof(buf), "%.15g", v->number_value);
    return xstrdup(buf);
}

static int ensure_capacity(void** data, size_t* cap, size_t elem_size, size_t need) {
    if (*cap >= need) return 1;
    size_t ncap = (*cap == 0) ? 8 : (*cap * 2);
    while (ncap < need) ncap *= 2;
    void* nd = realloc(*data, ncap * elem_size);
    if (!nd) return 0;
    *data = nd;
    *cap = ncap;
    return 1;
}

static VarEntry* find_var(BriseRuntime* rt, const char* key) {
    for (size_t i = 0; i < rt->vars_count; ++i) {
        if (strcmp(rt->vars[i].key, key) == 0) return &rt->vars[i];
    }
    return NULL;
}

static const VarEntry* find_var_const(const BriseRuntime* rt, const char* key) {
    for (size_t i = 0; i < rt->vars_count; ++i) {
        if (strcmp(rt->vars[i].key, key) == 0) return &rt->vars[i];
    }
    return NULL;
}

static int set_var(BriseRuntime* rt, const char* key, BriseValue v) {
    VarEntry* e = find_var(rt, key);
    if (e) {
        free_value(&e->value);
        e->value = v;
        return 1;
    }
    if (!ensure_capacity((void**)&rt->vars, &rt->vars_capacity, sizeof(VarEntry), rt->vars_count + 1)) return 0;
    rt->vars[rt->vars_count].key = xstrdup(key);
    rt->vars[rt->vars_count].value = v;
    rt->vars_count++;
    return 1;
}

static void token_array_free(BriseTokenArray* arr) {
    for (size_t i = 0; i < arr->count; ++i) free(arr->items[i].text);
    free(arr->items);
    arr->items = NULL;
    arr->count = arr->capacity = 0;
}

static int token_push(BriseTokenArray* arr, BriseTokenType type, const char* text) {
    if (!ensure_capacity((void**)&arr->items, &arr->capacity, sizeof(BriseToken), arr->count + 1)) return 0;
    arr->items[arr->count].type = type;
    arr->items[arr->count].text = xstrdup(text ? text : "");
    arr->count++;
    return 1;
}

static int is_keyword(const char* w) {
    const char* k[] = {"say","set","calc","if","else","while","assert","and","or","not","Include","Call","List","Command","Graph","Netwe","random","solve","query","read","write","Say","to","everyone","Matrix","Memory","free","true","false"};
    size_t n = sizeof(k)/sizeof(k[0]);
    for (size_t i=0;i<n;++i) if (brise_streq_ci(k[i], w)) return 1;
    return 0;
}

static BriseTokenArray tokenize_line(const char* line) {
    BriseTokenArray out = {NULL, 0, 0};
    size_t i = 0;
    size_t n = strlen(line);
    while (i < n) {
        char c = line[i];
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c=='/' && i+1<n && line[i+1]=='/') break;
        if (isalpha((unsigned char)c) || c=='_') {
            size_t s=i;
            while (i<n && (isalnum((unsigned char)line[i]) || line[i]=='_')) i++;
            char* t = xstrndup(line+s, i-s);
            token_push(&out, is_keyword(t)?TOK_KEYWORD:TOK_IDENTIFIER, t);
            free(t);
            continue;
        }
        if (isdigit((unsigned char)c) || c=='.') {
            char* end=NULL;
            errno=0;
            (void)strtod(line+i,&end);
            if (end==line+i) { token_push(&out,TOK_OPERATOR,"?"); i++; continue; }
            size_t length=(size_t)(end-(line+i));
            char* number=xstrndup(line+i,length);
            if (!number || errno==ERANGE || !isfinite(strtod(number?number:"0",NULL))) {
                free(number); token_push(&out,TOK_OPERATOR,"?"); return out;
            }
            token_push(&out,TOK_NUMBER,number);
            free(number);
            i+=length;
            continue;
        }
        if (c=='"') {
            i++;
            size_t s=i;
            while (i<n && line[i] != '"') i++;
            char* t = xstrndup(line+s, i-s);
            token_push(&out, TOK_STRING, t);
            free(t);
            if (i<n) i++;
            continue;
        }
        if (c=='(') { token_push(&out,TOK_LPAREN,"("); i++; continue; }
        if (c==')') { token_push(&out,TOK_RPAREN,")"); i++; continue; }
        if (c==',') { token_push(&out,TOK_COMMA,","); i++; continue; }
        if (c==':') { token_push(&out,TOK_COLON,":"); i++; continue; }
        if (strchr("=!<>", c)) {
            if (i+1<n && line[i+1]=='=') {
                char op[3] = {c,'=',0}; token_push(&out,TOK_OPERATOR,op); i+=2;
            } else {
                char op[2] = {c,0}; token_push(&out,TOK_OPERATOR,op); i++;
            }
            continue;
        }
        if (strchr("+-*/%^", c)) { char op[2]={c,0}; token_push(&out,TOK_OPERATOR,op); i++; continue; }
        i++;
    }
    token_push(&out, TOK_END, "");
    return out;
}

static char* trim_copy(const char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n>0 && isspace((unsigned char)s[n-1])) n--;
    char* out = (char*)malloc(n+1);
    memcpy(out,s,n); out[n]=0;
    return out;
}

static char* interpolate(BriseRuntime* rt, const char* src) {
    size_t cap = strlen(src) + 32;
    char* out = (char*)malloc(cap);
    size_t w = 0;
    for (size_t i = 0; src[i]; ) {
        if (src[i] == '(') {
            const char* end = strchr(src + i, ')');
            if (end) {
                size_t len = (size_t)(end - (src + i + 1));
                char* name = xstrndup(src + i + 1, len);
                const VarEntry* ve = find_var_const(rt, name);
                free(name);
                if (ve) {
                    char* rep = value_to_string_heap(&ve->value);
                    size_t rl = strlen(rep);
                    if (w + rl + 1 > cap) { cap = (w + rl + 64) * 2; out = (char*)realloc(out, cap); }
                    memcpy(out + w, rep, rl); w += rl; free(rep);
                    i = (size_t)(end - src) + 1;
                    continue;
                }
            }
        }
        if (w + 2 > cap) { cap *= 2; out = (char*)realloc(out, cap); }
        out[w++] = src[i++];
    }
    out[w] = 0;
    return out;
}

static int parse_expression(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out);

static int builtin_arity(BriseRuntime* rt, BriseErrorContext ctx, const char* name, size_t got, size_t expected) {
    if (got == expected) return 1;
    char message[160];
    snprintf(message, sizeof(message), "%s expects %zu argument(s)", name, expected);
    set_error(rt, ctx, message);
    return 0;
}

static char* ascii_transform(const char* input, int upper) {
    size_t n = strlen(input);
    char* result = (char*)malloc(n + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)input[i];
        result[i] = (char)(upper ? toupper(c) : tolower(c));
    }
    result[n] = '\0';
    return result;
}

static char* trim_string_copy(const char* input) {
    const char* begin = input;
    while (*begin && isspace((unsigned char)*begin)) begin++;
    const char* end = input + strlen(input);
    while (end > begin && isspace((unsigned char)end[-1])) end--;
    return xstrndup(begin, (size_t)(end - begin));
}

static int values_equal(const BriseValue* a, const BriseValue* b) {
    double an = 0.0, bn = 0.0;
    if (value_to_number(a, &an) && value_to_number(b, &bn)) return an == bn;
    char* as = value_to_string_heap(a);
    char* bs = value_to_string_heap(b);
    if (!as || !bs) { free(as); free(bs); return 0; }
    int equal = strcmp(as, bs) == 0;
    free(as); free(bs);
    return equal;
}

static int evaluate_builtin(BriseRuntime* rt, BriseErrorContext ctx, const char* name,
                            BriseValue* args, size_t argc, BriseValue* out) {
    double a=0.0,b=0.0,c=0.0,result=0.0;
    char* s=NULL;
    char* t=NULL;
    if (brise_streq_ci(name,"mem_get")) {
        if (!builtin_arity(rt,ctx,name,argc,2) || args[0].type!=BRISE_VAL_STRING ||
            !value_to_number(&args[1],&a) || !isfinite(a) || floor(a)!=a || a<0) {
            set_error(rt,ctx,"mem_get expects a string memory-block name and a non-negative integer index");
            return 0;
        }
        MemoryEntry* block=find_memory(rt,args[0].string_value);
        if (!block) { set_error(rt,ctx,"Memory block does not exist or was freed"); return 0; }
        if (a>=(double)block->count) { set_error(rt,ctx,"Memory cell index is out of range"); return 0; }
        *out=value_number(block->cells[(size_t)a]); return 1;
    }
    if (brise_streq_ci(name,"mget") || brise_streq_ci(name,"matrix_get")) {
        if (!builtin_arity(rt,ctx,name,argc,3) || args[0].type!=BRISE_VAL_STRING ||
            !value_to_number(&args[1],&a) || !value_to_number(&args[2],&b) ||
            !isfinite(a) || !isfinite(b) || floor(a)!=a || floor(b)!=b || a<0 || b<0) {
            set_error(rt,ctx,"mget expects a string matrix name and non-negative integer row and column");
            return 0;
        }
        MatrixEntry* matrix=find_matrix(rt,args[0].string_value);
        if (!matrix) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        if (a>=(double)matrix->rows || b>=(double)matrix->cols) {
            set_error(rt,ctx,"Matrix index is out of range (indexes are zero-based)"); return 0;
        }
        *out=value_number(matrix->cells[(size_t)a*matrix->cols+(size_t)b]); return 1;
    }
    if (brise_streq_ci(name,"mrows") || brise_streq_ci(name,"mcols") ||
        brise_streq_ci(name,"mdet") || brise_streq_ci(name,"mexists") ||
        brise_streq_ci(name,"matrix_exists")) {
        if (!builtin_arity(rt,ctx,name,argc,1) || args[0].type!=BRISE_VAL_STRING) {
            set_error(rt,ctx,"Matrix query expects one string matrix name"); return 0;
        }
        MatrixEntry* matrix=find_matrix(rt,args[0].string_value);
        if (brise_streq_ci(name,"mexists") || brise_streq_ci(name,"matrix_exists")) {
            *out=value_bool(matrix!=NULL); return 1;
        }
        if (!matrix) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        if (brise_streq_ci(name,"mrows")) { *out=value_number((double)matrix->rows); return 1; }
        if (brise_streq_ci(name,"mcols")) { *out=value_number((double)matrix->cols); return 1; }
        if (!matrix_det_value(rt,ctx,matrix,&result)) return 0;
        *out=value_number(result); return 1;
    }
    if (brise_streq_ci(name,"mem_size") || brise_streq_ci(name,"mem_exists")) {
        if (!builtin_arity(rt,ctx,name,argc,1) || args[0].type!=BRISE_VAL_STRING) {
            set_error(rt,ctx,"Memory query expects one string block name"); return 0;
        }
        MemoryEntry* block=find_memory(rt,args[0].string_value);
        if (brise_streq_ci(name,"mem_exists")) { *out=value_bool(block!=NULL); return 1; }
        if (!block) { set_error(rt,ctx,"Memory block does not exist or was freed"); return 0; }
        *out=value_number((double)block->count); return 1;
    }
    if (brise_streq_ci(name,"len") || brise_streq_ci(name,"str") ||
        brise_streq_ci(name,"type") || brise_streq_ci(name,"upper") ||
        brise_streq_ci(name,"lower") || brise_streq_ci(name,"trim") ||
        brise_streq_ci(name,"to_number")) {
        if (!builtin_arity(rt,ctx,name,argc,1)) return 0;
        if (brise_streq_ci(name,"type")) {
            *out=value_string(args[0].type==BRISE_VAL_STRING?"string":
                args[0].type==BRISE_VAL_NUMBER?"number":"boolean");
            return out->string_value!=NULL;
        }
        if (brise_streq_ci(name,"to_number")) {
            if (!value_to_number(&args[0],&a) || !isfinite(a)) {
                set_error(rt,ctx,"to_number cannot convert value to a finite number"); return 0;
            }
            *out=value_number(a); return 1;
        }
        s=value_to_string_heap(&args[0]);
        if (!s) { set_error(rt,ctx,"Out of memory"); return 0; }
        if (brise_streq_ci(name,"len")) { *out=value_number((double)strlen(s)); free(s); return 1; }
        if (brise_streq_ci(name,"str")) {
            *out=value_string(s); free(s);
            if (!out->string_value) set_error(rt,ctx,"Out of memory");
            return out->string_value!=NULL;
        }
        if (brise_streq_ci(name,"upper") || brise_streq_ci(name,"lower")) {
            t=ascii_transform(s,brise_streq_ci(name,"upper")); free(s);
        } else { t=trim_string_copy(s); free(s); }
        if (!t) { set_error(rt,ctx,"Out of memory"); return 0; }
        *out=value_string(t); free(t);
        if (!out->string_value) set_error(rt,ctx,"Out of memory");
        return out->string_value!=NULL;
    }
    if (brise_streq_ci(name,"contains") || brise_streq_ci(name,"starts_with") ||
        brise_streq_ci(name,"ends_with")) {
        if (!builtin_arity(rt,ctx,name,argc,2)) return 0;
        s=value_to_string_heap(&args[0]); t=value_to_string_heap(&args[1]);
        if (!s || !t) { free(s); free(t); set_error(rt,ctx,"Out of memory"); return 0; }
        int found=0;
        if (brise_streq_ci(name,"contains")) found=strstr(s,t)!=NULL;
        else if (brise_streq_ci(name,"starts_with")) found=strncmp(s,t,strlen(t))==0;
        else { size_t sl=strlen(s),tl=strlen(t); found=tl<=sl && strcmp(s+sl-tl,t)==0; }
        free(s); free(t); *out=value_bool(found); return 1;
    }
    if (brise_streq_ci(name,"assert_eq")) {
        if (!builtin_arity(rt,ctx,name,argc,2)) return 0;
        *out=value_bool(values_equal(&args[0],&args[1])); return 1;
    }
    if (brise_streq_ci(name,"is_finite") || brise_streq_ci(name,"is_nan")) {
        if (!builtin_arity(rt,ctx,name,argc,1) || !value_to_number(&args[0],&a)) {
            set_error(rt,ctx,"Math predicate expects one numeric argument"); return 0;
        }
        *out=value_bool(brise_streq_ci(name,"is_finite")?isfinite(a):isnan(a)); return 1;
    }
    if (brise_streq_ci(name,"abs") || brise_streq_ci(name,"sqrt") ||
        brise_streq_ci(name,"floor") || brise_streq_ci(name,"ceil") ||
        brise_streq_ci(name,"round") || brise_streq_ci(name,"trunc") ||
        brise_streq_ci(name,"sin") || brise_streq_ci(name,"cos") ||
        brise_streq_ci(name,"tan") || brise_streq_ci(name,"asin") ||
        brise_streq_ci(name,"acos") || brise_streq_ci(name,"atan") ||
        brise_streq_ci(name,"sinh") || brise_streq_ci(name,"cosh") ||
        brise_streq_ci(name,"tanh") || brise_streq_ci(name,"exp") ||
        brise_streq_ci(name,"log") || brise_streq_ci(name,"ln") ||
        brise_streq_ci(name,"log10") || brise_streq_ci(name,"log2") ||
        brise_streq_ci(name,"cbrt") || brise_streq_ci(name,"sign") ||
        brise_streq_ci(name,"deg_to_rad") || brise_streq_ci(name,"rad_to_deg") ||
        brise_streq_ci(name,"factorial") || brise_streq_ci(name,"is_prime")) {
        if (!builtin_arity(rt,ctx,name,argc,1) || !value_to_number(&args[0],&a)) {
            set_error(rt,ctx,"Math function expects one numeric argument"); return 0;
        }
        if (brise_streq_ci(name,"is_prime")) {
            if (!isfinite(a) || floor(a)!=a || a>1000000000000.0) {
                set_error(rt,ctx,"is_prime expects an integer between 0 and 1e12"); return 0;
            }
            if (a<2.0) { *out=value_bool(0); return 1; }
            unsigned long long n=(unsigned long long)a;
            int prime=1;
            for (unsigned long long d=2;d<=n/d;++d) if (n%d==0) { prime=0; break; }
            *out=value_bool(prime); return 1;
        }
        if (brise_streq_ci(name,"factorial")) {
            if (!isfinite(a) || floor(a)!=a || a<0.0 || a>170.0) {
                set_error(rt,ctx,"factorial expects an integer from 0 through 170"); return 0;
            }
            result=1.0;
            for (int i=2;i<=(int)a;++i) result*=(double)i;
        } else if (brise_streq_ci(name,"sqrt")) {
            if (a<0.0) { set_error(rt,ctx,"sqrt expects a non-negative number"); return 0; }
            result=sqrt(a);
        } else if (brise_streq_ci(name,"abs")) result=fabs(a);
        else if (brise_streq_ci(name,"floor")) result=floor(a);
        else if (brise_streq_ci(name,"ceil")) result=ceil(a);
        else if (brise_streq_ci(name,"round")) result=round(a);
        else if (brise_streq_ci(name,"trunc")) result=trunc(a);
        else if (brise_streq_ci(name,"sin")) result=sin(a);
        else if (brise_streq_ci(name,"cos")) result=cos(a);
        else if (brise_streq_ci(name,"tan")) result=tan(a);
        else if (brise_streq_ci(name,"asin")) {
            if (a < -1.0 || a > 1.0) { set_error(rt,ctx,"asin expects a value from -1 through 1"); return 0; }
            result=asin(a);
        } else if (brise_streq_ci(name,"acos")) {
            if (a < -1.0 || a > 1.0) { set_error(rt,ctx,"acos expects a value from -1 through 1"); return 0; }
            result=acos(a);
        } else if (brise_streq_ci(name,"atan")) result=atan(a);
        else if (brise_streq_ci(name,"sinh")) result=sinh(a);
        else if (brise_streq_ci(name,"cosh")) result=cosh(a);
        else if (brise_streq_ci(name,"tanh")) result=tanh(a);
        else if (brise_streq_ci(name,"exp")) result=exp(a);
        else if (brise_streq_ci(name,"log") || brise_streq_ci(name,"ln")) {
            if (a<=0.0) { set_error(rt,ctx,"log expects a positive number"); return 0; }
            result=log(a);
        } else if (brise_streq_ci(name,"log10")) {
            if (a<=0.0) { set_error(rt,ctx,"log10 expects a positive number"); return 0; }
            result=log10(a);
        } else if (brise_streq_ci(name,"log2")) {
            if (a<=0.0) { set_error(rt,ctx,"log2 expects a positive number"); return 0; }
            result=log2(a);
        } else if (brise_streq_ci(name,"cbrt")) result=cbrt(a);
        else if (brise_streq_ci(name,"sign")) result=a>0.0?1.0:(a<0.0?-1.0:0.0);
        else if (brise_streq_ci(name,"deg_to_rad")) result=a*(3.14159265358979323846/180.0);
        else if (brise_streq_ci(name,"rad_to_deg")) result=a*(180.0/3.14159265358979323846);
        if (!isfinite(result)) { set_error(rt,ctx,"Math result is not finite"); return 0; }
        *out=value_number(result); return 1;
    }
    if (brise_streq_ci(name,"min") || brise_streq_ci(name,"max") ||
        brise_streq_ci(name,"pow") || brise_streq_ci(name,"mod") ||
        brise_streq_ci(name,"hypot") || brise_streq_ci(name,"atan2") ||
        brise_streq_ci(name,"gcd") || brise_streq_ci(name,"lcm")) {
        if (!builtin_arity(rt,ctx,name,argc,2) ||
            !value_to_number(&args[0],&a) || !value_to_number(&args[1],&b)) {
            set_error(rt,ctx,"Math function expects two numeric arguments"); return 0;
        }
        if (brise_streq_ci(name,"min")) result=a<b?a:b;
        else if (brise_streq_ci(name,"max")) result=a>b?a:b;
        else if (brise_streq_ci(name,"hypot")) result=hypot(a,b);
        else if (brise_streq_ci(name,"atan2")) result=atan2(a,b);
        else if (brise_streq_ci(name,"pow")) result=pow(a,b);
        else if (brise_streq_ci(name,"mod")) {
            if (b==0.0) { set_error(rt,ctx,"mod divisor cannot be zero"); return 0; }
            result=fmod(a,b);
        } else {
            const double safe_integer=9007199254740991.0;
            if (!isfinite(a) || !isfinite(b) || floor(a)!=a || floor(b)!=b ||
                fabs(a)>safe_integer || fabs(b)>safe_integer) {
                set_error(rt,ctx,"gcd/lcm expect finite safe integers"); return 0;
            }
            unsigned long long ua=(unsigned long long)fabs(a),ub=(unsigned long long)fabs(b);
            unsigned long long x=ua,y=ub;
            while (y) { unsigned long long rem=x%y; x=y; y=rem; }
            if (brise_streq_ci(name,"gcd")) result=(double)x;
            else if (x==0) result=0.0;
            else {
                unsigned long long divided=ua/x;
                if (ub && divided>ULLONG_MAX/ub) {
                    set_error(rt,ctx,"lcm result exceeds supported integer range"); return 0;
                }
                result=(double)(divided*ub);
            }
        }
        if (!isfinite(result)) { set_error(rt,ctx,"Math result is not finite"); return 0; }
        *out=value_number(result); return 1;
    }
    if (brise_streq_ci(name,"clamp") || brise_streq_ci(name,"lerp")) {
        if (!builtin_arity(rt,ctx,name,argc,3) || !value_to_number(&args[0],&a) ||
            !value_to_number(&args[1],&b) || !value_to_number(&args[2],&c)) {
            set_error(rt,ctx,"Math function expects three numeric arguments"); return 0;
        }
        if (brise_streq_ci(name,"clamp")) {
            if (b>c) { set_error(rt,ctx,"clamp minimum cannot exceed maximum"); return 0; }
            result=a<b?b:(a>c?c:a);
        } else result=a+(b-a)*c;
        if (!isfinite(result)) { set_error(rt,ctx,"Math result is not finite"); return 0; }
        *out=value_number(result); return 1;
    }
    if (brise_streq_ci(name,"round_to")) {
        if (!builtin_arity(rt,ctx,name,argc,2) || !value_to_number(&args[0],&a) ||
            !value_to_number(&args[1],&b) || !isfinite(b) || floor(b)!=b || b < -12.0 || b > 12.0) {
            set_error(rt,ctx,"round_to expects a number and integer digits from -12 through 12"); return 0;
        }
        double scale=pow(10.0,b);
        result=round(a*scale)/scale;
        if (!isfinite(result)) { set_error(rt,ctx,"round_to result is not finite"); return 0; }
        *out=value_number(result); return 1;
    }
    set_error(rt,ctx,"Unknown built-in function");
    return 0;
}

static int parse_builtin_call(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    const char* name=t->items[*p].text;
    size_t cursor=*p+2;
    BriseValue args[8];
    memset(args,0,sizeof(args));
    size_t argc=0;
    if (t->items[cursor].type!=TOK_RPAREN) {
        for (;;) {
            if (argc>=sizeof(args)/sizeof(args[0])) {
                set_error(rt,ctx,"Built-in function accepts at most 8 arguments");
                for(size_t i=0;i<argc;i++) free_value(&args[i]);
                return 0;
            }
            if (!parse_expression(rt,t,&cursor,ctx,&args[argc])) {
                for(size_t i=0;i<argc;i++) free_value(&args[i]);
                return 0;
            }
            argc++;
            if (t->items[cursor].type==TOK_COMMA) { cursor++; continue; }
            if (t->items[cursor].type!=TOK_RPAREN) {
                set_error(rt,ctx,"Expected ',' or ')' in function call");
                for(size_t i=0;i<argc;i++) free_value(&args[i]);
                return 0;
            }
            break;
        }
    }
    if (t->items[cursor].type!=TOK_RPAREN) {
        set_error(rt,ctx,"Unclosed function call");
        for(size_t i=0;i<argc;i++) free_value(&args[i]);
        return 0;
    }
    *p=cursor+1;
    int ok=evaluate_builtin(rt,ctx,name,args,argc,out);
    for(size_t i=0;i<argc;i++) free_value(&args[i]);
    return ok;
}

static int parse_primary(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    BriseToken* tk = &t->items[*p];
    if (tk->type == TOK_NUMBER) {
        *out = value_number(strtod(tk->text, NULL)); (*p)++; return 1;
    }
    if (tk->type == TOK_STRING) {
        char* inter = interpolate(rt, tk->text);
        *out = value_string(inter);
        free(inter);
        (*p)++; return 1;
    }
    if (tk->type == TOK_KEYWORD && (brise_streq_ci(tk->text,"true") || brise_streq_ci(tk->text,"false"))) {
        *out = value_bool(brise_streq_ci(tk->text,"true")); (*p)++; return 1;
    }
    if (tk->type == TOK_IDENTIFIER && t->items[*p + 1].type == TOK_LPAREN) return parse_builtin_call(rt,t,p,ctx,out);
    if (tk->type == TOK_IDENTIFIER || tk->type == TOK_KEYWORD) {
        if (brise_streq_ci(tk->text,"pi")) { *out=value_number(3.14159265358979323846); (*p)++; return 1; }
        if (brise_streq_ci(tk->text,"e")) { *out=value_number(2.71828182845904523536); (*p)++; return 1; }
        if (brise_streq_ci(tk->text,"tau")) { *out=value_number(6.28318530717958647692); (*p)++; return 1; }
        if (brise_streq_ci(tk->text,"sqrt2")) { *out=value_number(1.41421356237309504880); (*p)++; return 1; }
        if (brise_streq_ci(tk->text,"ln2")) { *out=value_number(0.69314718055994530942); (*p)++; return 1; }
        if (brise_streq_ci(tk->text,"ln10")) { *out=value_number(2.30258509299404568402); (*p)++; return 1; }
        const VarEntry* v = find_var_const(rt, tk->text);
        if (!v) { set_error(rt, ctx, "Unknown variable"); return 0; }
        *out = clone_value(&v->value); (*p)++; return 1;
    }
    if (tk->type == TOK_LPAREN) {
        (*p)++;
        if (!parse_expression(rt, t, p, ctx, out)) return 0;
        if (t->items[*p].type != TOK_RPAREN) { set_error(rt, ctx, "Expected ')' in expression"); free_value(out); return 0; }
        (*p)++; return 1;
    }
    set_error(rt, ctx, "Unexpected token in expression");
    return 0;
}

static int parse_power(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out);

static int parse_factor(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (t->items[*p].type == TOK_OPERATOR && strcmp(t->items[*p].text, "-")==0) {
        (*p)++;
        if (!parse_factor(rt,t,p,ctx,out)) return 0;
        double n;
        if (!value_to_number(out,&n)) { set_error(rt,ctx,"Unary minus expects number"); free_value(out); return 0; }
        free_value(out); *out = value_number(-n); return 1;
    }
    return parse_primary(rt,t,p,ctx,out);
}

static int parse_power(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (!parse_factor(rt,t,p,ctx,out)) return 0;
    if (t->items[*p].type==TOK_OPERATOR && strcmp(t->items[*p].text,"^")==0) {
        (*p)++;
        BriseValue right;
        if (!parse_power(rt,t,p,ctx,&right)) { free_value(out); return 0; }
        double left_number=0.0,right_number=0.0;
        if (!value_to_number(out,&left_number) || !value_to_number(&right,&right_number)) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Power expects numbers"); return 0;
        }
        free_value(out); free_value(&right);
        double powered=pow(left_number,right_number);
        if (!isfinite(powered)) { set_error(rt,ctx,"Power result is invalid or outside the finite numeric range"); return 0; }
        *out=value_number(powered);
    }
    return 1;
}

static int parse_term(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (!parse_power(rt,t,p,ctx,out)) return 0;
    while (t->items[*p].type==TOK_OPERATOR &&
           (strcmp(t->items[*p].text,"*")==0 || strcmp(t->items[*p].text,"/")==0 ||
            strcmp(t->items[*p].text,"%")==0)) {
        char op=t->items[*p].text[0];
        (*p)++;
        BriseValue right;
        if (!parse_power(rt,t,p,ctx,&right)) { free_value(out); return 0; }
        double left_number=0.0,right_number=0.0;
        if (!value_to_number(out,&left_number) || !value_to_number(&right,&right_number)) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Arithmetic expects numbers"); return 0;
        }
        if ((op=='/' || op=='%') && right_number==0.0) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Division or remainder by zero"); return 0;
        }
        free_value(out); free_value(&right);
        double value=op=='*'?left_number*right_number:
                     op=='/'?left_number/right_number:fmod(left_number,right_number);
        if (!isfinite(value)) { set_error(rt,ctx,"Arithmetic result is not finite"); return 0; }
        *out=value_number(value);
    }
    return 1;
}

static int parse_expression(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (!parse_term(rt,t,p,ctx,out)) return 0;
    while (t->items[*p].type==TOK_OPERATOR &&
           (strcmp(t->items[*p].text,"+")==0 || strcmp(t->items[*p].text,"-")==0)) {
        char op=t->items[*p].text[0];
        (*p)++;
        BriseValue right;
        if (!parse_term(rt,t,p,ctx,&right)) { free_value(out); return 0; }
        if (op=='+' && (out->type==BRISE_VAL_STRING || right.type==BRISE_VAL_STRING)) {
            char* left_text=value_to_string_heap(out);
            char* right_text=value_to_string_heap(&right);
            if (!left_text || !right_text) {
                free(left_text); free(right_text); free_value(out); free_value(&right);
                set_error(rt,ctx,"Out of memory"); return 0;
            }
            size_t left_len=strlen(left_text),right_len=strlen(right_text);
            if (left_len>SIZE_MAX-right_len-1) {
                free(left_text); free(right_text); free_value(out); free_value(&right);
                set_error(rt,ctx,"String concatenation is too large"); return 0;
            }
            char* merged=(char*)malloc(left_len+right_len+1);
            if (!merged) {
                free(left_text); free(right_text); free_value(out); free_value(&right);
                set_error(rt,ctx,"Out of memory"); return 0;
            }
            memcpy(merged,left_text,left_len);
            memcpy(merged+left_len,right_text,right_len+1);
            free(left_text); free(right_text); free_value(out); free_value(&right);
            *out=value_string(merged); free(merged);
            if (!out->string_value) { set_error(rt,ctx,"Out of memory"); return 0; }
            continue;
        }
        double left_number=0.0,right_number=0.0;
        if (!value_to_number(out,&left_number) || !value_to_number(&right,&right_number)) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Arithmetic expects numbers"); return 0;
        }
        free_value(out); free_value(&right);
        double value=op=='+'?left_number+right_number:left_number-right_number;
        if (!isfinite(value)) { set_error(rt,ctx,"Arithmetic result is not finite"); return 0; }
        *out=value_number(value);
    }
    return 1;
}

static BriseTokenArray token_slice(const BriseTokenArray* src, size_t b, size_t e) {
    BriseTokenArray out = {NULL, 0, 0};
    if (e > src->count) e = src->count;
    for (size_t i=b; i<e; ++i) token_push(&out, src->items[i].type, src->items[i].text);
    if (out.count==0 || out.items[out.count-1].type!=TOK_END) token_push(&out, TOK_END, "");
    return out;
}

static size_t find_matching_rparen(BriseRuntime* rt, const BriseTokenArray* tokens, size_t lp, BriseErrorContext ctx, int* ok) {
    int depth = 0;
    for (size_t i=lp;i<tokens->count;++i) {
        if (tokens->items[i].type==TOK_LPAREN) depth++;
        if (tokens->items[i].type==TOK_RPAREN) { depth--; if (depth==0) { *ok=1; return i; } }
    }
    *ok=0; set_error(rt,ctx,"Unmatched '('");
    return 0;
}

static size_t find_condition_keyword(const BriseTokenArray* cond, const char* keyword) {
    int depth=0;
    for(size_t i=0;i+1<cond->count;i++) {
        if(cond->items[i].type==TOK_LPAREN) depth++;
        else if(cond->items[i].type==TOK_RPAREN && depth>0) depth--;
        else if(depth==0 && cond->items[i].type==TOK_KEYWORD && brise_streq_ci(cond->items[i].text,keyword)) return i;
    }
    return (size_t)-1;
}

static int condition_is_wrapped(const BriseTokenArray* cond) {
    if(cond->count<4 || cond->items[0].type!=TOK_LPAREN) return 0;
    int depth=0;
    for(size_t i=0;i+1<cond->count;i++) {
        if(cond->items[i].type==TOK_LPAREN) depth++;
        else if(cond->items[i].type==TOK_RPAREN) {
            depth--;
            if(depth==0) return i==cond->count-2;
        }
    }
    return 0;
}

static int eval_condition(BriseRuntime* rt, BriseTokenArray* cond, BriseErrorContext ctx, int* result) {
    if(condition_is_wrapped(cond)) {
        BriseTokenArray inner=token_slice(cond,1,cond->count-2);
        int ok=eval_condition(rt,&inner,ctx,result);
        token_array_free(&inner); return ok;
    }
    size_t split=find_condition_keyword(cond,"or");
    if(split!=(size_t)-1) {
        BriseTokenArray left=token_slice(cond,0,split), right=token_slice(cond,split+1,cond->count-1);
        int lv=0, ok=eval_condition(rt,&left,ctx,&lv);
        if(ok && lv) *result=1;
        else if(ok) ok=eval_condition(rt,&right,ctx,result);
        token_array_free(&left); token_array_free(&right); return ok;
    }
    split=find_condition_keyword(cond,"and");
    if(split!=(size_t)-1) {
        BriseTokenArray left=token_slice(cond,0,split), right=token_slice(cond,split+1,cond->count-1);
        int lv=0, ok=eval_condition(rt,&left,ctx,&lv);
        if(ok && !lv) *result=0;
        else if(ok) ok=eval_condition(rt,&right,ctx,result);
        token_array_free(&left); token_array_free(&right); return ok;
    }
    if(cond->count>2 && cond->items[0].type==TOK_KEYWORD && brise_streq_ci(cond->items[0].text,"not")) {
        BriseTokenArray inner=token_slice(cond,1,cond->count-1);
        int v=0, ok=eval_condition(rt,&inner,ctx,&v);
        token_array_free(&inner); if(ok) *result=!v; return ok;
    }

    size_t cmp=(size_t)-1; int depth=0;
    for(size_t i=0;i+1<cond->count;i++) {
        if(cond->items[i].type==TOK_LPAREN) depth++;
        else if(cond->items[i].type==TOK_RPAREN && depth>0) depth--;
        else if(depth==0 && cond->items[i].type==TOK_OPERATOR) {
            const char* op=cond->items[i].text;
            if(!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,">")||!strcmp(op,"<")||!strcmp(op,">=")||!strcmp(op,"<=")) { cmp=i; break; }
        }
    }
    if(cmp==(size_t)-1) {
        size_t p=0; BriseValue v;
        if(!parse_expression(rt,cond,&p,ctx,&v)) return 0;
        if(p!=cond->count-1) { free_value(&v); set_error(rt,ctx,"Unexpected token in condition"); return 0; }
        *result=value_to_bool(&v); free_value(&v); return 1;
    }
    BriseTokenArray left=token_slice(cond,0,cmp), right=token_slice(cond,cmp+1,cond->count-1);
    size_t lp=0,rp=0; BriseValue lv,rv; memset(&lv,0,sizeof(lv)); memset(&rv,0,sizeof(rv));
    int ok=parse_expression(rt,&left,&lp,ctx,&lv);
    if(ok) ok=parse_expression(rt,&right,&rp,ctx,&rv);
    if(ok && (lp!=left.count-1 || rp!=right.count-1)) { set_error(rt,ctx,"Unexpected token in comparison"); ok=0; }
    token_array_free(&left); token_array_free(&right);
    if(!ok) { free_value(&lv); free_value(&rv); return 0; }
    const char* op=cond->items[cmp].text; double ln=0.0,rn=0.0;
    if(value_to_number(&lv,&ln) && value_to_number(&rv,&rn)) {
        if(!strcmp(op,"==")) *result=ln==rn;
        else if(!strcmp(op,"!=")) *result=ln!=rn;
        else if(!strcmp(op,">")) *result=ln>rn;
        else if(!strcmp(op,"<")) *result=ln<rn;
        else if(!strcmp(op,">=")) *result=ln>=rn;
        else *result=ln<=rn;
    } else {
        char* ls=value_to_string_heap(&lv); char* rs=value_to_string_heap(&rv);
        if(!ls || !rs) { free(ls); free(rs); free_value(&lv); free_value(&rv); set_error(rt,ctx,"Out of memory"); return 0; }
        int c=strcmp(ls,rs);
        if(!strcmp(op,"==")) *result=c==0;
        else if(!strcmp(op,"!=")) *result=c!=0;
        else if(!strcmp(op,">")) *result=c>0;
        else if(!strcmp(op,"<")) *result=c<0;
        else if(!strcmp(op,">=")) *result=c>=0;
        else *result=c<=0;
        free(ls); free(rs);
    }
    free_value(&lv); free_value(&rv); return 1;
}

static int append_text(char** dst, const char* text) {
    size_t old = *dst ? strlen(*dst) : 0;
    size_t add = strlen(text);
    if (old>BRISE_MAX_NETWE_BYTES || add>BRISE_MAX_NETWE_BYTES-old) return 0;
    char* n = (char*)realloc(*dst, old + add + 1);
    if (!n) return 0;
    memcpy(n + old, text, add + 1);
    *dst = n;
    return 1;
}

static int append_response_chunk(char** dst, size_t* length, const char* data, size_t amount) {
    if (!dst || !length || (!data && amount) || *length>BRISE_MAX_NETWE_BYTES ||
        amount>BRISE_MAX_NETWE_BYTES-*length) return 0;
    char* next=(char*)realloc(*dst,*length+amount+1);
    if (!next) return 0;
    if (amount) memcpy(next+*length,data,amount);
    *length+=amount;
    next[*length]='\0';
    *dst=next;
    return 1;
}

static int execute_tokens(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx);

static CommandEntry* find_command(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->commands_count;++i) if (strcmp(rt->commands[i].name,name)==0) return &rt->commands[i];
    return NULL;
}

static int set_command(BriseRuntime* rt, const char* name, BriseTokenArray* body) {
    CommandEntry* c = find_command(rt,name);
    if (c) {
        token_array_free(&c->body);
        c->body = token_slice(body,0,body->count);
        return 1;
    }
    if (!ensure_capacity((void**)&rt->commands, &rt->commands_capacity, sizeof(CommandEntry), rt->commands_count+1)) return 0;
    rt->commands[rt->commands_count].name = xstrdup(name);
    rt->commands[rt->commands_count].body = token_slice(body,0,body->count);
    rt->commands_count++;
    return 1;
}

static ListEntry* find_list(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->lists_count;++i) if (strcmp(rt->lists[i].name,name)==0) return &rt->lists[i];
    return NULL;
}

static int list_add_item(ListEntry* list, const char* item) {
    if (!ensure_capacity((void**)&list->items,&list->capacity,sizeof(char*), list->count+1)) return 0;
    list->items[list->count++] = xstrdup(item);
    return 1;
}

static int set_list(BriseRuntime* rt, const char* name, BriseTokenArray* items_tokens) {
    ListEntry* list = find_list(rt,name);
    if (!list) {
        if (!ensure_capacity((void**)&rt->lists,&rt->lists_capacity,sizeof(ListEntry),rt->lists_count+1)) return 0;
        list = &rt->lists[rt->lists_count++];
        memset(list,0,sizeof(*list));
        list->name = xstrdup(name);
    }
    for (size_t i=0;i<list->count;++i) free(list->items[i]);
    free(list->items); list->items=NULL; list->count=list->capacity=0;

    for (size_t i=0;i<items_tokens->count;i++) {
        if (items_tokens->items[i].type==TOK_COMMA || items_tokens->items[i].type==TOK_END) continue;
        if (!list_add_item(list, items_tokens->items[i].text)) return 0;
    }
    return 1;
}


static MatrixEntry* find_matrix(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->matrices_count;i++)
        if (strcmp(rt->matrices[i].name,name)==0) return &rt->matrices[i];
    return NULL;
}

static MemoryEntry* find_memory(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->memory_count;i++)
        if (strcmp(rt->memory[i].name,name)==0) return &rt->memory[i];
    return NULL;
}

static int managed_cell_count_ok(BriseRuntime* rt, size_t old_count, size_t new_count) {
    if (old_count>rt->managed_cells) return 0;
    size_t current=rt->managed_cells-old_count;
    return current<=BRISE_MAX_MANAGED_CELLS &&
           new_count<=BRISE_MAX_MANAGED_CELLS-current;
}

static int store_matrix(BriseRuntime* rt, const char* name, size_t rows, size_t cols, double* cells) {
    if (!name || !*name || !cells || !rows || !cols || rows>SIZE_MAX/cols) return 0;
    size_t count=rows*cols;
    MatrixEntry* m=find_matrix(rt,name);
    size_t old_count=m?m->rows*m->cols:0;
    if (!managed_cell_count_ok(rt,old_count,count)) return 0;
    if (!m) {
        if (!ensure_capacity((void**)&rt->matrices,&rt->matrices_capacity,sizeof(MatrixEntry),rt->matrices_count+1)) return 0;
        m=&rt->matrices[rt->matrices_count];
        memset(m,0,sizeof(*m));
        m->name=xstrdup(name);
        if (!m->name) return 0;
        rt->matrices_count++;
    } else {
        free(m->cells);
        rt->managed_cells-=old_count;
    }
    m->rows=rows; m->cols=cols; m->cells=cells;
    rt->managed_cells+=count;
    return 1;
}

static int store_memory(BriseRuntime* rt, const char* name, size_t count, double* cells) {
    if (!name || !*name || !cells || !count) return 0;
    MemoryEntry* m=find_memory(rt,name);
    size_t old_count=m?m->count:0;
    if (!managed_cell_count_ok(rt,old_count,count)) return 0;
    if (!m) {
        if (!ensure_capacity((void**)&rt->memory,&rt->memory_capacity,sizeof(MemoryEntry),rt->memory_count+1)) return 0;
        m=&rt->memory[rt->memory_count];
        memset(m,0,sizeof(*m));
        m->name=xstrdup(name);
        if (!m->name) return 0;
        rt->memory_count++;
    } else {
        free(m->cells);
        rt->managed_cells-=old_count;
    }
    m->count=count; m->cells=cells;
    rt->managed_cells+=count;
    return 1;
}

static int release_matrix(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->matrices_count;i++) {
        MatrixEntry* m=&rt->matrices[i];
        if (strcmp(m->name,name)!=0) continue;
        size_t count=m->rows*m->cols;
        if (count<=rt->managed_cells) rt->managed_cells-=count;
        free(m->name); free(m->cells);
        rt->matrices[i]=rt->matrices[rt->matrices_count-1];
        rt->matrices_count--;
        return 1;
    }
    return 0;
}

static int release_memory(BriseRuntime* rt, const char* name) {
    for (size_t i=0;i<rt->memory_count;i++) {
        MemoryEntry* m=&rt->memory[i];
        if (strcmp(m->name,name)!=0) continue;
        if (m->count<=rt->managed_cells) rt->managed_cells-=m->count;
        free(m->name); free(m->cells);
        rt->memory[i]=rt->memory[rt->memory_count-1];
        rt->memory_count--;
        return 1;
    }
    return 0;
}

static int read_object_name(BriseRuntime* rt, BriseTokenArray* t, size_t* p,
                            size_t end, BriseErrorContext ctx, const char** name) {
    if (*p>=end || (t->items[*p].type!=TOK_IDENTIFIER && t->items[*p].type!=TOK_STRING)) {
        set_error(rt,ctx,"Expected an allocated matrix or memory-block name");
        return 0;
    }
    *name=t->items[*p].text;
    (*p)++;
    return 1;
}

static int expect_arg_comma(BriseRuntime* rt, BriseTokenArray* t, size_t* p,
                            size_t end, BriseErrorContext ctx) {
    if (*p>=end || t->items[*p].type!=TOK_COMMA) {
        set_error(rt,ctx,"Expected ',' between command arguments");
        return 0;
    }
    (*p)++;
    return 1;
}

static int read_numeric_arg(BriseRuntime* rt, BriseTokenArray* t, size_t* p,
                            size_t end, BriseErrorContext ctx, double* out) {
    if (*p>=end) { set_error(rt,ctx,"Missing numeric command argument"); return 0; }
    BriseValue value;
    if (!parse_expression(rt,t,p,ctx,&value)) return 0;
    int ok=value_to_number(&value,out) && isfinite(*out);
    free_value(&value);
    if (!ok) set_error(rt,ctx,"Expected a finite numeric argument");
    return ok;
}

static int positive_cell_count(BriseRuntime* rt, BriseErrorContext ctx, double n, size_t* out) {
    if (!isfinite(n) || n<1.0 || floor(n)!=n || n>(double)BRISE_MAX_MANAGED_CELLS) {
        set_error(rt,ctx,"Dimensions and cell counts must be positive whole numbers within the memory limit");
        return 0;
    }
    *out=(size_t)n;
    return 1;
}

static int matrix_det_value(BriseRuntime* rt, BriseErrorContext ctx, const MatrixEntry* m, double* result) {
    if (!m || m->rows!=m->cols) {
        set_error(rt,ctx,"Determinant requires a square matrix");
        return 0;
    }
    size_t n=m->rows, count=n*n;
    double* work=(double*)malloc(count*sizeof(double));
    if (!work) { set_error(rt,ctx,"Out of memory calculating determinant"); return 0; }
    memcpy(work,m->cells,count*sizeof(double));
    double det=1.0;
    int sign=1;
    for (size_t col=0;col<n;col++) {
        size_t pivot=col;
        for (size_t row=col+1;row<n;row++)
            if (fabs(work[row*n+col])>fabs(work[pivot*n+col])) pivot=row;
        if (fabs(work[pivot*n+col])<=1e-14) { det=0.0; break; }
        if (pivot!=col) {
            for (size_t j=0;j<n;j++) {
                double tmp=work[col*n+j]; work[col*n+j]=work[pivot*n+j]; work[pivot*n+j]=tmp;
            }
            sign=-sign;
        }
        double diagonal=work[col*n+col];
        det*=diagonal;
        for (size_t row=col+1;row<n;row++) {
            double factor=work[row*n+col]/diagonal;
            for (size_t j=col+1;j<n;j++) work[row*n+j]-=factor*work[col*n+j];
        }
    }
    free(work);
    det*=(double)sign;
    if (!isfinite(det)) { set_error(rt,ctx,"Determinant is outside the finite numeric range"); return 0; }
    *result=det;
    return 1;
}

static double* matrix_inverse_values(BriseRuntime* rt, BriseErrorContext ctx, const MatrixEntry* m) {
    if (!m || m->rows!=m->cols) {
        set_error(rt,ctx,"Inverse requires a square matrix");
        return NULL;
    }
    size_t n=m->rows, count=n*n;
    if (count>BRISE_MAX_MANAGED_CELLS/2) {
        set_error(rt,ctx,"Matrix is too large for the inverse operation");
        return NULL;
    }
    double* work=(double*)malloc(count*sizeof(double));
    double* inverse=(double*)calloc(count,sizeof(double));
    if (!work || !inverse) {
        free(work); free(inverse);
        set_error(rt,ctx,"Out of memory calculating matrix inverse");
        return NULL;
    }
    memcpy(work,m->cells,count*sizeof(double));
    for (size_t i=0;i<n;i++) inverse[i*n+i]=1.0;
    for (size_t col=0;col<n;col++) {
        size_t pivot=col;
        for (size_t row=col+1;row<n;row++)
            if (fabs(work[row*n+col])>fabs(work[pivot*n+col])) pivot=row;
        if (fabs(work[pivot*n+col])<=1e-14) {
            free(work); free(inverse);
            set_error(rt,ctx,"Cannot invert a singular matrix");
            return NULL;
        }
        if (pivot!=col) for (size_t j=0;j<n;j++) {
            double tmp=work[col*n+j]; work[col*n+j]=work[pivot*n+j]; work[pivot*n+j]=tmp;
            tmp=inverse[col*n+j]; inverse[col*n+j]=inverse[pivot*n+j]; inverse[pivot*n+j]=tmp;
        }
        double divisor=work[col*n+col];
        for (size_t j=0;j<n;j++) { work[col*n+j]/=divisor; inverse[col*n+j]/=divisor; }
        for (size_t row=0;row<n;row++) {
            if (row==col) continue;
            double factor=work[row*n+col];
            for (size_t j=0;j<n;j++) {
                work[row*n+j]-=factor*work[col*n+j];
                inverse[row*n+j]-=factor*inverse[col*n+j];
            }
        }
    }
    free(work);
    for (size_t i=0;i<count;i++) if (!isfinite(inverse[i])) {
        free(inverse); set_error(rt,ctx,"Matrix inverse produced a non-finite value"); return NULL;
    }
    return inverse;
}

static int execute_cmd_matrix(BriseRuntime* rt, BriseTokenArray* t, BriseErrorContext ctx) {
    if (t->count<5 || (t->items[2].type!=TOK_IDENTIFIER && t->items[2].type!=TOK_KEYWORD) || t->items[3].type!=TOK_LPAREN) {
        set_error(rt,ctx,"Use Matrix:create, Matrix:set, Matrix:identity, or an advanced Matrix operation");
        return 0;
    }
    const char* method=t->items[2].text;
    int ok=0;
    size_t end=find_matching_rparen(rt,t,3,ctx,&ok);
    if (!ok) return 0;
    size_t p=4, rows=0, cols=0, count=0;
    const char *name=NULL, *second=NULL, *dest=NULL;
    double a=0.0,b=0.0,c=0.0;
    double* values=NULL;
    MatrixEntry *left=NULL, *right=NULL;

    if (brise_streq_ci(method,"create") || brise_streq_ci(method,"basic")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&b) || p!=end ||
            !positive_cell_count(rt,ctx,a,&rows) || !positive_cell_count(rt,ctx,b,&cols) ||
            rows>BRISE_MAX_MANAGED_CELLS/cols) {
            if (!rt->last_error[0]) set_error(rt,ctx,"Invalid Matrix:create dimensions");
            return 0;
        }
        count=rows*cols;
        values=(double*)calloc(count,sizeof(double));
        if (!values || !store_matrix(rt,name,rows,cols,values)) {
            free(values); set_error(rt,ctx,"Matrix allocation failed or managed memory limit exceeded"); return 0;
        }
        return 1;
    }

    if (brise_streq_ci(method,"identity")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || p!=end ||
            !positive_cell_count(rt,ctx,a,&rows) || rows>BRISE_MAX_MANAGED_CELLS/rows) return 0;
        values=(double*)calloc(rows*rows,sizeof(double));
        if (!values) { set_error(rt,ctx,"Out of memory creating identity matrix"); return 0; }
        for (size_t i=0;i<rows;i++) values[i*rows+i]=1.0;
        if (!store_matrix(rt,name,rows,rows,values)) {
            free(values); set_error(rt,ctx,"Matrix allocation failed or managed memory limit exceeded"); return 0;
        }
        return 1;
    }

    if (brise_streq_ci(method,"set")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&b) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&c) || p!=end) return 0;
        left=find_matrix(rt,name);
        if (!left) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        if (a<0 || b<0 || floor(a)!=a || floor(b)!=b ||
            a>=(double)left->rows || b>=(double)left->cols) {
            set_error(rt,ctx,"Matrix index is out of range (indexes are zero-based)"); return 0;
        }
        left->cells[(size_t)a*left->cols+(size_t)b]=c;
        return 1;
    }

    if (brise_streq_ci(method,"fill")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || p!=end) return 0;
        left=find_matrix(rt,name);
        if (!left) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        for (size_t i=0;i<left->rows*left->cols;i++) left->cells[i]=a;
        return 1;
    }

    if (brise_streq_ci(method,"print")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || p!=end) return 0;
        left=find_matrix(rt,name);
        if (!left) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        for (size_t r=0;r<left->rows;r++) {
            for (size_t col=0;col<left->cols;col++)
                printf("%s%.10g",col?" ":"",left->cells[r*left->cols+col]);
            putchar('\n');
        }
        return 1;
    }

    if (brise_streq_ci(method,"det") || brise_streq_ci(method,"determinant")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            p+1!=end || t->items[p].type!=TOK_IDENTIFIER) {
            set_error(rt,ctx,"Syntax: Matrix:det(matrix, result_variable)"); return 0;
        }
        left=find_matrix(rt,name);
        if (!left) { set_error(rt,ctx,"Matrix does not exist"); return 0; }
        if (!matrix_det_value(rt,ctx,left,&a)) return 0;
        if (!set_var(rt,t->items[p].text,value_number(a))) {
            set_error(rt,ctx,"Could not store determinant result"); return 0;
        }
        return 1;
    }

    if (brise_streq_ci(method,"copy") || brise_streq_ci(method,"transpose") ||
        brise_streq_ci(method,"trans") || brise_streq_ci(method,"inverse") ||
        brise_streq_ci(method,"inv") || brise_streq_ci(method,"scale") ||
        brise_streq_ci(method,"add") || brise_streq_ci(method,"sub") ||
        brise_streq_ci(method,"subtract") || brise_streq_ci(method,"mul") ||
        brise_streq_ci(method,"multiply")) {
        if (!read_object_name(rt,t,&p,end,ctx,&dest) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_object_name(rt,t,&p,end,ctx,&name)) return 0;
        left=find_matrix(rt,name);
        if (!left) { set_error(rt,ctx,"Source matrix does not exist"); return 0; }

        if (brise_streq_ci(method,"copy")) {
            if (p!=end) { set_error(rt,ctx,"Matrix:copy expects two names"); return 0; }
            rows=left->rows; cols=left->cols; count=rows*cols;
            values=(double*)malloc(count*sizeof(double));
            if (values) memcpy(values,left->cells,count*sizeof(double));
        } else if (brise_streq_ci(method,"transpose") || brise_streq_ci(method,"trans")) {
            if (p!=end) { set_error(rt,ctx,"Matrix:transpose expects two names"); return 0; }
            rows=left->cols; cols=left->rows; count=rows*cols;
            values=(double*)malloc(count*sizeof(double));
            if (values) for (size_t r=0;r<left->rows;r++) for (size_t col=0;col<left->cols;col++)
                values[col*cols+r]=left->cells[r*left->cols+col];
        } else if (brise_streq_ci(method,"inverse") || brise_streq_ci(method,"inv")) {
            if (p!=end) { set_error(rt,ctx,"Matrix:inverse expects two names"); return 0; }
            values=matrix_inverse_values(rt,ctx,left);
            if (!values) return 0;
            rows=left->rows; cols=left->cols; count=rows*cols;
        } else if (brise_streq_ci(method,"scale")) {
            if (!expect_arg_comma(rt,t,&p,end,ctx) || !read_numeric_arg(rt,t,&p,end,ctx,&a) || p!=end) return 0;
            rows=left->rows; cols=left->cols; count=rows*cols;
            values=(double*)malloc(count*sizeof(double));
            if (values) for (size_t i=0;i<count;i++) values[i]=left->cells[i]*a;
        } else {
            if (!expect_arg_comma(rt,t,&p,end,ctx) || !read_object_name(rt,t,&p,end,ctx,&second) || p!=end) {
                if (!rt->last_error[0]) set_error(rt,ctx,"Expected a second source matrix name");
                return 0;
            }
            right=find_matrix(rt,second);
            if (!right) { set_error(rt,ctx,"Second source matrix does not exist"); return 0; }
            if (brise_streq_ci(method,"add") || brise_streq_ci(method,"sub") || brise_streq_ci(method,"subtract")) {
                if (left->rows!=right->rows || left->cols!=right->cols) {
                    set_error(rt,ctx,"Matrix addition/subtraction requires equal dimensions"); return 0;
                }
                rows=left->rows; cols=left->cols; count=rows*cols;
                values=(double*)malloc(count*sizeof(double));
                if (values) for (size_t i=0;i<count;i++)
                    values[i]=left->cells[i]+(brise_streq_ci(method,"add")?right->cells[i]:-right->cells[i]);
            } else {
                if (left->cols!=right->rows) {
                    set_error(rt,ctx,"Matrix multiplication requires left columns to equal right rows"); return 0;
                }
                rows=left->rows; cols=right->cols; count=rows*cols;
                values=(double*)calloc(count,sizeof(double));
                if (values) for (size_t r=0;r<rows;r++) for (size_t k=0;k<left->cols;k++)
                    for (size_t col=0;col<cols;col++)
                        values[r*cols+col]+=left->cells[r*left->cols+k]*right->cells[k*cols+col];
            }
        }
        if (!values) { set_error(rt,ctx,"Out of memory creating matrix result"); return 0; }
        for (size_t i=0;i<count;i++) if (!isfinite(values[i])) {
            free(values); set_error(rt,ctx,"Matrix operation produced a non-finite value"); return 0;
        }
        if (!store_matrix(rt,dest,rows,cols,values)) {
            free(values); set_error(rt,ctx,"Matrix result exceeds the managed memory limit"); return 0;
        }
        return 1;
    }

    set_error(rt,ctx,"Unknown Matrix operation");
    return 0;
}

static int execute_cmd_memory(BriseRuntime* rt, BriseTokenArray* t, BriseErrorContext ctx) {
    if (t->count<5 || (t->items[2].type!=TOK_IDENTIFIER && t->items[2].type!=TOK_KEYWORD) || t->items[3].type!=TOK_LPAREN) {
        set_error(rt,ctx,"Use Memory:alloc(name, cells), Memory:set(name, index, value), or Memory:fill");
        return 0;
    }
    const char* method=t->items[2].text;
    int ok=0;
    size_t end=find_matching_rparen(rt,t,3,ctx,&ok);
    if (!ok) return 0;
    size_t p=4, count=0;
    const char* name=NULL;
    double a=0.0,b=0.0;
    double* cells=NULL;

    if (brise_streq_ci(method,"alloc") || brise_streq_ci(method,"create")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || p!=end ||
            !positive_cell_count(rt,ctx,a,&count)) return 0;
        cells=(double*)calloc(count,sizeof(double));
        if (!cells || !store_memory(rt,name,count,cells)) {
            free(cells); set_error(rt,ctx,"Memory allocation failed or managed memory limit exceeded"); return 0;
        }
        return 1;
    }

    if (brise_streq_ci(method,"set")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&b) || p!=end) return 0;
        MemoryEntry* block=find_memory(rt,name);
        if (!block) { set_error(rt,ctx,"Memory block does not exist or was freed"); return 0; }
        if (a<0 || floor(a)!=a || a>=(double)block->count) {
            set_error(rt,ctx,"Memory cell index is out of range (indexes are zero-based)"); return 0;
        }
        block->cells[(size_t)a]=b;
        return 1;
    }

    if (brise_streq_ci(method,"fill")) {
        if (!read_object_name(rt,t,&p,end,ctx,&name) || !expect_arg_comma(rt,t,&p,end,ctx) ||
            !read_numeric_arg(rt,t,&p,end,ctx,&a) || p!=end) return 0;
        MemoryEntry* block=find_memory(rt,name);
        if (!block) { set_error(rt,ctx,"Memory block does not exist or was freed"); return 0; }
        for (size_t i=0;i<block->count;i++) block->cells[i]=a;
        return 1;
    }

    set_error(rt,ctx,"Unknown Memory operation");
    return 0;
}

static int execute_cmd_free(BriseRuntime* rt, BriseTokenArray* t, BriseErrorContext ctx) {
    if (t->count<4 || (t->items[2].type!=TOK_IDENTIFIER && t->items[2].type!=TOK_STRING) ||
        t->items[3].type!=TOK_END) {
        set_error(rt,ctx,"Use free:name to release a matrix or memory block");
        return 0;
    }
    const char* name=t->items[2].text;
    int matrix_released=release_matrix(rt,name);
    int memory_released=release_memory(rt,name);
    if (!matrix_released && !memory_released) {
        set_error(rt,ctx,"No allocated matrix or memory block has that name");
        return 0;
    }
    return 1;
}


static int execute_cmd_say(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    BriseTokenArray expr = token_slice(tokens,2,tokens->count-1);
    size_t p=0; BriseValue v;
    int ok = parse_expression(rt,&expr,&p,ctx,&v);
    token_array_free(&expr);
    if (!ok) return 0;
    char* s = value_to_string_heap(&v);
    char* it = interpolate(rt,s);
    printf("%s\n", it);
    free(s); free(it); free_value(&v);
    return 1;
}

static int execute_cmd_set(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx, int calc_alias) {
    (void)calc_alias;
    if (tokens->count < 5 || tokens->items[2].type!=TOK_IDENTIFIER || strcmp(tokens->items[3].text,"=")!=0) {
        set_error(rt,ctx,"Invalid set/calc syntax"); return 0;
    }
    BriseTokenArray expr = token_slice(tokens,4,tokens->count-1);
    size_t p=0; BriseValue v;
    int ok = parse_expression(rt,&expr,&p,ctx,&v);
    token_array_free(&expr);
    if (!ok) return 0;
    if (!set_var(rt,tokens->items[2].text,v)) { free_value(&v); set_error(rt,ctx,"Out of memory"); return 0; }
    return 1;
}

static int find_action_block(const BriseTokenArray* tokens, size_t start, size_t* lp_out, size_t* rp_out) {
    for(size_t i=start;i+1<tokens->count;i++) {
        if(tokens->items[i].type!=TOK_LPAREN) continue;
        int depth=0;
        for(size_t j=i;j+1<tokens->count;j++) {
            if(tokens->items[j].type==TOK_LPAREN) depth++;
            else if(tokens->items[j].type==TOK_RPAREN) {
                depth--;
                if(depth==0) {
                    int closes_action=tokens->items[j+1].type==TOK_END ||
                        (tokens->items[j+1].type==TOK_KEYWORD && brise_streq_ci(tokens->items[j+1].text,"else"));
                    if(closes_action) { *lp_out=i; *rp_out=j; return 1; }
                    break;
                }
            }
        }
    }
    return 0;
}

static int execute_cmd_if(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=0,rp=0;
    if(!find_action_block(tokens,2,&lp,&rp)) { set_error(rt,ctx,"if requires action block"); return 0; }
    int ok=1;
    BriseTokenArray cond = token_slice(tokens,2,lp);
    int pass=0;
    ok = eval_condition(rt,&cond,ctx,&pass); token_array_free(&cond); if(!ok) return 0;
    BriseTokenArray act = token_slice(tokens,lp+1,rp);
    if (pass) {
        ok = execute_tokens(rt,&act,ctx);
        token_array_free(&act);
        return ok;
    }
    token_array_free(&act);

    if (rp + 4 < tokens->count && tokens->items[rp+1].type==TOK_KEYWORD && brise_streq_ci(tokens->items[rp+1].text,"else") &&
        tokens->items[rp+2].type==TOK_COLON && tokens->items[rp+3].type==TOK_LPAREN) {
        int ok2=0; size_t erp = find_matching_rparen(rt,tokens,rp+3,ctx,&ok2); if(!ok2) return 0;
        BriseTokenArray eact = token_slice(tokens,rp+4,erp);
        ok = execute_tokens(rt,&eact,ctx);
        token_array_free(&eact);
        return ok;
    }
    return 1;
}

static int resolve_script_local_path(BriseErrorContext ctx, const char* filename, char* path, size_t path_size);
static FILE* open_script_local_file(BriseErrorContext ctx, const char* filename, int writing);
static int brise_execute_file_stream(BriseRuntime* rt, const char* filename, FILE* stream);

static int execute_cmd_include(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count < 4 || tokens->items[2].type != TOK_STRING) {
        set_error(rt,ctx,"Include expects string file"); return 0;
    }
    char path[1024];
    if (!resolve_script_local_path(ctx,tokens->items[2].text,path,sizeof(path))) {
        set_error(rt,ctx,"Include accepts a regular filename in the script directory; paths are not allowed");
        return 0;
    }
    FILE* included=open_script_local_file(ctx,tokens->items[2].text,0);
    if (!included) {
        set_error(rt,ctx,"Include requires a regular, non-symlink file in the script directory");
        return 0;
    }
    return brise_execute_file_stream(rt,path,included);
}

static int execute_cmd_list(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count<6 || tokens->items[2].type!=TOK_IDENTIFIER || tokens->items[3].type!=TOK_LPAREN) { set_error(rt,ctx,"Invalid List syntax"); return 0; }
    int ok=0; size_t rp = find_matching_rparen(rt,tokens,3,ctx,&ok); if(!ok) return 0;
    BriseTokenArray it = token_slice(tokens,4,rp);
    ok = set_list(rt,tokens->items[2].text,&it);
    token_array_free(&it);
    if(!ok) { set_error(rt,ctx,"Out of memory"); return 0; }
    return 1;
}

static int execute_cmd_loop(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=(size_t)-1;
    for (size_t i=0;i<tokens->count;i++) if (tokens->items[i].type==TOK_LPAREN) { lp=i; break; }
    if (lp==(size_t)-1) { set_error(rt,ctx,"Say to everyone requires (...) block"); return 0; }
    int ok=0; size_t rp = find_matching_rparen(rt,tokens,lp,ctx,&ok); if(!ok) return 0;
    BriseTokenArray tpl = token_slice(tokens,lp+1,rp);
    for (size_t i=0;i<rt->lists_count;i++) {
        ListEntry* list = &rt->lists[i];
        for (size_t j=0;j<list->count;j++) {
            BriseTokenArray act = token_slice(&tpl,0,tpl.count);
            for (size_t k=0;k<act.count;k++) {
                if (act.items[k].type==TOK_IDENTIFIER && strcmp(act.items[k].text,"item")==0) {
                    free(act.items[k].text);
                    act.items[k].type = TOK_STRING;
                    act.items[k].text = xstrdup(list->items[j]);
                }
            }
            if (!execute_tokens(rt,&act,ctx)) { token_array_free(&act); token_array_free(&tpl); return 0; }
            token_array_free(&act);
        }
    }
    token_array_free(&tpl);
    return 1;
}


static int execute_cmd_while(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=0,rp=0;
    if(!find_action_block(tokens,2,&lp,&rp)) { set_error(rt,ctx,"while requires action block"); return 0; }
    BriseTokenArray cond=token_slice(tokens,2,lp), action=token_slice(tokens,lp+1,rp);
    for(size_t i=0;i<10000;i++) {
        int pass=0;
        if(!eval_condition(rt,&cond,ctx,&pass)) { token_array_free(&cond); token_array_free(&action); return 0; }
        if(!pass) { token_array_free(&cond); token_array_free(&action); return 1; }
        if(!execute_tokens(rt,&action,ctx)) { token_array_free(&cond); token_array_free(&action); return 0; }
    }
    int still_true=0, ok=eval_condition(rt,&cond,ctx,&still_true);
    token_array_free(&cond); token_array_free(&action);
    if(!ok) return 0;
    if(still_true) { set_error(rt,ctx,"while loop exceeded 10000 iterations"); return 0; }
    return 1;
}

static int execute_cmd_assert(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    BriseTokenArray cond=token_slice(tokens,2,tokens->count-1);
    int pass=0, ok=eval_condition(rt,&cond,ctx,&pass);
    token_array_free(&cond);
    if(!ok) return 0;
    if(!pass) { set_error(rt,ctx,"Assertion failed"); return 0; }
    return 1;
}

static int execute_cmd_command(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count<6 || tokens->items[2].type!=TOK_IDENTIFIER || tokens->items[3].type!=TOK_LPAREN) { set_error(rt,ctx,"Invalid Command syntax"); return 0; }
    int ok=0; size_t rp=find_matching_rparen(rt,tokens,3,ctx,&ok); if(!ok) return 0;
    BriseTokenArray body = token_slice(tokens,4,rp);
    ok = set_command(rt,tokens->items[2].text,&body);
    token_array_free(&body);
    if(!ok) { set_error(rt,ctx,"Out of memory"); return 0; }
    return 1;
}

static int execute_cmd_random(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    BriseTokenArray expr = token_slice(tokens,2,tokens->count-1);
    size_t p=0; BriseValue minv,maxv;
    if (!parse_expression(rt,&expr,&p,ctx,&minv)) { token_array_free(&expr); return 0; }
    if (!parse_expression(rt,&expr,&p,ctx,&maxv)) { token_array_free(&expr); free_value(&minv); return 0; }
    token_array_free(&expr);
    double mn,mx;
    if (!value_to_number(&minv,&mn) || !value_to_number(&maxv,&mx)) { free_value(&minv); free_value(&maxv); set_error(rt,ctx,"random expects numeric range"); return 0; }
    free_value(&minv); free_value(&maxv);
    if (!isfinite(mn) || !isfinite(mx) || mn > mx || mn < (double)INT_MIN || mn > (double)INT_MAX ||
        mx < (double)INT_MIN || mx > (double)INT_MAX) {
        set_error(rt,ctx,"random expects an ordered range within the int limits"); return 0;
    }
    int64_t low=(int64_t)mn, high=(int64_t)mx;
    uint64_t range=(uint64_t)(high-low)+UINT64_C(1);
    uint64_t bits=((uint64_t)(unsigned int)rand()<<32) ^ (uint64_t)(unsigned int)rand();
    int64_t chosen=low+(int64_t)(bits%range);
    return set_var(rt,"random",value_number((double)chosen));
}

static int execute_cmd_solve(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count < 4 || tokens->items[2].type != TOK_STRING) { set_error(rt,ctx,"solve expects string expression"); return 0; }
    BriseTokenArray expr = tokenize_line(tokens->items[2].text);
    size_t p=0; BriseValue v;
    int ok = parse_expression(rt,&expr,&p,ctx,&v);
    token_array_free(&expr);
    if (!ok) return 0;
    return set_var(rt,"answer",v);
}

static int execute_cmd_query(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp = 2;
    const char* target = "reply";
    if (tokens->count>3 && tokens->items[2].type==TOK_IDENTIFIER && tokens->items[3].type==TOK_LPAREN) { target=tokens->items[2].text; lp=3; }
    if (tokens->count<=lp || tokens->items[lp].type!=TOK_LPAREN) { set_error(rt,ctx,"query expects (...) prompt"); return 0; }
    int ok=0; size_t rp = find_matching_rparen(rt,tokens,lp,ctx,&ok); if(!ok) return 0;
    BriseTokenArray pexpr = token_slice(tokens,lp+1,rp);
    size_t p=0; BriseValue pv;
    if (!parse_expression(rt,&pexpr,&p,ctx,&pv)) { token_array_free(&pexpr); return 0; }
    token_array_free(&pexpr);
    char* prompt = value_to_string_heap(&pv);
    char* pr = interpolate(rt,prompt);
    printf("%s ", pr);
    free(prompt); free(pr); free_value(&pv);

    char buf[1024];
    if (!fgets(buf,sizeof(buf),stdin)) buf[0]=0;
    size_t n=strlen(buf); if (n>0 && (buf[n-1]=='\n' || buf[n-1]=='\r')) buf[n-1]=0;
    set_var(rt,target,value_string(buf));
    set_var(rt,"reply",value_string(buf));
    return 1;
}

static int resolve_script_local_path(BriseErrorContext ctx, const char* filename, char* path, size_t path_size) {
    if (!ctx.file || !filename || !filename[0] || !path || !path_size ||
        strcmp(filename,".")==0 || strcmp(filename,"..")==0) return 0;
    for (const unsigned char* p=(const unsigned char*)filename;*p;++p)
        if (*p=='/' || *p=='\\' || *p==':' || *p<32) return 0;
    const char* slash=strrchr(ctx.file,'/');
    const char* backslash=strrchr(ctx.file,'\\');
    if (!slash || (backslash && backslash>slash)) slash=backslash;
    size_t prefix=slash?(size_t)(slash-ctx.file+1):0;
    size_t name_length=strlen(filename);
    if (prefix>=path_size || name_length>=path_size-prefix) return 0;
    if (prefix) memcpy(path,ctx.file,prefix);
    memcpy(path+prefix,filename,name_length+1);
    return 1;
}

static FILE* open_script_local_file(BriseErrorContext ctx, const char* filename, int writing) {
    char path[1024];
    if (!resolve_script_local_path(ctx,filename,path,sizeof(path))) return NULL;
#ifdef _WIN32
    HANDLE handle=CreateFileA(path,writing?GENERIC_WRITE:GENERIC_READ,FILE_SHARE_READ,NULL,
        writing?OPEN_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (handle==INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(handle); return NULL;
    }
    if (writing) {
        LARGE_INTEGER zero; zero.QuadPart=0;
        if (!SetFilePointerEx(handle,zero,NULL,FILE_BEGIN) || !SetEndOfFile(handle)) {
            CloseHandle(handle); return NULL;
        }
    }
    int fd=_open_osfhandle((intptr_t)handle,_O_BINARY|(writing?_O_WRONLY:_O_RDONLY));
    if (fd<0) { CloseHandle(handle); return NULL; }
    FILE* file=_fdopen(fd,writing?"wb":"rb");
    if (!file) _close(fd);
    return file;
#else
    int flags=writing?(O_WRONLY|O_CREAT|O_TRUNC):O_RDONLY;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags|=O_CLOEXEC;
#endif
    int fd=open(path,flags,0600);
    if (fd<0) return NULL;
    struct stat info;
    if (fstat(fd,&info)!=0 || !S_ISREG(info.st_mode)) { close(fd); return NULL; }
    FILE* file=fdopen(fd,writing?"wb":"rb");
    if (!file) close(fd);
    return file;
#endif
}

static int execute_cmd_read(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=2;
    const char* target="read";
    if (tokens->count>3 && tokens->items[2].type==TOK_IDENTIFIER && tokens->items[3].type==TOK_LPAREN) {
        target=tokens->items[2].text; lp=3;
    }
    if (tokens->count<=lp || tokens->items[lp].type!=TOK_LPAREN) {
        set_error(rt,ctx,"read expects (...) path"); return 0;
    }
    int ok=0;
    size_t rp=find_matching_rparen(rt,tokens,lp,ctx,&ok);
    if (!ok) return 0;
    BriseTokenArray expression=token_slice(tokens,lp+1,rp);
    size_t cursor=0;
    BriseValue path_value;
    if (!parse_expression(rt,&expression,&cursor,ctx,&path_value)) { token_array_free(&expression); return 0; }
    token_array_free(&expression);
    char* filename=value_to_string_heap(&path_value);
    free_value(&path_value);
    if (!filename) { set_error(rt,ctx,"Out of memory"); return 0; }
    FILE* file=open_script_local_file(ctx,filename,0);
    free(filename);
    if (!file) {
        set_error(rt,ctx,"read accepts a regular filename in the script directory; paths and symlinks are not allowed");
        return 0;
    }
    if (fseek(file,0,SEEK_END)!=0) {
        fclose(file); set_error(rt,ctx,"read could not seek to the end of the file"); return 0;
    }
    long size=ftell(file);
    if (size<0 || (unsigned long)size>BRISE_MAX_FILE_BYTES) {
        fclose(file); set_error(rt,ctx,"read file is too large or its size could not be determined"); return 0;
    }
    if (fseek(file,0,SEEK_SET)!=0) {
        fclose(file); set_error(rt,ctx,"read could not rewind the file"); return 0;
    }
    char* data=(char*)malloc((size_t)size+1);
    if (!data) { fclose(file); set_error(rt,ctx,"Out of memory while reading file"); return 0; }
    size_t count=fread(data,1,(size_t)size,file);
    int io_error=ferror(file),close_error=fclose(file);
    if (io_error || close_error!=0 || count!=(size_t)size) {
        free(data); set_error(rt,ctx,"read failed before the complete file was loaded"); return 0;
    }
    data[count]='\0';
    BriseValue result=value_string(data);
    free(data);
    if (!result.string_value || !set_var(rt,target,result)) {
        free_value(&result); set_error(rt,ctx,"Out of memory while storing file contents"); return 0;
    }
    return 1;
}

static int execute_cmd_write(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count<6 || tokens->items[2].type!=TOK_LPAREN) {
        set_error(rt,ctx,"write expects (path, value)"); return 0;
    }
    int ok=0;
    size_t rp=find_matching_rparen(rt,tokens,2,ctx,&ok);
    if (!ok) return 0;
    size_t comma=rp;
    int depth=0;
    for (size_t i=3;i<rp;++i) {
        if (tokens->items[i].type==TOK_LPAREN) depth++;
        else if (tokens->items[i].type==TOK_RPAREN) depth--;
        else if (depth==0 && tokens->items[i].type==TOK_COMMA) { comma=i; break; }
    }
    if (comma==rp) { set_error(rt,ctx,"write expects comma"); return 0; }
    BriseTokenArray path_expression=token_slice(tokens,3,comma);
    BriseTokenArray value_expression=token_slice(tokens,comma+1,rp);
    size_t path_cursor=0,value_cursor=0;
    BriseValue path_value,value;
    memset(&path_value,0,sizeof(path_value)); memset(&value,0,sizeof(value));
    if (!parse_expression(rt,&path_expression,&path_cursor,ctx,&path_value)) {
        token_array_free(&path_expression); token_array_free(&value_expression); return 0;
    }
    if (!parse_expression(rt,&value_expression,&value_cursor,ctx,&value)) {
        token_array_free(&path_expression); token_array_free(&value_expression); free_value(&path_value); return 0;
    }
    token_array_free(&path_expression); token_array_free(&value_expression);
    char* filename=value_to_string_heap(&path_value);
    char* text_value=value_to_string_heap(&value);
    free_value(&path_value); free_value(&value);
    if (!filename || !text_value) {
        free(filename); free(text_value); set_error(rt,ctx,"Out of memory"); return 0;
    }
    size_t text_length=strlen(text_value);
    if (text_length>BRISE_MAX_FILE_BYTES) {
        free(filename); free(text_value); set_error(rt,ctx,"write data exceeds 4 MiB"); return 0;
    }
    FILE* file=open_script_local_file(ctx,filename,1);
    free(filename);
    if (!file) {
        free(text_value);
        set_error(rt,ctx,"write accepts a regular filename in the script directory; paths and symlinks are not allowed");
        return 0;
    }
    size_t written=fwrite(text_value,1,text_length,file);
    int close_error=fclose(file);
    free(text_value);
    if (written!=text_length || close_error!=0) {
        set_error(rt,ctx,"write could not save the complete file"); return 0;
    }
    return 1;
}

static FILE* open_call_library_file(const char* lib, const char* path) {
    size_t n=strlen(lib);
    if (!n || n>64) return NULL;
    for (size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)lib[i];
        if (!(isalnum(c) || c=='_' || c=='-')) return NULL;
    }
#ifdef _WIN32
    HANDLE dir=CreateFileA("libs",FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (dir==INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION di;
    int dir_ok=GetFileInformationByHandle(dir,&di) &&
        (di.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(di.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
    CloseHandle(dir);
    if (!dir_ok) return NULL;
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (file==INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION fi;
    int file_ok=GetFileInformationByHandle(file,&fi) &&
        !(fi.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    if (!file_ok) { CloseHandle(file); return NULL; }
    int fd=_open_osfhandle((intptr_t)file,_O_BINARY|_O_RDONLY);
    if (fd<0) { CloseHandle(file); return NULL; }
    FILE* stream=_fdopen(fd,"rb");
    if (!stream) _close(fd);
    return stream;
#else
    int dirfd=open("libs",O_RDONLY|O_DIRECTORY|O_NOFOLLOW
#ifdef O_CLOEXEC
        |O_CLOEXEC
#endif
    );
    if (dirfd<0) return NULL;
    int fd=openat(dirfd,lib,O_RDONLY|O_NOFOLLOW
#ifdef O_CLOEXEC
        |O_CLOEXEC
#endif
    );
    close(dirfd);
    if (fd<0) return NULL;
    struct stat info;
    if (fstat(fd,&info)!=0 || !S_ISREG(info.st_mode)) { close(fd); return NULL; }
    FILE* stream=fdopen(fd,"rb");
    if (!stream) close(fd);
    return stream;
#endif
}

static int execute_cmd_call(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count < 2 || tokens->items[1].type != TOK_STRING) { set_error(rt,ctx,"Call expects string library name"); return 0; }
    const char* lib = tokens->items[1].text;
    if (strcmp(lib, "Graph") == 0) { rt->graph_enabled = 1; return 1; }
    char path[1024];
    int written=snprintf(path,sizeof(path),"libs/%s.bri",lib);
    if (written<0 || (size_t)written>=sizeof(path)) {
        set_error(rt,ctx,"Call library path is too long"); return 0;
    }
    FILE* library=open_call_library_file(lib,path);
    if (!library) {
        set_error(rt,ctx,"Call accepts a safe library name and a regular, non-symlink file in libs/");
        return 0;
    }
    return brise_execute_file_stream(rt,path,library);
}

static int execute_cmd_graph(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (!rt->graph_enabled) { set_error(rt,ctx,"Graph library is not loaded. Use Call \"Graph\""); return 0; }
    if (tokens->count < 5 || tokens->items[2].type != TOK_IDENTIFIER || tokens->items[3].type != TOK_LPAREN) {
        set_error(rt,ctx,"Graph syntax: Graph:window(...), Graph:label(...), Graph:button(...), Graph:render(...)");
        return 0;
    }
    const char* method = tokens->items[2].text;
    int ok = 0;
    size_t rp = find_matching_rparen(rt, tokens, 3, ctx, &ok);
    if (!ok) return 0;

    char* txt = NULL;
    char* callback = xstrdup("");
    BriseTokenArray expr = {NULL, 0, 0};

    if (brise_streq_ci(method, "render") || brise_streq_ci(method, "update") || brise_streq_ci(method, "refresh")) {
        if (rp == 4) {
            txt = xstrdup("0");
        } else {
            expr = token_slice(tokens, 4, rp);
            size_t p = 0;
            BriseValue v;
            if (!parse_expression(rt, &expr, &p, ctx, &v)) { token_array_free(&expr); free(callback); return 0; }
            token_array_free(&expr);
            txt = value_to_string_heap(&v);
            free_value(&v);
        }
    } else if (brise_streq_ci(method, "button")) {
        size_t comma = rp;
        int depth = 0;
        for (size_t i = 4; i < rp; ++i) {
            if (tokens->items[i].type == TOK_LPAREN) depth++;
            if (tokens->items[i].type == TOK_RPAREN) depth--;
            if (depth == 0 && tokens->items[i].type == TOK_COMMA) { comma = i; break; }
        }
        BriseTokenArray label_expr = token_slice(tokens, 4, comma == rp ? rp : comma);
        size_t p = 0;
        BriseValue v;
        if (!parse_expression(rt, &label_expr, &p, ctx, &v)) { token_array_free(&label_expr); free(callback); return 0; }
        token_array_free(&label_expr);
        txt = value_to_string_heap(&v);
        free_value(&v);

        if (comma != rp) {
            free(callback);
            BriseTokenArray cb_expr = token_slice(tokens, comma + 1, rp);
            if (cb_expr.count == 1 && (cb_expr.items[0].type == TOK_IDENTIFIER || cb_expr.items[0].type == TOK_KEYWORD)) {
                callback = xstrdup(cb_expr.items[0].text);
            } else {
                p = 0;
                BriseValue cv;
                if (!parse_expression(rt, &cb_expr, &p, ctx, &cv)) { token_array_free(&cb_expr); free(txt); return 0; }
                callback = value_to_string_heap(&cv);
                free_value(&cv);
            }
            token_array_free(&cb_expr);
        }
    } else {
        expr = token_slice(tokens, 4, rp);
        size_t p = 0;
        BriseValue v;
        if (!parse_expression(rt, &expr, &p, ctx, &v)) { token_array_free(&expr); free(callback); return 0; }
        token_array_free(&expr);
        txt = value_to_string_heap(&v);
        free_value(&v);
    }

    if (!txt || !callback) { free(txt); free(callback); set_error(rt,ctx,"Out of memory"); return 0; }

    if (brise_streq_ci(method, "window")) {
        free(rt->graph_title);
        rt->graph_title = xstrdup(txt);
        free(txt);
        free(callback);
        return 1;
    }
    if (brise_streq_ci(method, "label") || brise_streq_ci(method, "button")) {
        char row[2048];
        if (brise_streq_ci(method, "label")) snprintf(row, sizeof(row), "L:%s\n", txt);
        else snprintf(row, sizeof(row), "B:%s|%s\n", txt, callback);
        free(txt);
        free(callback);
        if (!append_text(&rt->graph_elements, row)) { set_error(rt,ctx,"Out of memory"); return 0; }
        return 1;
    }
    if (!brise_streq_ci(method, "render") && !brise_streq_ci(method, "update") && !brise_streq_ci(method, "refresh")) {
        free(txt);
        free(callback);
        set_error(rt,ctx,"Unknown Graph method");
        return 0;
    }

    int window_ms = atoi(txt);
    int run_forever = (window_ms <= 0) || brise_streq_ci(txt, "forever");
    free(txt);
    free(callback);

    void* lib = brise_dlopen("SDL2.dll");
    if (!lib) lib = brise_dlopen("libSDL2-2.0.so.0");
    if (!lib) lib = brise_dlopen("libSDL2.so");
    if (!lib) { set_error(rt,ctx,"SDL2 is not installed (Graph needs SDL2)"); return 0; }

    typedef int (*SDL_Init_Fn)(unsigned int);
    typedef void (*SDL_Quit_Fn)(void);
    typedef void* (*SDL_CreateWindow_Fn)(const char*, int, int, int, int, unsigned int);
    typedef void* (*SDL_CreateRenderer_Fn)(void*, int, unsigned int);
    typedef int (*SDL_SetRenderDrawColor_Fn)(void*, unsigned char, unsigned char, unsigned char, unsigned char);
    typedef int (*SDL_RenderClear_Fn)(void*);
    typedef int (*SDL_RenderFillRect_Fn)(void*, const void*);
    typedef void (*SDL_RenderPresent_Fn)(void*);
    typedef int (*SDL_PollEvent_Fn)(void*);
    typedef void (*SDL_Delay_Fn)(unsigned int);
    typedef void (*SDL_DestroyRenderer_Fn)(void*);
    typedef void (*SDL_DestroyWindow_Fn)(void*);

    SDL_Init_Fn SDL_Init_p = (SDL_Init_Fn)brise_dlsym(lib, "SDL_Init");
    SDL_Quit_Fn SDL_Quit_p = (SDL_Quit_Fn)brise_dlsym(lib, "SDL_Quit");
    SDL_CreateWindow_Fn SDL_CreateWindow_p = (SDL_CreateWindow_Fn)brise_dlsym(lib, "SDL_CreateWindow");
    SDL_CreateRenderer_Fn SDL_CreateRenderer_p = (SDL_CreateRenderer_Fn)brise_dlsym(lib, "SDL_CreateRenderer");
    SDL_SetRenderDrawColor_Fn SDL_SetRenderDrawColor_p = (SDL_SetRenderDrawColor_Fn)brise_dlsym(lib, "SDL_SetRenderDrawColor");
    SDL_RenderClear_Fn SDL_RenderClear_p = (SDL_RenderClear_Fn)brise_dlsym(lib, "SDL_RenderClear");
    SDL_RenderFillRect_Fn SDL_RenderFillRect_p = (SDL_RenderFillRect_Fn)brise_dlsym(lib, "SDL_RenderFillRect");
    SDL_RenderPresent_Fn SDL_RenderPresent_p = (SDL_RenderPresent_Fn)brise_dlsym(lib, "SDL_RenderPresent");
    SDL_PollEvent_Fn SDL_PollEvent_p = (SDL_PollEvent_Fn)brise_dlsym(lib, "SDL_PollEvent");
    SDL_Delay_Fn SDL_Delay_p = (SDL_Delay_Fn)brise_dlsym(lib, "SDL_Delay");
    SDL_DestroyRenderer_Fn SDL_DestroyRenderer_p = (SDL_DestroyRenderer_Fn)brise_dlsym(lib, "SDL_DestroyRenderer");
    SDL_DestroyWindow_Fn SDL_DestroyWindow_p = (SDL_DestroyWindow_Fn)brise_dlsym(lib, "SDL_DestroyWindow");

    if (!SDL_Init_p || !SDL_Quit_p || !SDL_CreateWindow_p || !SDL_CreateRenderer_p || !SDL_SetRenderDrawColor_p ||
        !SDL_RenderClear_p || !SDL_RenderFillRect_p || !SDL_RenderPresent_p || !SDL_PollEvent_p || !SDL_Delay_p ||
        !SDL_DestroyRenderer_p || !SDL_DestroyWindow_p) {
        brise_dlclose(lib);
        set_error(rt,ctx,"SDL2 symbols are missing");
        return 0;
    }

    if (SDL_Init_p(0x00000020u) != 0) { brise_dlclose(lib); set_error(rt,ctx,"SDL2 init failed"); return 0; }

    const char* title = rt->graph_title ? rt->graph_title : "Graph";
    void* win = SDL_CreateWindow_p(title, 100, 100, 640, 420, 0);
    if (!win) { SDL_Quit_p(); brise_dlclose(lib); set_error(rt,ctx,"SDL2 window creation failed"); return 0; }
    void* ren = SDL_CreateRenderer_p(win, -1, 0);
    if (!ren) { SDL_DestroyWindow_p(win); SDL_Quit_p(); brise_dlclose(lib); set_error(rt,ctx,"SDL2 renderer creation failed"); return 0; }

    typedef struct { int x,y,w,h; unsigned char r,g,b,a; } R;
    typedef struct { int x,y,w,h; char* callback; } ButtonHit;
    R rect;
    ButtonHit button_hits[64];
    size_t button_hit_count = 0;

    SDL_SetRenderDrawColor_p(ren, 0, 128, 128, 255);
    SDL_RenderClear_p(ren);

    rect.x=40; rect.y=40; rect.w=560; rect.h=320; rect.r=192; rect.g=192; rect.b=192; rect.a=255;
    SDL_SetRenderDrawColor_p(ren, rect.r, rect.g, rect.b, rect.a);
    SDL_RenderFillRect_p(ren, &rect);

    rect.x=40; rect.y=40; rect.w=560; rect.h=30; rect.r=0; rect.g=0; rect.b=128; rect.a=255;
    SDL_SetRenderDrawColor_p(ren, rect.r, rect.g, rect.b, rect.a);
    SDL_RenderFillRect_p(ren, &rect);
    brise_graph_draw_text(ren, SDL_SetRenderDrawColor_p, SDL_RenderFillRect_p, 52, 48, title, 255, 255, 255);

    int y = 90;
    if (rt->graph_elements) {
        char* copy = xstrdup(rt->graph_elements);
        char* line = strtok(copy, "\n");
        while (line) {
            if (line[0] == 'L' && line[1] == ':') {
                rect.x=70; rect.y=y; rect.w=300; rect.h=22; rect.r=230; rect.g=230; rect.b=230; rect.a=255;
                SDL_SetRenderDrawColor_p(ren, rect.r, rect.g, rect.b, rect.a);
                SDL_RenderFillRect_p(ren, &rect);
                brise_graph_draw_text(ren, SDL_SetRenderDrawColor_p, SDL_RenderFillRect_p, rect.x + 8, rect.y + 4, line + 2, 0, 0, 0);
                y += 30;
            } else if (line[0] == 'B' && line[1] == ':') {
                char* button_text = line + 2;
                char* button_callback = strchr(button_text, '|');
                if (button_callback) { *button_callback = '\0'; button_callback++; }
                rect.x=70; rect.y=y; rect.w=180; rect.h=30; rect.r=192; rect.g=192; rect.b=192; rect.a=255;
                SDL_SetRenderDrawColor_p(ren, rect.r, rect.g, rect.b, rect.a);
                SDL_RenderFillRect_p(ren, &rect);
                brise_graph_draw_text(ren, SDL_SetRenderDrawColor_p, SDL_RenderFillRect_p, rect.x + 10, rect.y + 8, button_text, 0, 0, 0);
                if (button_callback && button_callback[0] && button_hit_count < sizeof(button_hits)/sizeof(button_hits[0])) {
                    button_hits[button_hit_count].x = rect.x;
                    button_hits[button_hit_count].y = rect.y;
                    button_hits[button_hit_count].w = rect.w;
                    button_hits[button_hit_count].h = rect.h;
                    button_hits[button_hit_count].callback = xstrdup(button_callback);
                    button_hit_count++;
                }
                y += 40;
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }

    SDL_RenderPresent_p(ren);

    unsigned int elapsed = 0;
    int running = 1;
    int render_ok = 1;
    while (running && (run_forever || elapsed < (unsigned int)window_ms)) {
        unsigned char ev[56];
        while (SDL_PollEvent_p(ev)) {
            unsigned int event_type = 0;
            memcpy(&event_type, ev, sizeof(event_type));
            if (event_type == 0x100u) running = 0;
            if (event_type == 0x401u) {
                int mx = 0;
                int my = 0;
                memcpy(&mx, ev + 20, sizeof(mx));
                memcpy(&my, ev + 24, sizeof(my));
                for (size_t i = 0; i < button_hit_count; ++i) {
                    ButtonHit* hit = &button_hits[i];
                    if (mx >= hit->x && mx < hit->x + hit->w && my >= hit->y && my < hit->y + hit->h) {
                        CommandEntry* command = find_command(rt, hit->callback);
                        if (!command) {
                            set_error(rt,ctx,"Graph button callback command not found");
                            render_ok = 0;
                            running = 0;
                            break;
                        }
                        if (!execute_tokens(rt, &command->body, ctx)) {
                            render_ok = 0;
                            running = 0;
                            break;
                        }
                    }
                }
            }
        }
        SDL_Delay_p(16);
        if (!run_forever) elapsed += 16;
    }

    for (size_t i = 0; i < button_hit_count; ++i) free(button_hits[i].callback);
    SDL_DestroyRenderer_p(ren);
    SDL_DestroyWindow_p(win);
    SDL_Quit_p();
    brise_dlclose(lib);
    return render_ok;
}

static int parse_netwe_url(const char* url, int* secure, char* host, size_t host_size,
                           char* host_header, size_t host_header_size, char* path, size_t path_size,
                           char* port_text, size_t port_size) {
    if (!url || !secure || !host || !host_header || !path || !port_text) return 0;
    const char* authority=NULL;
    int port=0;
    if (strncmp(url,"https://",8)==0) { *secure=1; authority=url+8; port=443; }
    else if (strncmp(url,"http://",7)==0) { *secure=0; authority=url+7; port=80; }
    else return 0;
    const char* authority_end=strpbrk(authority,"/?#");
    if (!authority_end) authority_end=authority+strlen(authority);
    size_t authority_length=(size_t)(authority_end-authority);
    if (!authority_length || memchr(authority,'@',authority_length)) return 0;
    const char* host_begin=authority;
    const char* host_end=authority_end;
    const char* port_begin=NULL;
    int ipv6=0;
    if (*authority=='[') {
        const char* close=(const char*)memchr(authority,']',authority_length);
        if (!close) return 0;
        ipv6=1; host_begin=authority+1; host_end=close;
        if (close+1<authority_end) { if (close[1]!=':') return 0; port_begin=close+2; }
    } else {
        const char* colon=(const char*)memchr(authority,':',authority_length);
        if (colon) {
            if (memchr(colon+1,':',(size_t)(authority_end-colon-1))) return 0;
            host_end=colon; port_begin=colon+1;
        }
    }
    size_t host_length=(size_t)(host_end-host_begin);
    if (!host_length || host_length>=host_size) return 0;
    memcpy(host,host_begin,host_length); host[host_length]='\0';
    if (port_begin) {
        if (port_begin>=authority_end) return 0;
        char* end=NULL; long parsed=strtol(port_begin,&end,10);
        if (end!=authority_end || parsed<1 || parsed>65535) return 0;
        port=(int)parsed;
    }
    if (ipv6) {
        if (port!=(*secure?443:80)) snprintf(host_header,host_header_size,"[%s]:%d",host,port);
        else snprintf(host_header,host_header_size,"[%s]",host);
    } else {
        if (port!=(*secure?443:80)) snprintf(host_header,host_header_size,"%s:%d",host,port);
        else snprintf(host_header,host_header_size,"%s",host);
    }
    const char* path_begin=authority_end;
    if (*path_begin=='/') {
        const char* fragment=strchr(path_begin,'#');
        size_t n=fragment?(size_t)(fragment-path_begin):strlen(path_begin);
        if (n>=path_size) return 0;
        memcpy(path,path_begin,n); path[n]='\0';
    } else if (*path_begin=='?') {
        size_t n=strcspn(path_begin,"#");
        if (n+1>=path_size) return 0;
        path[0]='/'; memcpy(path+1,path_begin,n); path[n+1]='\0';
    } else {
        if (path_size<2) return 0;
        strcpy(path,"/");
    }
    if (snprintf(port_text,port_size,"%d",port)<0) return 0;
    return 1;
}

#ifdef _WIN32
static wchar_t* brise_utf8_to_wide(const char* input) {
    if (!input) return NULL;
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input,-1,NULL,0);
    if (count<=0) return NULL;
    wchar_t* output=(wchar_t*)malloc((size_t)count*sizeof(wchar_t));
    if (!output) return NULL;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input,-1,output,count)) { free(output); return NULL; }
    return output;
}
static int execute_netwe_winhttp(BriseRuntime* rt, const char* url, const char* target, BriseErrorContext ctx) {
    wchar_t* wide_url=brise_utf8_to_wide(url);
    if (!wide_url) { set_error(rt,ctx,"Netwe URL is not valid UTF-8"); return 0; }
    URL_COMPONENTS parts;
    memset(&parts,0,sizeof(parts));
    parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=(DWORD)-1; parts.dwHostNameLength=(DWORD)-1;
    parts.dwUrlPathLength=(DWORD)-1; parts.dwExtraInfoLength=(DWORD)-1;
    if (!WinHttpCrackUrl(wide_url,0,0,&parts) ||
        (parts.nScheme!=INTERNET_SCHEME_HTTP && parts.nScheme!=INTERNET_SCHEME_HTTPS)) {
        free(wide_url); set_error(rt,ctx,"Netwe expects a valid http:// or https:// URL"); return 0;
    }
    wchar_t* host=(wchar_t*)malloc(((size_t)parts.dwHostNameLength+1)*sizeof(wchar_t));
    size_t object_length=(size_t)parts.dwUrlPathLength+(size_t)parts.dwExtraInfoLength;
    wchar_t* object=(wchar_t*)malloc((object_length+2)*sizeof(wchar_t));
    if (!host || !object) { free(host); free(object); free(wide_url); set_error(rt,ctx,"Out of memory"); return 0; }
    memcpy(host,parts.lpszHostName,(size_t)parts.dwHostNameLength*sizeof(wchar_t));
    host[parts.dwHostNameLength]=L'\0';
    size_t copied=0;
    if (parts.dwUrlPathLength) { memcpy(object,parts.lpszUrlPath,(size_t)parts.dwUrlPathLength*sizeof(wchar_t)); copied+=(size_t)parts.dwUrlPathLength; }
    if (parts.dwExtraInfoLength) { memcpy(object+copied,parts.lpszExtraInfo,(size_t)parts.dwExtraInfoLength*sizeof(wchar_t)); copied+=(size_t)parts.dwExtraInfoLength; }
    if (!copied) object[copied++]=L'/';
    object[copied]=L'\0';

    DWORD status_code=0,status_size=sizeof(status_code);
    HINTERNET session=WinHttpOpen(L"BriseNet/0.5",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    HINTERNET connection=NULL,request=NULL;
    int ok=0;
    char* body=NULL;
    size_t body_length=0;
    if (!session) goto winhttp_cleanup;
    WinHttpSetTimeouts(session,5000,5000,10000,10000);
    connection=WinHttpConnect(session,host,parts.nPort,0);
    if (!connection) goto winhttp_cleanup;
    request=WinHttpOpenRequest(connection,L"GET",object,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
    if (!request) goto winhttp_cleanup;
    if (!WinHttpSendRequest(request,L"Accept-Encoding: identity\r\n",-1,WINHTTP_NO_REQUEST_DATA,0,0,0) ||
        !WinHttpReceiveResponse(request,NULL)) goto winhttp_cleanup;
    if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status_code,&status_size,WINHTTP_NO_HEADER_INDEX) ||
        status_code<200 || status_code>=300) {
        set_error(rt,ctx,"Netwe server returned a non-success HTTP status"); goto winhttp_cleanup;
    }
    for (;;) {
        DWORD available=0;
        if (!WinHttpQueryDataAvailable(request,&available)) goto winhttp_cleanup;
        if (!available) { ok=1; break; }
        while (available) {
            char buffer[4096];
            DWORD wanted=available<(DWORD)(sizeof(buffer)-1)?available:(DWORD)(sizeof(buffer)-1);
            DWORD received=0;
            if (!WinHttpReadData(request,buffer,wanted,&received) || !received) goto winhttp_cleanup;
            if (!append_response_chunk(&body,&body_length,buffer,(size_t)received)) {
                set_error(rt,ctx,"Netwe response exceeds 4 MiB or memory is unavailable"); goto winhttp_cleanup;
            }
            available-=received;
        }
    }
winhttp_cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    free(host); free(object); free(wide_url);
    if (!ok) {
        free(body);
        set_error(rt,ctx,"Netwe HTTP request failed (timeout, TLS validation, or connection error)");
        return 0;
    }
    BriseValue result=value_string(body?body:"");
    free(body);
    if (!result.string_value || !set_var(rt,target,result)) {
        free_value(&result); set_error(rt,ctx,"Out of memory while storing Netwe response"); return 0;
    }
    return 1;
}
#else
static int brise_connect_with_timeout(BriseSocket sock, const struct sockaddr* address, socklen_t address_length) {
    int old_flags=fcntl(sock,F_GETFL,0);
    if (old_flags<0 || fcntl(sock,F_SETFL,old_flags|O_NONBLOCK)<0) return 0;
    int rc=connect(sock,address,address_length);
    if (rc<0 && errno!=EINPROGRESS && errno!=EWOULDBLOCK) { (void)fcntl(sock,F_SETFL,old_flags); return 0; }
    if (rc<0) {
        fd_set write_set; FD_ZERO(&write_set); FD_SET(sock,&write_set);
        struct timeval timeout; timeout.tv_sec=5; timeout.tv_usec=0;
        rc=select(sock+1,NULL,&write_set,NULL,&timeout);
        if (rc<=0) { (void)fcntl(sock,F_SETFL,old_flags); return 0; }
        int socket_error=0; socklen_t error_length=(socklen_t)sizeof(socket_error);
        if (getsockopt(sock,SOL_SOCKET,SO_ERROR,&socket_error,&error_length)<0 || socket_error!=0) {
            (void)fcntl(sock,F_SETFL,old_flags); return 0;
        }
    }
    if (fcntl(sock,F_SETFL,old_flags)<0) return 0;
    return 1;
}
static int http_header_has_chunked_transfer(const char* response,const char* header_end) {
    const char* line=response;
    while (line<header_end) {
        const char* end=strstr(line,"\r\n");
        if (!end || end>header_end) break;
        const char* colon=(const char*)memchr(line,':',(size_t)(end-line));
        if (colon && (size_t)(colon-line)==17) {
            static const char key[]="transfer-encoding";
            int matches=1;
            for (size_t i=0;i<17;i++)
                if (tolower((unsigned char)line[i])!=key[i]) { matches=0; break; }
            if (matches) {
                const char* value=colon+1;
                while (value<end && (*value==' ' || *value=='\t')) value++;
                while (value<end) {
                    const char* token_end=value;
                    while (token_end<end && *token_end!=',') token_end++;
                    const char* trimmed_end=token_end;
                    while (trimmed_end>value && (trimmed_end[-1]==' ' || trimmed_end[-1]=='\t')) trimmed_end--;
                    if ((size_t)(trimmed_end-value)==7) {
                        static const char chunked[]="chunked";
                        int token_matches=1;
                        for (size_t i=0;i<7;i++)
                            if (tolower((unsigned char)value[i])!=chunked[i]) { token_matches=0; break; }
                        if (token_matches) return 1;
                    }
                    value=token_end<end?token_end+1:end;
                    while (value<end && (*value==' ' || *value=='\t')) value++;
                }
            }
        }
        line=end+2;
    }
    return 0;
}

static char* decode_http_chunked_body(const char* body) {
    size_t input_length=strlen(body), position=0, output_length=0;
    char* output=(char*)malloc(BRISE_MAX_NETWE_BYTES+1u);
    if (!output) return NULL;
    for (;;) {
        const char* line_end=strstr(body+position,"\r\n");
        if (!line_end) { free(output); return NULL; }
        size_t line_length=(size_t)(line_end-(body+position));
        const char* extension=(const char*)memchr(body+position,';',line_length);
        if (extension) line_length=(size_t)(extension-(body+position));
        while (line_length && (body[position+line_length-1]==' ' || body[position+line_length-1]=='\t')) line_length--;
        size_t start=0;
        while (start<line_length && (body[position+start]==' ' || body[position+start]=='\t')) start++;
        if (line_length<=start || line_length-start>=64) { free(output); return NULL; }
        char size_text[64];
        size_t digits=line_length-start;
        memcpy(size_text,body+position+start,digits);
        size_text[digits]='\0';
        errno=0;
        char* parse_end=NULL;
        unsigned long long chunk_size=strtoull(size_text,&parse_end,16);
        if (errno==ERANGE || !parse_end || *parse_end!='\0') { free(output); return NULL; }
        position=(size_t)(line_end-body)+2;
        if (chunk_size==0) {
            output[output_length]='\0';
            return output;
        }
        if (chunk_size>(unsigned long long)(BRISE_MAX_NETWE_BYTES-output_length) ||
            chunk_size>(unsigned long long)(input_length-position) ||
            input_length-position-(size_t)chunk_size<2) { free(output); return NULL; }
        size_t amount=(size_t)chunk_size;
        memcpy(output+output_length,body+position,amount);
        output_length+=amount;
        position+=amount;
        if (body[position]!='\r' || body[position+1]!='\n') { free(output); return NULL; }
        position+=2;
    }
}

static int execute_netwe_posix(BriseRuntime* rt, const char* url, const char* target, BriseErrorContext ctx) {
    int secure=0;
    char host[256]={0},host_header[320]={0},path[2048]={0},port_text[16]={0};
    if (!parse_netwe_url(url,&secure,host,sizeof(host),host_header,sizeof(host_header),path,sizeof(path),port_text,sizeof(port_text))) {
        set_error(rt,ctx,"Netwe expects a valid http:// or https:// URL"); return 0;
    }
    struct addrinfo hints; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* addresses=NULL;
    if (getaddrinfo(host,port_text,&hints,&addresses)!=0) { set_error(rt,ctx,"Netwe DNS resolution failed"); return 0; }
    BriseSocket sock=BRISE_INVALID_SOCKET;
    for (struct addrinfo* item=addresses;item;item=item->ai_next) {
        sock=brise_socket(item->ai_family,item->ai_socktype,item->ai_protocol);
        if (sock==BRISE_INVALID_SOCKET) continue;
        if (brise_connect_with_timeout(sock,item->ai_addr,(socklen_t)item->ai_addrlen)) break;
        brise_close_socket(sock); sock=BRISE_INVALID_SOCKET;
    }
    brise_freeaddrinfo(addresses);
    if (sock==BRISE_INVALID_SOCKET) { set_error(rt,ctx,"Netwe connection failed or timed out"); return 0; }
    struct timeval timeout; timeout.tv_sec=10; timeout.tv_usec=0;
    (void)setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    (void)setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));

    SSL_CTX* ssl_context=NULL;
    SSL* ssl=NULL;
    if (secure) {
        ssl_context=SSL_CTX_new(TLS_client_method());
        if (!ssl_context) { brise_close_socket(sock); set_error(rt,ctx,"Netwe could not initialize TLS"); return 0; }
        SSL_CTX_set_min_proto_version(ssl_context,TLS1_2_VERSION);
        SSL_CTX_set_verify(ssl_context,SSL_VERIFY_PEER,NULL);
        if (SSL_CTX_set_default_verify_paths(ssl_context)!=1) {
            SSL_CTX_free(ssl_context); brise_close_socket(sock);
            set_error(rt,ctx,"Netwe could not load trusted certificate authorities"); return 0;
        }
        ssl=SSL_new(ssl_context);
        if (!ssl) { SSL_CTX_free(ssl_context); brise_close_socket(sock); set_error(rt,ctx,"Netwe could not create TLS session"); return 0; }
        if (SSL_set_tlsext_host_name(ssl,host)!=1) {
            SSL_free(ssl); SSL_CTX_free(ssl_context); brise_close_socket(sock);
            set_error(rt,ctx,"Netwe could not set TLS server name"); return 0;
        }
        unsigned char ip_buffer[16];
        int is_ip=inet_pton(AF_INET,host,ip_buffer)==1 || inet_pton(AF_INET6,host,ip_buffer)==1;
        int name_ok=is_ip?X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),host):SSL_set1_host(ssl,host);
        if (name_ok!=1 || SSL_set_fd(ssl,sock)!=1 || SSL_connect(ssl)!=1 || SSL_get_verify_result(ssl)!=X509_V_OK) {
            SSL_free(ssl); SSL_CTX_free(ssl_context); brise_close_socket(sock);
            set_error(rt,ctx,"Netwe TLS handshake or certificate/hostname verification failed"); return 0;
        }
    }
    char request[4096];
    int request_length=snprintf(request,sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\nUser-Agent: BriseNet/0.5\r\n\r\n",
        path,host_header);
    if (request_length<0 || (size_t)request_length>=sizeof(request)) {
        if (ssl) SSL_free(ssl);
        if (ssl_context) SSL_CTX_free(ssl_context);
        brise_close_socket(sock); set_error(rt,ctx,"Netwe request URL is too long"); return 0;
    }
    size_t sent=0;
    while (sent<(size_t)request_length) {
        int n=secure?SSL_write(ssl,request+sent,(int)((size_t)request_length-sent)):
                     (int)send(sock,request+sent,(size_t)request_length-sent,0);
        if (n<=0) {
            if (ssl) SSL_free(ssl);
            if (ssl_context) SSL_CTX_free(ssl_context);
            brise_close_socket(sock); set_error(rt,ctx,"Netwe request send failed or timed out"); return 0;
        }
        sent+=(size_t)n;
    }
    char* response=NULL; size_t response_length=0; int read_failed=0;
    for (;;) {
        char buffer[4096];
        int n=secure?SSL_read(ssl,buffer,(int)sizeof(buffer)-1):(int)recv(sock,buffer,sizeof(buffer)-1,0);
        if (n==0) break;
        if (n<0) {
            if (secure && SSL_get_error(ssl,n)==SSL_ERROR_ZERO_RETURN) break;
            if (!secure && errno==EINTR) continue;
            read_failed=1; break;
        }
        if (!append_response_chunk(&response,&response_length,buffer,(size_t)n)) { read_failed=1; break; }
    }
    if (ssl) { (void)SSL_shutdown(ssl); SSL_free(ssl); }
    if (ssl_context) SSL_CTX_free(ssl_context);
    brise_close_socket(sock);
    if (read_failed || !response) {
        free(response); set_error(rt,ctx,"Netwe response exceeded 4 MiB, timed out, or could not be read"); return 0;
    }
    int status=0;
    if (sscanf(response,"HTTP/%*s %d",&status)!=1 || status<200 || status>=300) {
        free(response); set_error(rt,ctx,"Netwe server returned an invalid or non-success HTTP status"); return 0;
    }
    char* body=strstr(response,"\r\n\r\n");
    if (!body) { free(response); set_error(rt,ctx,"Netwe response did not contain an HTTP header terminator"); return 0; }
    body+=4;
    char* decoded_body=NULL;
    if (http_header_has_chunked_transfer(response,body-4)) {
        decoded_body=decode_http_chunked_body(body);
        if (!decoded_body) {
            free(response); set_error(rt,ctx,"Netwe received an invalid or oversized chunked response"); return 0;
        }
        body=decoded_body;
    }
    BriseValue result=value_string(body);
    free(decoded_body);
    free(response);
    if (!result.string_value || !set_var(rt,target,result)) {
        free_value(&result); set_error(rt,ctx,"Out of memory while storing Netwe response"); return 0;
    }
    return 1;
}
#endif

static int execute_cmd_netwe(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=2;
    const char* target="netwe";
    if (tokens->count>3 && tokens->items[2].type==TOK_IDENTIFIER && tokens->items[3].type==TOK_LPAREN) {
        target=tokens->items[2].text; lp=3;
    }
    if (tokens->count<=lp || tokens->items[lp].type!=TOK_LPAREN) { set_error(rt,ctx,"Netwe expects (...) url"); return 0; }
    int ok=0; size_t rp=find_matching_rparen(rt,tokens,lp,ctx,&ok);
    if (!ok) return 0;
    BriseTokenArray expression=token_slice(tokens,lp+1,rp);
    size_t cursor=0; BriseValue value;
    if (!parse_expression(rt,&expression,&cursor,ctx,&value)) { token_array_free(&expression); return 0; }
    token_array_free(&expression);
    char* url=value_to_string_heap(&value);
    free_value(&value);
    if (!url) { set_error(rt,ctx,"Out of memory"); return 0; }
#ifdef _WIN32
    int result=execute_netwe_winhttp(rt,url,target,ctx);
#else
    int result=execute_netwe_posix(rt,url,target,ctx);
#endif
    free(url);
    return result;
}

static int execute_tokens(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count==0 || tokens->items[0].type==TOK_END) return 1;

    if (tokens->count>=4 && tokens->items[0].type==TOK_KEYWORD && brise_streq_ci(tokens->items[0].text,"Say") &&
        tokens->items[1].type==TOK_KEYWORD && brise_streq_ci(tokens->items[1].text,"to") &&
        tokens->items[2].type==TOK_KEYWORD && brise_streq_ci(tokens->items[2].text,"everyone") && tokens->items[3].type==TOK_COLON) {
        return execute_cmd_loop(rt,tokens,ctx);
    }

    if (tokens->count>=3 && tokens->items[0].type==TOK_KEYWORD && tokens->items[1].type==TOK_COLON) {
        const char* cmd = tokens->items[0].text;
        if (brise_streq_ci(cmd,"say")) return execute_cmd_say(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"set")) return execute_cmd_set(rt,tokens,ctx,0);
        if (brise_streq_ci(cmd,"calc")) return execute_cmd_set(rt,tokens,ctx,1);
        if (brise_streq_ci(cmd,"if")) return execute_cmd_if(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"while")) return execute_cmd_while(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"assert")) return execute_cmd_assert(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Include")) return execute_cmd_include(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"List")) return execute_cmd_list(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Command")) return execute_cmd_command(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"random")) return execute_cmd_random(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"solve")) return execute_cmd_solve(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"query")) return execute_cmd_query(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"read")) return execute_cmd_read(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"write")) return execute_cmd_write(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Graph")) return execute_cmd_graph(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Netwe")) return execute_cmd_netwe(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Matrix")) return execute_cmd_matrix(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"Memory")) return execute_cmd_memory(rt,tokens,ctx);
        if (brise_streq_ci(cmd,"free")) return execute_cmd_free(rt,tokens,ctx);
        set_error(rt,ctx,"Unknown command keyword"); return 0;
    }

    if (tokens->count>=2 && tokens->items[0].type==TOK_KEYWORD && brise_streq_ci(tokens->items[0].text,"Call")) {
        return execute_cmd_call(rt,tokens,ctx);
    }

    if (tokens->items[0].type==TOK_IDENTIFIER || tokens->items[0].type==TOK_KEYWORD) {
        CommandEntry* c = find_command(rt,tokens->items[0].text);
        if (c) return execute_tokens(rt,&c->body,ctx);
    }

    set_error(rt,ctx,"Unknown command");
    return 0;
}

BriseRuntime* brise_runtime_create(void) {
    BriseRuntime* rt = (BriseRuntime*)calloc(1,sizeof(BriseRuntime));
    if (!rt) return NULL;
    srand((unsigned int)time(NULL));
    rt->last_error[0]=0;
    return rt;
}

void brise_runtime_destroy(BriseRuntime* rt) {
    if (!rt) return;
    for (size_t i=0;i<rt->vars_count;i++) { free(rt->vars[i].key); free_value(&rt->vars[i].value); }
    free(rt->vars);
    for (size_t i=0;i<rt->commands_count;i++) { free(rt->commands[i].name); token_array_free(&rt->commands[i].body); }
    free(rt->commands);
    for (size_t i=0;i<rt->lists_count;i++) {
        free(rt->lists[i].name);
        for (size_t j=0;j<rt->lists[i].count;j++) free(rt->lists[i].items[j]);
        free(rt->lists[i].items);
    }
    free(rt->lists);
    for (size_t i=0;i<rt->matrices_count;i++) {
        free(rt->matrices[i].name);
        free(rt->matrices[i].cells);
    }
    free(rt->matrices);
    for (size_t i=0;i<rt->memory_count;i++) {
        free(rt->memory[i].name);
        free(rt->memory[i].cells);
    }
    free(rt->memory);
    for (size_t i=0;i<rt->cache_count;i++) {
        free(rt->cache[i].path);
        for (size_t j=0;j<rt->cache[i].count;j++) token_array_free(&rt->cache[i].lines[j].tokens);
        free(rt->cache[i].lines);
    }
    free(rt->cache);
    free(rt->graph_title);
    free(rt->graph_elements);
    free(rt);
}

const char* brise_last_error(const BriseRuntime* rt) {
    return rt ? rt->last_error : "runtime is null";
}

static FileCacheEntry* find_cache(BriseRuntime* rt, const char* path) {
    for (size_t i=0;i<rt->cache_count;i++) if (strcmp(rt->cache[i].path,path)==0) return &rt->cache[i];
    return NULL;
}

static int brise_execute_file_stream(BriseRuntime* rt, const char* filename, FILE* f) {
    if (!rt || !filename || !f) { if (f) fclose(f); return 0; }
    FileCacheEntry* cache = find_cache(rt, filename);
    if (!cache) {
        if (!ensure_capacity((void**)&rt->cache,&rt->cache_capacity,sizeof(FileCacheEntry),rt->cache_count+1)) {
            fclose(f); BriseErrorContext ectx = {0, filename}; set_error(rt, ectx, "Out of memory"); return 0;
        }
        cache = &rt->cache[rt->cache_count++];
        memset(cache,0,sizeof(*cache));
        cache->path = xstrdup(filename);
        if (!cache->path) {
            fclose(f); BriseErrorContext ectx = {0, filename}; set_error(rt, ectx, "Out of memory"); return 0;
        }
        char line[2048];
        size_t line_no = 0;
        while (fgets(line,sizeof(line),f)) {
            line_no++;
            char* tr = trim_copy(line);
            if (!tr) {
                fclose(f); BriseErrorContext ectx = {line_no, filename}; set_error(rt, ectx, "Out of memory"); return 0;
            }
            if (tr[0]=='\0' || (tr[0]=='/' && tr[1]=='/')) { free(tr); continue; }
            if (!ensure_capacity((void**)&cache->lines,&cache->capacity,sizeof(CachedLine),cache->count+1)) {
                free(tr); fclose(f); BriseErrorContext ectx = {line_no, filename}; set_error(rt, ectx, "Out of memory"); return 0;
            }
            cache->lines[cache->count].tokens = tokenize_line(tr);
            cache->lines[cache->count].line_number = line_no;
            cache->count++;
            free(tr);
        }
        if (ferror(f)) {
            fclose(f); BriseErrorContext ectx = {line_no, filename}; set_error(rt, ectx, "Could not read complete file"); return 0;
        }
        fclose(f);
    } else {
        fclose(f);
    }
    for (size_t i=0;i<cache->count;i++) {
        BriseErrorContext ctx = {cache->lines[i].line_number, cache->path};
        if (!execute_tokens(rt, &cache->lines[i].tokens, ctx)) return 0;
    }
    return 1;
}

int brise_execute_file(BriseRuntime* rt, const char* filename) {
    if (!rt || !filename) return 0;
    FILE* f=fopen(filename,"rb");
    if (!f) {
        BriseErrorContext ectx = {0, filename};
        set_error(rt, ectx, "Could not open file");
        return 0;
    }
    return brise_execute_file_stream(rt,filename,f);
}
