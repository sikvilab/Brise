#include "brise_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ERROR_LEN 512

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

    FileCacheEntry* cache;
    size_t cache_count;
    size_t cache_capacity;

    char last_error[MAX_ERROR_LEN];
};

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
    const char* k[] = {"say","set","calc","if","Include","List","Command","random","solve","query","read","write","Say","to","everyone","true","false"};
    size_t n = sizeof(k)/sizeof(k[0]);
    for (size_t i=0;i<n;++i) if (strcmp(k[i], w)==0) return 1;
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
            size_t s=i;
            while (i<n && (isdigit((unsigned char)line[i])||line[i]=='.')) i++;
            char* t = xstrndup(line+s, i-s);
            token_push(&out, TOK_NUMBER, t);
            free(t);
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
        if (strchr("+-*/", c)) { char op[2]={c,0}; token_push(&out,TOK_OPERATOR,op); i++; continue; }
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
    if (tk->type == TOK_KEYWORD && (strcmp(tk->text,"true")==0 || strcmp(tk->text,"false")==0)) {
        *out = value_bool(strcmp(tk->text,"true")==0); (*p)++; return 1;
    }
    if (tk->type == TOK_IDENTIFIER || tk->type == TOK_KEYWORD) {
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

static int parse_term(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (!parse_factor(rt,t,p,ctx,out)) return 0;
    while (t->items[*p].type == TOK_OPERATOR && (strcmp(t->items[*p].text,"*")==0 || strcmp(t->items[*p].text,"/")==0)) {
        char op = t->items[*p].text[0];
        (*p)++;
        BriseValue right;
        if (!parse_factor(rt,t,p,ctx,&right)) { free_value(out); return 0; }
        double l,r;
        if (!value_to_number(out,&l) || !value_to_number(&right,&r)) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Arithmetic expects numbers"); return 0;
        }
        if (op=='/' && r==0.0) { free_value(out); free_value(&right); set_error(rt,ctx,"Division by zero"); return 0; }
        free_value(out); free_value(&right);
        *out = value_number(op=='*' ? l*r : l/r);
    }
    return 1;
}

static int parse_expression(BriseRuntime* rt, BriseTokenArray* t, size_t* p, BriseErrorContext ctx, BriseValue* out) {
    if (!parse_term(rt,t,p,ctx,out)) return 0;
    while (t->items[*p].type == TOK_OPERATOR && (strcmp(t->items[*p].text,"+")==0 || strcmp(t->items[*p].text,"-")==0)) {
        char op = t->items[*p].text[0];
        (*p)++;
        BriseValue right;
        if (!parse_term(rt,t,p,ctx,&right)) { free_value(out); return 0; }

        if (op=='+' && (out->type==BRISE_VAL_STRING || right.type==BRISE_VAL_STRING)) {
            char* ls = value_to_string_heap(out);
            char* rs = value_to_string_heap(&right);
            size_t len = strlen(ls) + strlen(rs) + 1;
            char* merged = (char*)malloc(len);
            snprintf(merged, len, "%s%s", ls, rs);
            free(ls); free(rs); free_value(out); free_value(&right);
            *out = value_string(merged); free(merged);
            continue;
        }

        double l,r;
        if (!value_to_number(out,&l) || !value_to_number(&right,&r)) {
            free_value(out); free_value(&right); set_error(rt,ctx,"Arithmetic expects numbers"); return 0;
        }
        free_value(out); free_value(&right);
        *out = value_number(op=='+' ? l+r : l-r);
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

static int eval_condition(BriseRuntime* rt, BriseTokenArray* cond, BriseErrorContext ctx, int* result) {
    size_t cmp = (size_t)-1;
    for (size_t i=0;i<cond->count;i++) {
        if (cond->items[i].type==TOK_OPERATOR) {
            const char* op = cond->items[i].text;
            if (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,">")||!strcmp(op,"<")||!strcmp(op,">=")||!strcmp(op,"<=")) { cmp=i; break; }
        }
    }
    if (cmp==(size_t)-1) {
        size_t p=0; BriseValue v;
        if (!parse_expression(rt,cond,&p,ctx,&v)) return 0;
        *result = value_to_bool(&v);
        free_value(&v); return 1;
    }

    BriseTokenArray left = token_slice(cond,0,cmp);
    BriseTokenArray right = token_slice(cond,cmp+1,cond->count-1);
    size_t lp=0,rp=0; BriseValue lv,rv;
    int ok = parse_expression(rt,&left,&lp,ctx,&lv) && parse_expression(rt,&right,&rp,ctx,&rv);
    token_array_free(&left); token_array_free(&right);
    if (!ok) return 0;

    const char* op = cond->items[cmp].text;
    double ln,rn;
    if (value_to_number(&lv,&ln) && value_to_number(&rv,&rn)) {
        if (!strcmp(op,"==")) *result = (ln==rn);
        else if (!strcmp(op,"!=")) *result = (ln!=rn);
        else if (!strcmp(op,">")) *result = (ln>rn);
        else if (!strcmp(op,"<")) *result = (ln<rn);
        else if (!strcmp(op,">=")) *result = (ln>=rn);
        else *result = (ln<=rn);
    } else {
        char* ls=value_to_string_heap(&lv); char* rs=value_to_string_heap(&rv);
        int c = strcmp(ls,rs);
        if (!strcmp(op,"==")) *result = (c==0);
        else if (!strcmp(op,"!=")) *result = (c!=0);
        else if (!strcmp(op,">")) *result = (c>0);
        else if (!strcmp(op,"<")) *result = (c<0);
        else if (!strcmp(op,">=")) *result = (c>=0);
        else *result = (c<=0);
        free(ls); free(rs);
    }
    free_value(&lv); free_value(&rv);
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

static int execute_cmd_if(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp=(size_t)-1;
    for (size_t i=2;i<tokens->count;i++) if (tokens->items[i].type==TOK_LPAREN) { lp=i; break; }
    if (lp==(size_t)-1) { set_error(rt,ctx,"if requires action block"); return 0; }
    int ok=0; size_t rp=find_matching_rparen(rt,tokens,lp,ctx,&ok); if(!ok) return 0;
    BriseTokenArray cond = token_slice(tokens,2,lp);
    int pass=0;
    ok = eval_condition(rt,&cond,ctx,&pass); token_array_free(&cond); if(!ok) return 0;
    if (!pass) return 1;
    BriseTokenArray act = token_slice(tokens,lp+1,rp);
    ok = execute_tokens(rt,&act,ctx);
    token_array_free(&act);
    return ok;
}

static int execute_cmd_include(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count < 4 || tokens->items[2].type != TOK_STRING) { set_error(rt,ctx,"Include expects string file"); return 0; }
    char path[1024];
    const char* slash = strrchr(ctx.file, '/');
    if (!slash) slash = strrchr(ctx.file, '\\');
    if (slash) {
        size_t n = (size_t)(slash - ctx.file + 1);
        memcpy(path, ctx.file, n); path[n]=0;
        strncat(path, tokens->items[2].text, sizeof(path)-strlen(path)-1);
    } else {
        snprintf(path,sizeof(path),"%s",tokens->items[2].text);
    }
    return brise_execute_file(rt,path);
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
    if (mn > mx) { set_error(rt,ctx,"random min > max"); return 0; }
    int r = (int)mn + (rand() % ((int)mx - (int)mn + 1));
    return set_var(rt,"random",value_number((double)r));
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

static int execute_cmd_read(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    size_t lp = 2; const char* target = "read";
    if (tokens->count>3 && tokens->items[2].type==TOK_IDENTIFIER && tokens->items[3].type==TOK_LPAREN) { target=tokens->items[2].text; lp=3; }
    if (tokens->count<=lp || tokens->items[lp].type!=TOK_LPAREN) { set_error(rt,ctx,"read expects (...) path"); return 0; }
    int ok=0; size_t rp=find_matching_rparen(rt,tokens,lp,ctx,&ok); if(!ok) return 0;
    BriseTokenArray pexpr = token_slice(tokens,lp+1,rp);
    size_t p=0; BriseValue pv;
    if (!parse_expression(rt,&pexpr,&p,ctx,&pv)) { token_array_free(&pexpr); return 0; }
    token_array_free(&pexpr);
    char* rel = value_to_string_heap(&pv); free_value(&pv);

    char path[1024];
    const char* slash = strrchr(ctx.file, '/'); if(!slash) slash = strrchr(ctx.file, '\\');
    if (slash) { size_t k=(size_t)(slash-ctx.file+1); memcpy(path,ctx.file,k); path[k]=0; strncat(path,rel,sizeof(path)-strlen(path)-1);} else snprintf(path,sizeof(path),"%s",rel);
    free(rel);

    FILE* f = fopen(path,"rb");
    if (!f) { set_error(rt,ctx,"read cannot open file"); return 0; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char* data=(char*)malloc((size_t)sz+1);
    size_t rd = fread(data,1,(size_t)sz,f);
    data[rd]=0;
    fclose(f);
    int rc = set_var(rt,target,value_string(data));
    free(data);
    return rc;
}

static int execute_cmd_write(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count<6 || tokens->items[2].type!=TOK_LPAREN) { set_error(rt,ctx,"write expects (path, value)"); return 0; }
    int ok=0; size_t rp = find_matching_rparen(rt,tokens,2,ctx,&ok); if(!ok) return 0;
    size_t comma = rp;
    int depth=0;
    for (size_t i=3;i<rp;i++) {
        if (tokens->items[i].type==TOK_LPAREN) depth++;
        if (tokens->items[i].type==TOK_RPAREN) depth--;
        if (depth==0 && tokens->items[i].type==TOK_COMMA) { comma=i; break; }
    }
    if (comma==rp) { set_error(rt,ctx,"write expects comma"); return 0; }

    BriseTokenArray pexpr=token_slice(tokens,3,comma);
    BriseTokenArray vexpr=token_slice(tokens,comma+1,rp);
    size_t p1=0,p2=0; BriseValue pv,vv;
    if (!parse_expression(rt,&pexpr,&p1,ctx,&pv)) { token_array_free(&pexpr); token_array_free(&vexpr); return 0; }
    if (!parse_expression(rt,&vexpr,&p2,ctx,&vv)) { token_array_free(&pexpr); token_array_free(&vexpr); free_value(&pv); return 0; }
    token_array_free(&pexpr); token_array_free(&vexpr);

    char* rel = value_to_string_heap(&pv);
    char* txt = value_to_string_heap(&vv);
    free_value(&pv); free_value(&vv);

    char path[1024];
    const char* slash = strrchr(ctx.file, '/'); if(!slash) slash = strrchr(ctx.file, '\\');
    if (slash) { size_t k=(size_t)(slash-ctx.file+1); memcpy(path,ctx.file,k); path[k]=0; strncat(path,rel,sizeof(path)-strlen(path)-1);} else snprintf(path,sizeof(path),"%s",rel);
    free(rel);

    FILE* f = fopen(path,"wb");
    if (!f) { free(txt); set_error(rt,ctx,"write cannot open file"); return 0; }
    fwrite(txt,1,strlen(txt),f); fclose(f); free(txt);
    return 1;
}

static int execute_tokens(BriseRuntime* rt, BriseTokenArray* tokens, BriseErrorContext ctx) {
    if (tokens->count==0 || tokens->items[0].type==TOK_END) return 1;

    if (tokens->count>=4 && tokens->items[0].type==TOK_KEYWORD && strcmp(tokens->items[0].text,"Say")==0 &&
        tokens->items[1].type==TOK_KEYWORD && strcmp(tokens->items[1].text,"to")==0 &&
        tokens->items[2].type==TOK_KEYWORD && strcmp(tokens->items[2].text,"everyone")==0 && tokens->items[3].type==TOK_COLON) {
        return execute_cmd_loop(rt,tokens,ctx);
    }

    if (tokens->count>=3 && tokens->items[0].type==TOK_KEYWORD && tokens->items[1].type==TOK_COLON) {
        const char* cmd = tokens->items[0].text;
        if (!strcmp(cmd,"say")) return execute_cmd_say(rt,tokens,ctx);
        if (!strcmp(cmd,"set")) return execute_cmd_set(rt,tokens,ctx,0);
        if (!strcmp(cmd,"calc")) return execute_cmd_set(rt,tokens,ctx,1);
        if (!strcmp(cmd,"if")) return execute_cmd_if(rt,tokens,ctx);
        if (!strcmp(cmd,"Include")) return execute_cmd_include(rt,tokens,ctx);
        if (!strcmp(cmd,"List")) return execute_cmd_list(rt,tokens,ctx);
        if (!strcmp(cmd,"Command")) return execute_cmd_command(rt,tokens,ctx);
        if (!strcmp(cmd,"random")) return execute_cmd_random(rt,tokens,ctx);
        if (!strcmp(cmd,"solve")) return execute_cmd_solve(rt,tokens,ctx);
        if (!strcmp(cmd,"query")) return execute_cmd_query(rt,tokens,ctx);
        if (!strcmp(cmd,"read")) return execute_cmd_read(rt,tokens,ctx);
        if (!strcmp(cmd,"write")) return execute_cmd_write(rt,tokens,ctx);
        set_error(rt,ctx,"Unknown command keyword"); return 0;
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
    for (size_t i=0;i<rt->cache_count;i++) {
        free(rt->cache[i].path);
        for (size_t j=0;j<rt->cache[i].count;j++) token_array_free(&rt->cache[i].lines[j].tokens);
        free(rt->cache[i].lines);
    }
    free(rt->cache);
    free(rt);
}

const char* brise_last_error(const BriseRuntime* rt) {
    return rt ? rt->last_error : "runtime is null";
}

static FileCacheEntry* find_cache(BriseRuntime* rt, const char* path) {
    for (size_t i=0;i<rt->cache_count;i++) if (strcmp(rt->cache[i].path,path)==0) return &rt->cache[i];
    return NULL;
}

int brise_execute_file(BriseRuntime* rt, const char* filename) {
    if (!rt || !filename) return 0;

    FileCacheEntry* cache = find_cache(rt, filename);
    if (!cache) {
        FILE* f = fopen(filename,"rb");
        if (!f) {
            BriseErrorContext ectx = {0, filename};
            set_error(rt, ectx, "Could not open file");
            return 0;
        }

        if (!ensure_capacity((void**)&rt->cache,&rt->cache_capacity,sizeof(FileCacheEntry),rt->cache_count+1)) {
            fclose(f); BriseErrorContext ectx = {0, filename}; set_error(rt, ectx, "Out of memory"); return 0;
        }
        cache = &rt->cache[rt->cache_count++];
        memset(cache,0,sizeof(*cache));
        cache->path = xstrdup(filename);

        char line[2048];
        size_t line_no = 0;
        while (fgets(line,sizeof(line),f)) {
            line_no++;
            char* tr = trim_copy(line);
            if (tr[0]=='\0' || (tr[0]=='/' && tr[1]=='/')) { free(tr); continue; }

            if (!ensure_capacity((void**)&cache->lines,&cache->capacity,sizeof(CachedLine),cache->count+1)) {
                free(tr); fclose(f); BriseErrorContext ectx = {line_no, filename}; set_error(rt, ectx, "Out of memory"); return 0;
            }
            cache->lines[cache->count].tokens = tokenize_line(tr);
            cache->lines[cache->count].line_number = line_no;
            cache->count++;
            free(tr);
        }
        fclose(f);
    }

    for (size_t i=0;i<cache->count;i++) {
        BriseErrorContext ctx = {cache->lines[i].line_number, cache->path};
        if (!execute_tokens(rt, &cache->lines[i].tokens, ctx)) return 0;
    }
    return 1;
}
