/* date = May 2026
 *
 * qsharp.c  -  Tree-sitter Q# frontend + IR lowering + Q# emitter
 *
 * Pass 1 : qs_parse_file         -- read source, run Tree-sitter
 * Pass 2 : qs_print_tree /
 *           qs_print_named_nodes  -- debug printers
 * Pass 3 : qsharp_tree_to_ir     -- CST -> MQ_Program IR
 * Pass 4 : qsharp_emit           -- MQ_Program IR -> Q# source
 */

#include "qsharp.h"
#include "ir.h"
#include "../base/mem.h"
#include "../base/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_SNIPPET_MAX 64

/* ═══════════════════════════════════════════════════════════════════════════
 * Shared internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static char *read_file_raw(const char *path, u32 *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "[qsharp] cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t r = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[r] = '\0';
    *out_len = (u32)r;
    return buf;
}

static string node_text(M_Arena *arena, TSNode node, const char *src)
{
    u32 start = ts_node_start_byte(node);
    u32 end = ts_node_end_byte(node);
    u32 len = end - start;
    if (len == 0)
        return (string){0};
    u8 *buf = (u8 *)arena_alloc(arena, len + 1);
    memcpy(buf, src + start, len);
    buf[len] = '\0';
    return (string){.str = buf, .size = len};
}

static b8 node_is(TSNode n, const char *type)
{
    return strcmp(ts_node_type(n), type) == 0;
}

static TSNode find_child(TSNode parent, const char *type)
{
    u32 count = ts_node_named_child_count(parent);
    for (u32 i = 0; i < count; i++)
    {
        TSNode c = ts_node_named_child(parent, i);
        if (node_is(c, type))
            return c;
    }
    TSNode null_node;
    memset(&null_node, 0, sizeof(TSNode));
    return null_node;
}

static b8 node_null(TSNode n) { return ts_node_is_null(n); }

static void copy_snippet(char *dst, size_t dst_size,
                         const char *src, u32 src_len, u32 max_chars)
{
    while (src_len > 0 && (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n'))
    {
        src++;
        src_len--;
    }
    b8 trunc = (src_len > max_chars);
    u32 copy_len = trunc ? max_chars : src_len;
    if (copy_len >= (u32)dst_size - 4)
        copy_len = (u32)dst_size - 4;
    memcpy(dst, src, copy_len);
    if (trunc)
    {
        memcpy(dst + copy_len, "...", 3);
        dst[copy_len + 3] = '\0';
    }
    else
        dst[copy_len] = '\0';
    for (char *p = dst; *p; p++)
        if (*p == '\r' || *p == '\n')
            *p = ' ';
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pass 1 – parse
 * ═══════════════════════════════════════════════════════════════════════════ */

QS_ParseResult qs_parse_file(const char *path)
{
    QS_ParseResult result;
    memset(&result, 0, sizeof(result));

    result.source = read_file_raw(path, &result.source_len);
    if (!result.source)
        return result;

    result.parser = ts_parser_new();
    if (!result.parser)
    {
        fprintf(stderr, "[qsharp] ts_parser_new() failed\n");
        free(result.source);
        result.source = NULL;
        return result;
    }

    if (!ts_parser_set_language(result.parser, tree_sitter_qsharp()))
    {
        fprintf(stderr, "[qsharp] ts_parser_set_language() failed\n");
        ts_parser_delete(result.parser);
        free(result.source);
        result.parser = NULL;
        result.source = NULL;
        return result;
    }

    result.tree = ts_parser_parse_string(result.parser, NULL,
                                         result.source, result.source_len);
    if (!result.tree)
    {
        fprintf(stderr, "[qsharp] parse returned NULL\n");
        ts_parser_delete(result.parser);
        free(result.source);
        result.parser = NULL;
        result.source = NULL;
        return result;
    }

    TSNode root = ts_tree_root_node(result.tree);
    if (ts_node_has_error(root))
        fprintf(stderr, "[qsharp] warning: parse errors in '%s'\n", path);

    result.ok = true;
    return result;
}

void qs_parse_result_free(QS_ParseResult *result)
{
    if (!result)
        return;
    if (result->tree)
    {
        ts_tree_delete(result->tree);
        result->tree = NULL;
    }
    if (result->parser)
    {
        ts_parser_delete(result->parser);
        result->parser = NULL;
    }
    if (result->source)
    {
        free(result->source);
        result->source = NULL;
    }
    result->ok = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pass 2 – debug printers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_node(TSNode node, const char *source, int depth)
{
    const char *type = ts_node_type(node);
    b8 is_named = ts_node_is_named(node);
    b8 has_error = ts_node_has_error(node);
    u32 start_byte = ts_node_start_byte(node);
    u32 end_byte = ts_node_end_byte(node);
    u32 span = end_byte - start_byte;
    TSPoint sp = ts_node_start_point(node);
    TSPoint ep = ts_node_end_point(node);

    for (int i = 0; i < depth; i++)
        printf("  ");
    if (is_named)
        printf("\033[1;36m%s\033[0m", type);
    else
        printf("\033[0;33m\"%s\"\033[0m", type);
    printf(" \033[0;90m[%u:%u \xe2\x80\x93 %u:%u]\033[0m",
           sp.row + 1, sp.column, ep.row + 1, ep.column);
    if (has_error)
        printf(" \033[1;31m<ERROR>\033[0m");

    u32 child_count = ts_node_child_count(node);
    if (child_count == 0 && span > 0 && span < 200)
    {
        char snippet[QS_SNIPPET_MAX + 4];
        copy_snippet(snippet, sizeof(snippet), source + start_byte, span, QS_SNIPPET_MAX);
        printf("  \033[0;32m`%s`\033[0m", snippet);
    }
    printf("\n");
    for (u32 i = 0; i < child_count; i++)
        print_node(ts_node_child(node, i), source, depth + 1);
}

void qs_print_tree(const QS_ParseResult *result)
{
    if (!result || !result->ok)
        return;
    TSNode root = ts_tree_root_node(result->tree);
    printf("\n=== Q# Syntax Tree ===\n\n");
    print_node(root, result->source, 0);
    printf("\n");
}

void qs_print_named_nodes(const QS_ParseResult *result)
{
    if (!result || !result->ok)
        return;
    printf("\n=== Q# Named Nodes ===\n");
    printf("%-40s  %-8s  %s\n", "Node Type", "Line", "Source");
    printf("%-40s  %-8s  %s\n",
           "----------------------------------------",
           "--------",
           "----------------------------------------------");

    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(result->tree));
    b8 visited_children = false;
    while (true)
    {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (!visited_children && ts_node_is_named(node))
        {
            const char *type = ts_node_type(node);
            TSPoint sp = ts_node_start_point(node);
            u32 start = ts_node_start_byte(node);
            u32 span = ts_node_end_byte(node) - start;
            char snippet[QS_SNIPPET_MAX + 4] = "";
            if (span < 120)
                copy_snippet(snippet, sizeof(snippet), result->source + start, span, QS_SNIPPET_MAX);
            printf("%-40s  %4u:%-3u  %s%s\n",
                   type, sp.row + 1, sp.column, snippet,
                   ts_node_has_error(node) ? "  <ERROR>" : "");
        }
        if (!visited_children && ts_tree_cursor_goto_first_child(&cursor))
            visited_children = false;
        else if (ts_tree_cursor_goto_next_sibling(&cursor))
            visited_children = false;
        else if (ts_tree_cursor_goto_parent(&cursor))
            visited_children = true;
        else
            break;
    }
    ts_tree_cursor_delete(&cursor);
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pass 3 – tree -> IR
 * ═══════════════════════════════════════════════════════════════════════════ */

#define QS_MAX_REGS 64

typedef struct
{
    M_Arena *arena;
    const char *src;
    MQ_Program *prog;
    MQ_Circuit *circuit;
    u32 qubit_counter;
    struct
    {
        string name;
        u32 base_id;
        u32 size;
    } regs[QS_MAX_REGS];
    u32 reg_count;
} QS_Ctx;

static u32 ctx_resolve_qubit(QS_Ctx *ctx, string reg_name, u32 index)
{
    for (u32 i = 0; i < ctx->reg_count; i++)
    {
        if (str_eq(ctx->regs[i].name, reg_name))
            return ctx->regs[i].base_id + index;
    }
    return ctx->qubit_counter++;
}

static u32 ctx_add_register(QS_Ctx *ctx, string name, u32 size)
{
    u32 base = ctx->qubit_counter;
    ctx->qubit_counter += size;
    if (ctx->reg_count < QS_MAX_REGS)
    {
        ctx->regs[ctx->reg_count].name = name;
        ctx->regs[ctx->reg_count].base_id = base;
        ctx->regs[ctx->reg_count].size = size;
        ctx->reg_count++;
    }
    return base;
}

static MQ_GateType gate_from_name(const char *name)
{
    if (strcmp(name, "H") == 0)
        return MQ_Gate_H;
    if (strcmp(name, "X") == 0)
        return MQ_Gate_X;
    if (strcmp(name, "Y") == 0)
        return MQ_Gate_Y;
    if (strcmp(name, "Z") == 0)
        return MQ_Gate_Z;
    if (strcmp(name, "S") == 0)
        return MQ_Gate_S;
    if (strcmp(name, "T") == 0)
        return MQ_Gate_T;
    if (strcmp(name, "I") == 0)
        return MQ_Gate_I;
    if (strcmp(name, "SWAP") == 0)
        return MQ_Gate_SWAP;
    if (strcmp(name, "CNOT") == 0 || strcmp(name, "CX") == 0)
        return MQ_Gate_X;
    if (strcmp(name, "CCX") == 0 || strcmp(name, "CCNOT") == 0)
        return MQ_Gate_CCX;
    if (strcmp(name, "CSWAP") == 0)
        return MQ_Gate_CSWAP;
    if (strcmp(name, "R1") == 0)
        return MQ_Gate_P;
    if (strcmp(name, "Rx") == 0)
        return MQ_Gate_RX;
    if (strcmp(name, "Ry") == 0)
        return MQ_Gate_RY;
    if (strcmp(name, "Rz") == 0)
        return MQ_Gate_RZ;
    return MQ_Gate_Custom;
}

static MQ_Stmt *lower_stmt(QS_Ctx *ctx, TSNode node);
static MQ_Expr *lower_expr(QS_Ctx *ctx, TSNode node);
static MQ_Stmt *lower_block(QS_Ctx *ctx, TSNode block_node);
static MQ_Stmt *lower_call_expr(QS_Ctx *ctx, TSNode call_node);
/* Bit 31 of MQ_Type.width is used as a mutable flag when the type is stored
 * in a DeclClassical node.  It is never set by the IR construction helpers
 * (which use width values like 64, 0, register sizes — all well under 2^31).
 * The emitter must mask this bit off before using width numerically. */
#define QS_MUTABLE_FLAG (1u << 31)

static int infer_expr_type(MQ_Expr *e)
{
    if (!e) return MQ_Type_Int;

    switch (e->kind)
    {
        case MQ_Expr_BoolLit:  return MQ_Type_Bool;
        case MQ_Expr_IntLit:   return MQ_Type_Int;
        case MQ_Expr_FloatLit: return MQ_Type_Float;

        case MQ_Expr_Call:
        {
            if (!e->call.name.str) return MQ_Type_Int;
            const char *n = (const char *)e->call.name.str;
            /* Measurement functions return Int (Result → 0/1) */
            if (strcmp(n, "MResetZ")        == 0 ||
                strcmp(n, "MResetX")        == 0 ||
                strcmp(n, "MResetY")        == 0 ||
                strcmp(n, "M")              == 0 ||
                strcmp(n, "Measure")        == 0 ||
                strcmp(n, "MeasureInteger") == 0 ||
                strcmp(n, "ResultAsInt")    == 0)
                return MQ_Type_Int;
            /* Trig / math functions return Float */
            if (strcmp(n, "Sin")  == 0 || strcmp(n, "Cos")  == 0 ||
                strcmp(n, "Tan")  == 0 || strcmp(n, "Sqrt") == 0 ||
                strcmp(n, "Log")  == 0 || strcmp(n, "ArcTan2") == 0 ||
                strcmp(n, "IntAsDouble") == 0)
                return MQ_Type_Float;
            /* Boolean predicates */
            if (strcmp(n, "IsResultOne")  == 0 ||
                strcmp(n, "IsResultZero") == 0)
                return MQ_Type_Bool;
            return MQ_Type_Int;
        }

        case MQ_Expr_BinOp:
            if (e->bin.op >= MQ_BinOp_Eq && e->bin.op <= MQ_BinOp_Ge)
                return MQ_Type_Bool;
            if (e->bin.op == MQ_BinOp_LogAnd || e->bin.op == MQ_BinOp_LogOr)
                return MQ_Type_Bool;
            return infer_expr_type(e->bin.lhs);

        case MQ_Expr_UnOp:
            if (e->un.op == MQ_UnOp_Not) return MQ_Type_Bool;
            if (e->un.op >= MQ_UnOp_Sin) return MQ_Type_Float;
            return infer_expr_type(e->un.operand);

        case MQ_Expr_Var:
            if (e->name.str && strncmp((char *)e->name.str, "PI", 2) == 0)
                return MQ_Type_Float;
            return MQ_Type_Int;

        default:
            return MQ_Type_Int;
    }
}
static MQ_Expr *lower_expr(QS_Ctx *ctx, TSNode node)
{
    if (node_null(node))
        return NULL;
    const char *type = ts_node_type(node);

    if (strcmp(type, "integer_literal") == 0) {
        string txt = node_text(ctx->arena, node, ctx->src);
        i64 val = txt.str ? (i64)strtoll((char *)txt.str, NULL, 10) : 0;
        return mq_expr_int(ctx->arena, val);
    }

    if (strcmp(type, "float_literal") == 0) {
        string txt = node_text(ctx->arena, node, ctx->src);
        f64 val = txt.str ? strtod((char *)txt.str, NULL) : 0.0;
        return mq_expr_float(ctx->arena, val);
    }

    if (strcmp(type, "identifier") == 0)
        return mq_expr_var(ctx->arena, node_text(ctx->arena, node, ctx->src));

    if (strcmp(type, "index_expression") == 0) {
        TSNode base  = ts_node_named_child(node, 0);
        TSNode index = ts_node_named_child(node, 1);
        string reg_name = node_text(ctx->arena, base, ctx->src);
        MQ_Expr *idx_expr = lower_expr(ctx, index);
        return mq_expr_reg_index(ctx->arena, reg_name, idx_expr);
    }

    if (strcmp(type, "range_expression") == 0) {
    u32 nc = ts_node_named_child_count(node);
    TSNode start_node = ts_node_named_child(node, 0);
    TSNode end_node   = ts_node_named_child(node, nc - 1);
    MQ_Expr *start_e  = lower_expr(ctx, start_node);
    MQ_Expr *end_e    = lower_expr(ctx, end_node);
    /* Use range(start, end) call instead of BinOp_Shl sentinel */
    MQ_Expr *range_args[2] = { start_e, end_e };
    return mq_expr_call(ctx->arena, str_lit("range"), range_args, 2);
}

    if (strcmp(type, "binary_expression") == 0) {
        TSNode lhs_node = ts_node_named_child(node, 0);
        TSNode rhs_node = ts_node_named_child(node, 1);
        MQ_BinOp op = MQ_BinOp_Add;

        u32 total = ts_node_child_count(node);
        for (u32 i = 0; i < total; i++) {
            TSNode c = ts_node_child(node, i);
            if (ts_node_is_named(c)) continue;
            string tok = node_text(ctx->arena, c, ctx->src);
            if (!tok.str) continue;
            char *t = (char *)tok.str;
            if      (strcmp(t, "+")   == 0) op = MQ_BinOp_Add;
            else if (strcmp(t, "-")   == 0) op = MQ_BinOp_Sub;
            else if (strcmp(t, "*")   == 0) op = MQ_BinOp_Mul;
            else if (strcmp(t, "/")   == 0) op = MQ_BinOp_Div;
            else if (strcmp(t, "%")   == 0) op = MQ_BinOp_Mod;
            else if (strcmp(t, "^")   == 0) op = MQ_BinOp_Pow;
            else if (strcmp(t, "==")  == 0) op = MQ_BinOp_Eq;
            else if (strcmp(t, "!=")  == 0) op = MQ_BinOp_Ne;
            else if (strcmp(t, "<")   == 0) op = MQ_BinOp_Lt;
            else if (strcmp(t, "<=")  == 0) op = MQ_BinOp_Le;
            else if (strcmp(t, ">")   == 0) op = MQ_BinOp_Gt;
            else if (strcmp(t, ">=")  == 0) op = MQ_BinOp_Ge;
            else if (strcmp(t, "&&")  == 0) op = MQ_BinOp_LogAnd;
            else if (strcmp(t, "||")  == 0) op = MQ_BinOp_LogOr;
            else if (strcmp(t, "&&&") == 0) op = MQ_BinOp_And;
            else if (strcmp(t, "|||") == 0) op = MQ_BinOp_Or;
           
        }

        MQ_Expr *lhs = lower_expr(ctx, lhs_node);
        MQ_Expr *rhs = lower_expr(ctx, rhs_node);

        /* Q# has no implicit Int->Double coercion.
         * If one side of an arithmetic op is inherently Double
         * (Call, FloatLit, or the identifier "PI"), promote any
         * IntLit on the other side to FloatLit. */
        b8 is_arith = (op == MQ_BinOp_Add || op == MQ_BinOp_Sub ||
                       op == MQ_BinOp_Mul || op == MQ_BinOp_Div);

        #define IS_DOUBLE_EXPR(e) (                                        \
            (e) && ((e)->kind == MQ_Expr_Call                  ||          \
                    (e)->kind == MQ_Expr_FloatLit               ||          \
                    ((e)->kind == MQ_Expr_Var && (e)->name.str  &&          \
                     strncmp((char *)(e)->name.str, "PI", 2) == 0)))

        if (is_arith) {
            if (IS_DOUBLE_EXPR(lhs) && rhs && rhs->kind == MQ_Expr_IntLit)
                rhs = mq_expr_float(ctx->arena, (f64)rhs->lit.int_val);
            if (IS_DOUBLE_EXPR(rhs) && lhs && lhs->kind == MQ_Expr_IntLit)
                lhs = mq_expr_float(ctx->arena, (f64)lhs->lit.int_val);
        }

        #undef IS_DOUBLE_EXPR
        

        return mq_expr_binop(ctx->arena, op, lhs, rhs);
    }   /* ← closes binary_expression — was missing, causing nesting corruption */

    if (strcmp(type, "call_expression") == 0) {
        TSNode callee_node = ts_node_named_child(node, 0);
        string callee_name = node_text(ctx->arena, callee_node, ctx->src);
        u32 arg_count = ts_node_named_child_count(node) - 1;
        MQ_Expr **args = NULL;
        if (arg_count > 0) {
            args = (MQ_Expr **)arena_alloc(ctx->arena, sizeof(MQ_Expr *) * arg_count);
            for (u32 i = 0; i < arg_count; i++)
                args[i] = lower_expr(ctx, ts_node_named_child(node, i + 1));
        }
        return mq_expr_call(ctx->arena, callee_name, args, arg_count);
    }

    if (strcmp(type, "tuple_expression") == 0 || strcmp(type, "array_expression") == 0) {
        u32 n = ts_node_named_child_count(node);
        MQ_Expr **elems = (MQ_Expr **)arena_alloc(ctx->arena, sizeof(MQ_Expr *) * n);
        for (u32 i = 0; i < n; i++)
            elems[i] = lower_expr(ctx, ts_node_named_child(node, i));
        return mq_expr_array(ctx->arena, elems, n);
    }

    if (strcmp(type, "string_literal") == 0)
        return mq_expr_symbol(ctx->arena, node_text(ctx->arena, node, ctx->src));

    /* interpolated string: $"Measurement: [{r0}, {r1}, {r2}]"
     * Tree-sitter represents this as interpolated_string_expression with
     * string_interpolation children.  Grab the full raw source text and
     * store as a symbol so the emitter prints it verbatim. */
    if (strcmp(type, "interpolated_string_expression") == 0)
        return mq_expr_symbol(ctx->arena, node_text(ctx->arena, node, ctx->src));

    /* Fallback: treat as a variable reference */
    return mq_expr_var(ctx->arena, node_text(ctx->arena, node, ctx->src));
}
    static u32 resolve_qubit_node(QS_Ctx * ctx, TSNode node)
    {
        if (node_null(node))
            return 0;
        if (node_is(node, "index_expression"))
        {
            TSNode base = ts_node_named_child(node, 0);
            TSNode index = ts_node_named_child(node, 1);
            string reg_name = node_text(ctx->arena, base, ctx->src);
            u32 idx = 0;
            if (!node_null(index) && node_is(index, "integer_literal"))
            {
                string idx_txt = node_text(ctx->arena, index, ctx->src);
                if (idx_txt.str)
                    idx = (u32)strtoul((char *)idx_txt.str, NULL, 10);
            }
            return ctx_resolve_qubit(ctx, reg_name, idx);
        }
        if (node_is(node, "identifier"))
        {
            string name = node_text(ctx->arena, node, ctx->src);
            return ctx_resolve_qubit(ctx, name, 0);
        }
        return 0;
    }

    static MQ_Stmt *lower_gate_call(QS_Ctx * ctx, TSNode call_node,
                                    b8 is_controlled, u32 control_qubit,
                                    b8 is_adjoint)
    {
        TSNode callee_node = ts_node_named_child(call_node, 0);
        if (node_null(callee_node))
            return NULL;

        string callee_name = node_text(ctx->arena, callee_node, ctx->src);
// In lower_gate_call(), replace lines 599–613:

if (callee_name.str && (strcmp((char *)callee_name.str, "ApplyToEach")  == 0 ||
                        strcmp((char *)callee_name.str, "ApplyToEachA") == 0 ||
                        strcmp((char *)callee_name.str, "ApplyToEachC") == 0))
{
    /*
     * ApplyToEach(Gate, register)
     *
     * Arg 0 (node child 1): the gate name as an identifier  -- e.g. "H"
     * Arg 1 (node child 2): the register expression         -- e.g. "qs"
     *
     * Lower into:
     *   for _i in 0 .. reg_size - 1 {
     *       Gate(reg[_i]);
     *   }
     *
     * If we cannot resolve the register size statically (parameter context),
     * we fall back to a symbolic range  0 .. Length(reg)-1  stored as a
     * BinOp_Shl (the range sentinel already used by the range_expression
     * lowering path) so the emitter can still round-trip it to Q#.
     */

    u32 arg_node_count = ts_node_named_child_count(call_node) - 1;
    if (arg_node_count < 2)
        return mq_stmt_call(ctx->arena, callee_name, NULL, 0); /* malformed, bail */

    /* ── 1. Resolve the gate ── */
    TSNode gate_node = ts_node_named_child(call_node, 1);
    string gate_name = node_text(ctx->arena, gate_node, ctx->src);
    MQ_GateType gate  = gate_from_name((char *)gate_name.str);

    /* ── 2. Resolve the register ── */
    TSNode reg_node  = ts_node_named_child(call_node, 2);
    string reg_name  = node_text(ctx->arena, reg_node, ctx->src);

    /* Look up register size in the context table */
    u32 reg_size = 0;
    u32 reg_base = 0;
    b8  found    = false;
    for (u32 ri = 0; ri < ctx->reg_count; ri++)
    {
        if (str_eq(ctx->regs[ri].name, reg_name))
        {
            reg_size = ctx->regs[ri].size;
            reg_base = ctx->regs[ri].base_id;
            found    = true;
            break;
        }
    }

    /* ── 3. Build the loop iterable  0 .. (reg_size - 1) ── */
    MQ_Expr *range_start = mq_expr_int(ctx->arena, 0);
    MQ_Expr *range_end;

    if (found && reg_size > 0)
    {
        /* Static size known — emit a concrete integer range */
        range_end = mq_expr_int(ctx->arena, (i64)(reg_size - 1));
    }
    else
    {
        /*
         * Size not statically known (e.g. routine formal param).
         * Emit  Length(reg) - 1  as a BinOp_Sub expression so the Q#
         * emitter can reconstruct  0 .. Length(qs) - 1.
         */
        MQ_Expr *length_args[1];
        length_args[0] = mq_expr_var(ctx->arena, reg_name);
        MQ_Expr *len_call = mq_expr_call(ctx->arena, str_lit("Length"),
                                          length_args, 1);
        range_end = mq_expr_binop(ctx->arena, MQ_BinOp_Sub,
                                   len_call, mq_expr_int(ctx->arena, 1));
    }

    /* Range stored as BinOp_Shl — matches the range_expression sentinel */
    MQ_Expr *range = mq_expr_binop(ctx->arena, MQ_BinOp_Shl,
                                    range_start, range_end);

    /* ── 4. Build the loop body: Gate(reg[_i]) ── */
    string loop_var = str_lit("_i");

    /*
     * If the size is statically known, unroll gate instructions directly
     * into a flat block (reg[0], reg[1], ...) — cleaner IR for small regs.
     * Otherwise emit a single for-loop so the emitter can round-trip.
     */
    MQ_Stmt *body_stmt;

    if (found && reg_size > 0 && reg_size <= 16)
    {
        /* Unrolled block */
        MQ_Stmt **body_stmts = (MQ_Stmt **)arena_alloc(
            ctx->arena, sizeof(MQ_Stmt *) * reg_size);

        for (u32 qi = 0; qi < reg_size; qi++)
        {
            u32 qubit_id = reg_base + qi;
            MQ_Instruction instr;
            if (gate != MQ_Gate_Custom)
                instr = mq_instr_gate(gate, &qubit_id, 1);
            else
            {
                instr = mq_instr_gate_custom(gate_name, &qubit_id, 1, NULL, 0);
            }

            /* Handle ApplyToEachA (Adjoint) */
            MQ_Stmt *gs = mq_stmt_instr(ctx->arena, instr);
            if (strcmp((char *)callee_name.str, "ApplyToEachA") == 0)
                gs = mq_stmt_adjoint(ctx->arena, gs);
            body_stmts[qi] = gs;
        }
        body_stmt = mq_stmt_block(ctx->arena, body_stmts, reg_size);
    }
    else
    {
        /*
         * For large or unknown-size registers, emit a for loop with a
         * symbolic reg[_i] qubit reference in the body.
         * The gate instruction carries qubit_id = reg_base + 0 as a
         * placeholder; emitters that need the loop form will use the
         * for-stmt's var_name to reconstruct  reg[_i].
         *
         * Better approach for unknown size: emit mq_stmt_call with a
         * structured tag so smart emitters can still recognise it.
         * We use mq_stmt_for with a single-instruction body whose qubit
         * is the symbolic base_id (emitter must map via the loop var).
         */
        u32 placeholder_qubit = reg_base; /* will be overridden by loop var */
        MQ_Instruction instr;
        if (gate != MQ_Gate_Custom)
            instr = mq_instr_gate(gate, &placeholder_qubit, 1);
        else
            instr = mq_instr_gate_custom(gate_name, &placeholder_qubit, 1, NULL, 0);

        MQ_Stmt *gate_stmt = mq_stmt_instr(ctx->arena, instr);
        if (strcmp((char *)callee_name.str, "ApplyToEachA") == 0)
            gate_stmt = mq_stmt_adjoint(ctx->arena, gate_stmt);

        MQ_Stmt *loop_body = mq_stmt_block(ctx->arena, &gate_stmt, 1);
        body_stmt = mq_stmt_for(ctx->arena, loop_var,
                                 mq_type_int(ctx->arena, 64),
                                 range, loop_body);
    }

    return body_stmt;
}
        if (!callee_name.str)
            return NULL;

        /* CNOT/CX: first qubit arg is the control, second is the target */
        b8 is_cnot = (strcmp((char *)callee_name.str, "CNOT") == 0 ||
                      strcmp((char *)callee_name.str, "CX") == 0);

        MQ_GateType gate = gate_from_name((char *)callee_name.str);
        u32 arg_node_count = ts_node_named_child_count(call_node) - 1;

        u32 qubits[MQ_MAX_GATE_QUBITS] = {0};
        u8 qubit_count = 0;
        MQ_Expr *param_exprs[MQ_MAX_GATE_PARAMS] = {NULL};
        u8 param_count = 0;

        for (u32 i = 0; i < arg_node_count && i < 8; i++)
        {
            TSNode arg = ts_node_named_child(call_node, i + 1);
            const char *arg_type = ts_node_type(arg);

            if (strcmp(arg_type, "tuple_expression") == 0)
            {
                u32 tn = ts_node_named_child_count(arg);
                for (u32 j = 0; j < tn; j++)
                {
                    TSNode te = ts_node_named_child(arg, j);
                    const char *tet = ts_node_type(te);
                    if (strcmp(tet, "index_expression") == 0 || strcmp(tet, "identifier") == 0)
                    {
                        if (qubit_count < MQ_MAX_GATE_QUBITS)
                            qubits[qubit_count++] = resolve_qubit_node(ctx, te);
                    }
                    else
                    {
                        if (param_count < MQ_MAX_GATE_PARAMS)
                            param_exprs[param_count++] = lower_expr(ctx, te);
                    }
                }
                continue;
            }
            if (strcmp(arg_type, "array_expression") == 0)
            {
                u32 an = ts_node_named_child_count(arg);
                for (u32 j = 0; j < an; j++)
                {
                    TSNode ae = ts_node_named_child(arg, j);
                    if (qubit_count < MQ_MAX_GATE_QUBITS)
                        qubits[qubit_count++] = resolve_qubit_node(ctx, ae);
                }
                continue;
            }
            if (strcmp(arg_type, "index_expression") == 0 || strcmp(arg_type, "identifier") == 0)
            {
                if (qubit_count < MQ_MAX_GATE_QUBITS)
                    qubits[qubit_count++] = resolve_qubit_node(ctx, arg);
                continue;
            }
            if (param_count < MQ_MAX_GATE_PARAMS)
                param_exprs[param_count++] = lower_expr(ctx, arg);
        }

        MQ_Instruction instr;
        if (param_count > 0)
            instr = mq_instr_gate_sym(gate, qubits, qubit_count, param_exprs, param_count);
        else
            instr = mq_instr_gate(gate, qubits, qubit_count);

        if (gate == MQ_Gate_Custom)
            instr.gate.custom_name = callee_name;

        TSPoint sp = ts_node_start_point(call_node);
        instr.source_line = sp.row + 1;
        instr.source_col = sp.column;

        if (is_controlled)
        {
            mq_instr_add_control(&instr, control_qubit, 1);
        }
        else if (is_cnot && qubit_count >= 2)
        {
            /* qubits[0] = control, qubits[1] = target; re-pack */
            u32 ctrl = instr.qubits[0];
            instr.qubits[0] = instr.qubits[1];
            instr.qubit_count = 1;
            mq_instr_add_control(&instr, ctrl, 1);
        }

        MQ_Stmt *stmt = mq_stmt_instr(ctx->arena, instr);
        if (is_adjoint)
            stmt = mq_stmt_adjoint(ctx->arena, stmt);
        return stmt;
    }

    static MQ_Stmt *lower_call_expr(QS_Ctx *ctx, TSNode call_node)
{
    TSNode callee = ts_node_named_child(call_node, 0);
    if (node_null(callee))
        return NULL;

    /* ── Controlled / Adjoint: callee is a unary_expression ─────────────────
     *
     * Q# syntax:  Controlled Gate([ctrl0, ctrl1, ...], target)
     *             Adjoint Gate(target)
     * ──────────────────────────────────────────────────────────────────────*/
    if (node_is(callee, "unary_expression"))
    {
        b8 is_ctrl = false, is_adj = false;

        u32 total = ts_node_child_count(callee);
        for (u32 i = 0; i < total; i++)
        {
            TSNode c = ts_node_child(callee, i);
            if (ts_node_is_named(c))
                continue;
            string tok = node_text(ctx->arena, c, ctx->src);
            if (!tok.str)
                continue;
            if (strcmp((char *)tok.str, "Controlled") == 0)
                is_ctrl = true;
            if (strcmp((char *)tok.str, "Adjoint") == 0)
                is_adj = true;
        }

        /* Collect ALL control qubits from the array_expression argument.
         * Old code only grabbed index [0] — broke CCZ (Toffoli-phase).    */
        u32 ctrl_qubits[MQ_MAX_CONTROLS] = {0};
        u8  ctrl_count  = 0;

        if (is_ctrl)
        {
            u32 n = ts_node_named_child_count(call_node);
            for (u32 i = 1; i < n; i++)
            {
                TSNode arg = ts_node_named_child(call_node, i);
                if (node_is(arg, "array_expression"))
                {
                    u32 arr_len = ts_node_named_child_count(arg);
                    for (u32 j = 0; j < arr_len && ctrl_count < MQ_MAX_CONTROLS; j++)
                    {
                        ctrl_qubits[ctrl_count++] =
                            resolve_qubit_node(ctx, ts_node_named_child(arg, j));
                    }
                    break;
                }
            }
        }

        /* Gate name from first named child of the unary_expression */
        TSNode gate_id = ts_node_named_child(callee, 0);
        if (node_null(gate_id))
            return NULL;

        string gate_name = node_text(ctx->arena, gate_id, ctx->src);
        MQ_GateType gate = gate_from_name((char *)gate_name.str);

        u32      qubits[MQ_MAX_GATE_QUBITS] = {0};
        u8       qubit_count = 0;
        MQ_Expr *param_exprs[MQ_MAX_GATE_PARAMS] = {NULL};
        u8       param_count = 0;

        u32 n = ts_node_named_child_count(call_node);
        for (u32 i = 1; i < n; i++)
        {
            TSNode arg = ts_node_named_child(call_node, i);
            const char *at = ts_node_type(arg);

            if (strcmp(at, "array_expression") == 0)
            {
                /* Control array — already collected above, skip */
                continue;
            }
            if (strcmp(at, "tuple_expression") == 0)
            {
                u32 tn = ts_node_named_child_count(arg);
                for (u32 j = 0; j < tn; j++)
                {
                    TSNode te = ts_node_named_child(arg, j);
                    const char *tet = ts_node_type(te);
                    if (strcmp(tet, "index_expression") == 0 ||
                        strcmp(tet, "identifier")        == 0)
                    {
                        if (qubit_count < MQ_MAX_GATE_QUBITS)
                            qubits[qubit_count++] = resolve_qubit_node(ctx, te);
                    }
                    else
                    {
                        if (param_count < MQ_MAX_GATE_PARAMS)
                            param_exprs[param_count++] = lower_expr(ctx, te);
                    }
                }
                continue;
            }
            if (strcmp(at, "index_expression") == 0 ||
                strcmp(at, "identifier")        == 0)
            {
                if (qubit_count < MQ_MAX_GATE_QUBITS)
                    qubits[qubit_count++] = resolve_qubit_node(ctx, arg);
                continue;
            }
            if (param_count < MQ_MAX_GATE_PARAMS)
                param_exprs[param_count++] = lower_expr(ctx, arg);
        }

        MQ_Instruction instr;
        if (param_count > 0)
            instr = mq_instr_gate_sym(gate, qubits, qubit_count,
                                      param_exprs, param_count);
        else
            instr = mq_instr_gate(gate, qubits, qubit_count);

        if (gate == MQ_Gate_Custom)
            instr.gate.custom_name = gate_name;

        TSPoint spt = ts_node_start_point(call_node);
        instr.source_line = spt.row + 1;
        instr.source_col  = spt.column;

        /* Apply ALL collected control qubits */
        for (u8 ci = 0; ci < ctrl_count; ci++)
            mq_instr_add_control(&instr, ctrl_qubits[ci], 1);

        MQ_Stmt *stmt = mq_stmt_instr(ctx->arena, instr);
        if (is_adj)
            stmt = mq_stmt_adjoint(ctx->arena, stmt);
        return stmt;
    }

    /* ── Plain identifier callee ─────────────────────────────────────────── */
    if (node_is(callee, "identifier"))
    {
        string cname = node_text(ctx->arena, callee, ctx->src);
        if (!cname.str) return NULL;

        /* ── ApplyToEach / ApplyToEachA / ApplyToEachC ──────────────────────
         *
         * Always lowered to a for-loop — never unrolled.
         *
         *   ApplyToEach(Gate, reg)
         *   →
         *   for _i in 0..reg_size-1 { Gate(reg[_i]); }
         *
         * ────────────────────────────────────────────────────────────────── */
        b8 is_apply_each   = strcmp((char *)cname.str, "ApplyToEach")  == 0;
        b8 is_apply_each_a = strcmp((char *)cname.str, "ApplyToEachA") == 0;
        b8 is_apply_each_c = strcmp((char *)cname.str, "ApplyToEachC") == 0;

        if (is_apply_each || is_apply_each_a || is_apply_each_c)
        {
            u32 total_args = ts_node_named_child_count(call_node) - 1;
            if (total_args < 2)
                return mq_stmt_call(ctx->arena, cname, NULL, 0);

            /* 1. Gate */
            TSNode gate_node = ts_node_named_child(call_node, 1);
            string gate_name = node_text(ctx->arena, gate_node, ctx->src);
            MQ_GateType gate = gate_from_name((char *)gate_name.str);

            /* 2. Register */
            TSNode reg_node = ts_node_named_child(call_node, 2);
            string reg_name = node_text(ctx->arena, reg_node, ctx->src);

            u32 reg_size = 0, reg_base = 0;
            b8  found    = false;
            for (u32 ri = 0; ri < ctx->reg_count; ri++)
            {
                if (str_eq(ctx->regs[ri].name, reg_name))
                {
                    reg_size = ctx->regs[ri].size;
                    reg_base = ctx->regs[ri].base_id;
                    found    = true;
                    break;
                }
            }

            /* ── 3. Build range as range(0, reg_size-1) call expr ── */
            MQ_Expr *range_start = mq_expr_int(ctx->arena, 0);
            MQ_Expr *range_end;
            if (found && reg_size > 0)
            {
                range_end = mq_expr_int(ctx->arena, (i64)(reg_size - 1));
            }
            else
            {
                /* Symbolic: Length(reg) - 1 */
                MQ_Expr *len_arg  = mq_expr_var(ctx->arena, reg_name);
                MQ_Expr *len_call = mq_expr_call(ctx->arena, str_lit("Length"),
                                                  &len_arg, 1);
                range_end = mq_expr_binop(ctx->arena, MQ_BinOp_Sub,
                                           len_call, mq_expr_int(ctx->arena, 1));
            }
            MQ_Expr *range_args[2] = { range_start, range_end };
            MQ_Expr *range = mq_expr_call(ctx->arena, str_lit("range"), range_args, 2);

            /* 4. Loop body */
            string loop_var    = str_lit("_i");
            u32    placeholder = reg_base;

            MQ_Instruction instr = (gate != MQ_Gate_Custom)
                ? mq_instr_gate(gate, &placeholder, 1)
                : mq_instr_gate_custom(gate_name, &placeholder, 1, NULL, 0);

            MQ_Stmt *gate_stmt = mq_stmt_instr(ctx->arena, instr);
            if (is_apply_each_a)
                gate_stmt = mq_stmt_adjoint(ctx->arena, gate_stmt);

            MQ_Stmt *loop_body = mq_stmt_block(ctx->arena, &gate_stmt, 1);

            return mq_stmt_for(ctx->arena, loop_var,
                               mq_type_int(ctx->arena, 64),
                               range,
                               loop_body);
        }

        /* Not ApplyToEach and not a known gate → user-defined routine call */
        if (gate_from_name((char *)cname.str) == MQ_Gate_Custom)
        {
            u32 n = ts_node_named_child_count(call_node);
            u32 arg_count = n - 1;
            MQ_Expr **args = NULL;
            if (arg_count > 0)
            {
                args = (MQ_Expr **)arena_alloc(ctx->arena,
                                               sizeof(MQ_Expr *) * arg_count);
                for (u32 i = 0; i < arg_count; i++)
                    args[i] = lower_expr(ctx, ts_node_named_child(call_node, i + 1));
            }
            return mq_stmt_call(ctx->arena, cname, args, arg_count);
        }
    }

    /* Plain known-gate call */
    return lower_gate_call(ctx, call_node, false, 0, false);
}

    static MQ_Stmt *lower_stmt(QS_Ctx * ctx, TSNode node)
    {
        if (node_null(node))
            return NULL;
        const char *type = ts_node_type(node);
        TSPoint sp = ts_node_start_point(node);

        if (strcmp(type, "use_statement") == 0)
        {
            
            TSNode pat = find_child(node, "pattern");
            TSNode qinit = find_child(node, "qubit_init");

            string name = (string){0};
            if (!node_null(pat))
            {
                TSNode id = find_child(pat, "identifier");
                if (!node_null(id))
                    name = node_text(ctx->arena, id, ctx->src);
            }

            u32 count = 1;
            if (!node_null(qinit))
            {
                TSNode sz = find_child(qinit, "integer_literal");
                if (!node_null(sz))
                {
                    string sz_txt = node_text(ctx->arena, sz, ctx->src);
                    if (sz_txt.str)
                        count = (u32)strtoul((char *)sz_txt.str, NULL, 10);
                }
            }

            u32 base_id = ctx_add_register(ctx, name, count);

            // In the use_statement branch, replace the ctx->circuit block:
            if (ctx->circuit)
            {
                /* Only register once — check if this name is already registered */
                b8 already = false;
                for (u32 ri = 0; ri < ctx->circuit->register_count; ri++)
                {
                    if (str_eq(ctx->circuit->registers[ri].name, name))
                    {
                        already = true;
                        break;
                    }
                }
                if (!already)
                {
                    MQ_QubitMeta *meta = (MQ_QubitMeta *)arena_alloc(
                        ctx->arena, sizeof(MQ_QubitMeta) * count);
                    for (u32 i = 0; i < count; i++)
                    {
                        meta[i] = (MQ_QubitMeta){
                            .id = base_id + i,
                            .style = MQ_Qubit_Named,
                            .name = name,
                            .register_name = name,
                            .register_index = i,
                        };
                    }
                    u32 ri = ctx->circuit->register_count;
                    MQ_Register *new_regs = (MQ_Register *)arena_alloc(
                        ctx->arena, sizeof(MQ_Register) * (ri + 1));
                    if (ri > 0)
                        memcpy(new_regs, ctx->circuit->registers, sizeof(MQ_Register) * ri);
                    new_regs[ri] = mq_register_quantum(name, count, base_id, meta);
                    ctx->circuit->registers = new_regs;
                    ctx->circuit->register_count = ri + 1;
                    ctx->circuit->total_qubits += count;
                }
            }

            if (ctx->circuit)
                return NULL;

            MQ_Stmt *s = mq_stmt_decl_qubit(ctx->arena, name, count, base_id);
            s->source_line = sp.row + 1;
            s->source_col = sp.column;
            return s;
        }
        if (strcmp(type, "qubit_declaration_statement") == 0) {
    /* Delegate to the inner use_statement node */
    TSNode inner = ts_node_named_child(node, 0);
    if (!node_null(inner))
        return lower_stmt(ctx, inner);
    return NULL;
}

        if (strcmp(type, "expression_statement") == 0)
        {
            TSNode expr = ts_node_named_child(node, 0);
            if (node_null(expr))
                return NULL;

            const char *et = ts_node_type(expr);

            /* for/while/if wrapped as expression — unwrap and recurse */
            b8 is_ctrl = (strcmp(et, "for_statement")      == 0 ||
                          strcmp(et, "for_in_statement")    == 0 ||
                          strcmp(et, "iteration_statement") == 0 ||
                          strcmp(et, "for_expression")      == 0 ||
                          strcmp(et, "while_statement")     == 0 ||
                          strcmp(et, "while_expression")    == 0 ||
                          strcmp(et, "if_statement")        == 0 ||
                          strcmp(et, "if_expression")       == 0);
            if (is_ctrl)
                return lower_stmt(ctx, expr);

            if (strcmp(et, "call_expression") == 0)
                return lower_call_expr(ctx, expr);

            MQ_Expr *e = lower_expr(ctx, expr);
            if (e && e->kind == MQ_Expr_Call)
            {
                MQ_Stmt *s = mq_stmt_call(ctx->arena, e->call.name,
                                          e->call.args, e->call.arg_count);
                s->source_line = sp.row + 1;
                return s;
            }
            return NULL;
        }

        if (strcmp(type, "comment") == 0)
            return mq_stmt_comment(ctx->arena, node_text(ctx->arena, node, ctx->src));

        if (strcmp(type, "return_statement") == 0)
        {
            TSNode val_node = ts_node_named_child(node, 0);
            MQ_Stmt *s = mq_stmt_return(ctx->arena,
                                        node_null(val_node) ? NULL : lower_expr(ctx, val_node));
            s->source_line = sp.row + 1;
            return s;
        }

        if (strcmp(type, "if_statement") == 0 || strcmp(type, "if_expression") == 0)
        {
            TSNode cond_node = ts_node_named_child(node, 0);
            TSNode then_node = ts_node_named_child(node, 1);

            /* if_expression: then_node might be the block, or the grammar may
             * give a bare expression. If it's not a block, search all children. */
            if (!node_null(then_node) && strcmp(ts_node_type(then_node), "block") != 0)
            {
                u32 tc = ts_node_child_count(node);
                for (u32 _i = 0; _i < tc; _i++) {
                    TSNode c = ts_node_child(node, _i);
                    if (strcmp(ts_node_type(c), "block") == 0) {
                        then_node = c; break;
                    }
                }
            }

            MQ_Expr *cond = lower_expr(ctx, cond_node);
            MQ_Stmt *then_body = lower_block(ctx, then_node);

            TSNode else_node = find_child(node, "else_clause");
            if (!node_null(else_node))
            {
                MQ_Stmt *else_body = lower_block(ctx, ts_node_named_child(else_node, 0));
                MQ_IfBranch *branches = (MQ_IfBranch *)arena_alloc(
                    ctx->arena, sizeof(MQ_IfBranch) * 2);
                branches[0] = mq_if_branch(cond, then_body);
                branches[1] = mq_else_branch(else_body);
                MQ_Stmt *s = mq_stmt_if(ctx->arena, branches, 2);
                s->source_line = sp.row + 1;
                return s;
            }
            MQ_IfBranch *branch = (MQ_IfBranch *)arena_alloc(ctx->arena, sizeof(MQ_IfBranch));
            *branch = mq_if_branch(cond, then_body);
            MQ_Stmt *s = mq_stmt_if(ctx->arena, branch, 1);
            s->source_line = sp.row + 1;
            return s;
        }

        if (strcmp(type, "while_statement") == 0)
        {
            MQ_Stmt *s = mq_stmt_while(ctx->arena,
                                       lower_expr(ctx, ts_node_named_child(node, 0)),
                                       lower_block(ctx, ts_node_named_child(node, 1)));
            s->source_line = sp.row + 1;
            return s;
        }

       if (strcmp(type, "let_statement") == 0 || strcmp(type, "mutable_statement") == 0)
        {
            b8 is_mutable = (strcmp(type, "mutable_statement") == 0);

            TSNode pat = find_child(node, "pattern");
            string name = (string){0};
            if (!node_null(pat))
            {
                TSNode id = find_child(pat, "identifier");
                if (!node_null(id))
                    name = node_text(ctx->arena, id, ctx->src);
            }

            TSNode init = ts_node_named_child(node, ts_node_named_child_count(node) - 1);
            MQ_Expr *init_expr = lower_expr(ctx, init);

            /* Build a correctly-sized type for the inferred kind. */
            int resolved_kind = infer_expr_type(init_expr);
            MQ_Type *classical_type;
            switch (resolved_kind)
            {
                case MQ_Type_Float:
                case MQ_Type_Angle:
                    classical_type = mq_type_scalar(ctx->arena, MQ_Type_Float);
                    break;
                case MQ_Type_Bool:
                    classical_type = mq_type_scalar(ctx->arena, MQ_Type_Bool);
                    break;
                default: /* MQ_Type_Int and anything else */
                    classical_type = mq_type_int(ctx->arena, 64); /* Q# Int is 64-bit */
                    break;
            }

            /* Encode mutability in bit 31 of width so the emitter can read it
             * without any changes to ir.h / ir.c. */
            if (is_mutable)
                classical_type->width |= QS_MUTABLE_FLAG;

            MQ_Stmt *s = mq_stmt_decl_classical(ctx->arena, name, classical_type, init_expr);
            s->source_line = sp.row + 1;
            return s;
        }

        if (strcmp(type, "set_statement") == 0)
        {
            /* The set_statement node contains an assignment_expression as its first (and only) named child.
             * The assignment_expression has LHS and RHS as its named children. */
            TSNode assign_node = ts_node_named_child(node, 0);
            u32 assign_count = ts_node_named_child_count(assign_node);
            
            /* Extract LHS and RHS from the assignment expression */
            MQ_Expr *lhs = NULL;
            MQ_Expr *rhs = NULL;
            
            if (assign_count >= 2) {
                lhs = lower_expr(ctx, ts_node_named_child(assign_node, 0));
                rhs = lower_expr(ctx, ts_node_named_child(assign_node, 1));
            }
            
            MQ_Stmt *s = mq_stmt_set(ctx->arena, lhs, rhs);
            s->source_line = sp.row + 1;
            return s;
        }

        /* ── for loop ── cover every node-type name the Q# grammar may use ── */
        b8 is_for = (strcmp(type, "for_statement")       == 0 ||
                     strcmp(type, "for_in_statement")     == 0 ||
                     strcmp(type, "iteration_statement")  == 0 ||
                     strcmp(type, "for_expression")       == 0 ||
                     strcmp(type, "for")                  == 0);
        if (is_for)
        {
            /* Find the loop variable name from the first named child.
             * The grammar may call it: pattern, identifier, binding, etc. */
            TSNode pat_node  = ts_node_named_child(node, 0);
            TSNode iter_node = ts_node_named_child(node, 1);
            TSNode body_node = ts_node_named_child(node, 2);

            string var_name = str_lit("_");
            if (!node_null(pat_node)) {
                TSNode id = node_is(pat_node, "identifier")
                    ? pat_node : find_child(pat_node, "identifier");
                if (!node_null(id))
                    var_name = node_text(ctx->arena, id, ctx->src);
            }

            /* body: search all children if not found as named child 2 */
            if (node_null(body_node)) {
                u32 total = ts_node_child_count(node);
                for (u32 i = 0; i < total; i++) {
                    TSNode c = ts_node_child(node, i);
                    const char *ct = ts_node_type(c);
                    if (strcmp(ct, "block") == 0 ||
                        strcmp(ct, "block_expression") == 0) {
                        body_node = c; break;
                    }
                }
            }

            /* iter: search all children if not found as named child 1 */
            if (node_null(iter_node)) {
                u32 total = ts_node_child_count(node);
                for (u32 i = 0; i < total; i++) {
                    TSNode c = ts_node_child(node, i);
                    const char *ct = ts_node_type(c);
                    if (strcmp(ct, "range_expression")  == 0 ||
                        strcmp(ct, "identifier")         == 0 ||
                        strcmp(ct, "integer_literal")    == 0 ||
                        strcmp(ct, "call_expression")    == 0) {
                        iter_node = c; break;
                    }
                }
            }

            MQ_Stmt *s = mq_stmt_for(ctx->arena, var_name, NULL,
                                      lower_expr(ctx, iter_node),
                                      lower_block(ctx, body_node));
            s->source_line = sp.row + 1;
            return s;
        }

        return NULL;
    }

    static MQ_Stmt *lower_block(QS_Ctx * ctx, TSNode block_node)
    {
        if (node_null(block_node))
            return NULL;

        u32 total = ts_node_child_count(block_node);
        u32 named = ts_node_named_child_count(block_node);

        MQ_Stmt **stmts = (MQ_Stmt **)arena_alloc(
            ctx->arena, sizeof(MQ_Stmt *) * (total + 1));
        u32 count = 0;

        for (u32 i = 0; i < total; i++)
        {
            TSNode child = ts_node_child(block_node, i);

            /* Skip pure punctuation/delimiters that carry no semantic weight. */
            if (!ts_node_is_named(child)) {
                const char *t = ts_node_type(child);
                if (strcmp(t, "for")     != 0 &&
                    strcmp(t, "while")   != 0 &&
                    strcmp(t, "if")      != 0 &&
                    strcmp(t, "use")     != 0 &&
                    strcmp(t, "let")     != 0 &&
                    strcmp(t, "mutable") != 0 &&
                    strcmp(t, "set")     != 0 &&
                    strcmp(t, "return")  != 0)
                    continue;
            }

            MQ_Stmt *s = NULL;
            const char *ct = ts_node_type(child);

            if (strcmp(ct, "expression_statement") == 0)
            {
                TSNode expr = ts_node_named_child(child, 0);
                if (node_null(expr))
                {
                    s = lower_stmt(ctx, child);
                }
                else
                {
                    const char *et = ts_node_type(expr);
                    /* for/while/if may be wrapped in expression_statement */
                    b8 is_ctrl = (strcmp(et, "for_statement")      == 0 ||
                                  strcmp(et, "for_in_statement")    == 0 ||
                                  strcmp(et, "iteration_statement") == 0 ||
                                  strcmp(et, "for_expression")      == 0 ||
                                  strcmp(et, "while_statement")     == 0 ||
                                  strcmp(et, "while_expression")    == 0 ||
                                  strcmp(et, "if_statement")        == 0 ||
                                  strcmp(et, "if_expression")       == 0);
                    if (is_ctrl)
                        s = lower_stmt(ctx, expr);   /* unwrap and lower directly */
                    else if (strcmp(et, "call_expression") == 0)
                        s = lower_call_expr(ctx, expr);
                    else
                        s = lower_stmt(ctx, child);
                }
            }
            else
            {
                s = lower_stmt(ctx, child);
            }

            if (s)
                stmts[count++] = s;
        }
        return mq_stmt_block(ctx->arena, stmts, count);
    }

    static void lower_callable(QS_Ctx * ctx, TSNode decl_node)
    {
        /* Check for @EntryPoint attribute */
        b8 is_entry = false;
        u32 n = ts_node_named_child_count(decl_node);
        for (u32 i = 0; i < n; i++)
        {
            TSNode c = ts_node_named_child(decl_node, i);
            if (node_is(c, "attribute"))
            {
                TSNode poi = find_child(c, "path_or_identifier");
                if (!node_null(poi))
                {
                    string attr = node_text(ctx->arena, poi, ctx->src);
                    if (attr.str && strcmp((char *)attr.str, "EntryPoint") == 0)
                        is_entry = true;
                }
            }
        }

        TSNode name_node = find_child(decl_node, "identifier");
        string op_name = node_null(name_node)
                             ? str_lit("unnamed")
                             : node_text(ctx->arena, name_node, ctx->src);

        TSNode body_decl = find_child(decl_node, "callable_body");
        TSNode block = node_null(body_decl)
                           ? find_child(decl_node, "block")
                           : find_child(body_decl, "block");

        if (is_entry)
        {
            MQ_Circuit *circ = mq_circuit(ctx->arena, op_name, NULL, 0, 0, 0);
            ctx->circuit = circ;
            circ->body = lower_block(ctx, block);

            if (circ->total_qubits > 0)
            {
                circ->measure_map = (i32 *)arena_alloc(
                    ctx->arena, sizeof(i32) * circ->total_qubits);
                for (u32 qi = 0; qi < circ->total_qubits; qi++)
                    circ->measure_map[qi] = -1;
            }

            mq_program_add_circuit(ctx->arena, ctx->prog, circ);
            ctx->circuit = NULL;
        }
        else
        {
            /* Non-entry callable becomes a routine */
            TSNode param_list = find_child(decl_node, "param_list");
            u32 param_count = 0;
            MQ_FormalParam *params = NULL;

            if (!node_null(param_list))
            {
                u32 pn = ts_node_named_child_count(param_list);
                params = (MQ_FormalParam *)arena_alloc(
                    ctx->arena, sizeof(MQ_FormalParam) * pn);
                // AFTER:
                // REPLACE the param loop body:
                for (u32 i = 0; i < pn; i++)
                {
                    TSNode p = ts_node_named_child(param_list, i);
                    TSNode pid = find_child(p, "identifier");
                    string pname = node_null(pid)
                                       ? str_lit("param")
                                       : node_text(ctx->arena, pid, ctx->src);

                    MQ_Type *ptype = mq_type_scalar(ctx->arena, MQ_Type_Float);

                    /* Walk all children of the param node to find any type annotation */
                    u32 pc = ts_node_child_count(p);
                    for (u32 j = 0; j < pc; j++)
                    {
                        TSNode c = ts_node_child(p, j);
                        u32 start = ts_node_start_byte(c);
                        u32 end = ts_node_end_byte(c);
                        u32 len = end - start;
                        if (len == 0)
                            continue;
                        /* Read raw source text of this child */
                        char tmp[64] = {0};
                        if (len < sizeof(tmp))
                        {
                            memcpy(tmp, ctx->src + start, len);
                            tmp[len] = '\0';
                        }
                        /* A callable type annotation (e.g. "((Qubit[], Qubit) => Unit)")
                         * must be checked BEFORE the bare "Qubit" check, otherwise the
                         * Qubit[] inside the arrow type is mis-classified as QubitReg. */
                        if (strstr(tmp, "=>"))
                        {
                            /* Encode as MQ_Type_Void with a magic width so the emitter
                             * knows to print the Q# callable type string verbatim. */
                            ptype = mq_type_scalar(ctx->arena, MQ_Type_Void);
                            ptype->width = 0xCAFEu; /* callable sentinel */
                            break;
                        }
                        if (strstr(tmp, "Qubit[]") || strstr(tmp, "Qubit ["))
                        {
                            ptype = mq_type_reg(ctx->arena, MQ_Type_QubitReg, 0);
                            break;
                        }
                        else if (strstr(tmp, "Qubit"))
                        {
                            ptype = mq_type_scalar(ctx->arena, MQ_Type_Qubit);
                            break;
                        }
                    }
                    params[param_count++] = mq_formal_param(pname, ptype, NULL);
                }
            }

            MQ_Routine *routine = mq_routine(ctx->arena, op_name,
                                             MQ_Routine_Operation,
                                             params, param_count, NULL,
                                             lower_block(ctx, block));
            routine->source_line = ts_node_start_point(decl_node).row + 1;
            mq_program_add_routine(ctx->arena, ctx->prog, routine);
        }
    }

    MQ_Program *qsharp_tree_to_ir(const QS_ParseResult *result, M_Arena *arena)
    {
        if (!result || !result->ok)
            return NULL;

        TSNode root = ts_tree_root_node(result->tree);

        MQ_Program *prog = mq_program(arena, str_lit("QSharpProgram"), MQ_Lang_QSharp);
        prog->mq_ir_version[0] = MQ_IR_VERSION_MAJOR;
        prog->mq_ir_version[1] = MQ_IR_VERSION_MINOR;
        prog->mq_ir_version[2] = MQ_IR_VERSION_PATCH;

        QS_Ctx ctx = {
            .arena = arena,
            .src = result->source,
            .prog = prog,
            .circuit = NULL,
            .qubit_counter = 0,
            .reg_count = 0,
        };

        /* Find the namespace node */
        TSNode ns_node;
        memset(&ns_node, 0, sizeof(TSNode));
        u32 root_n = ts_node_named_child_count(root);
        for (u32 i = 0; i < root_n; i++)
        {
            TSNode c = ts_node_named_child(root, i);
            if (node_is(c, "namespace"))
            {
                ns_node = c;
                break;
            }
        }

        if (node_null(ns_node))
        {
            fprintf(stderr, "[qsharp] no namespace found\n");
            return prog;
        }

        /*
         * Walk namespace children.
         * open_decl     -> skip entirely (not represented in IR)
         * callable_decl -> lower to circuit or routine
         */
        u32 ns_n = ts_node_named_child_count(ns_node);
        for (u32 i = 0; i < ns_n; i++)
        {
            TSNode child = ts_node_named_child(ns_node, i);
            if (node_is(child, "open_decl"))
                continue; /* skip imports */
            if (node_is(child, "callable_decl"))
                lower_callable(&ctx, child);
        }

        return prog;
    }

    /* ═══════════════════════════════════════════════════════════════════════════
     * Pass 4 – IR -> Q# emitter
     * ═══════════════════════════════════════════════════════════════════════════ */

    /* =============================================================================
     * qsharp_emit_new.c
     *
     * Drop-in replacement for Pass 4 (qsharp_emit + helpers) in qsharp.c.
     *
     * Replace everything from the comment
     *   "Pass 4 – IR -> Q# emitter"
     * to the end of the file with this content.
     *
     * Design decisions
     * ────────────────
     * 1. Expressions carry type context explicitly via `emit_expr_as_double`.
     *    Any IntLit that appears where a Double is needed is printed as "N.0".
     *    This is the ONLY correct fix — patching at parse time is fragile because
     *    the MQL frontend also builds the same IR for programs like Grover.
     *
     * 2. Gate parameters are ALWAYS Double in Q#. Every param position in every
     *    gate call uses emit_expr_as_double().
     *
     * 3. Routine params: type is read from the MQ_FormalParam.type field.
     *    QubitReg → "Qubit[]", Qubit → "Qubit", anything else → "Double".
     *
     * 4. Controlled gates: Q# syntax is
     *      Controlled GateName([ctrl1, ctrl2, ...], (param, target))
     *    The inner tuple is (params..., target_qubit).
     *    For multi-target gates (SWAP, etc.) the tuple is (tgt0, tgt1, ...).
     *
     * 5. mutable_statement vs let_statement: MQ_Stmt_DeclClassical always
     *    emits "mutable" because measurement results must be re-assignable.
     *
     * 6. MResetZ is used instead of M to automatically reset qubits, matching
     *    idiomatic Q# for measurement + reset patterns.
     * =============================================================================
     */

    /* ── indent helper ─────────────────────────────────────────────────────────── */

    static void qs_indent(FILE * out, u32 level)
    {
        for (u32 i = 0; i < level; i++)
            fprintf(out, "    ");
    }

    /* ── expression emitter ────────────────────────────────────────────────────── */

    /* Forward declaration (mutual recursion) */
    static void qs_emit_expr(FILE * out, MQ_Expr * e, b8 want_double);

    /*
     * Emit a single expression node.
     * want_double: if true, any IntLit is printed as "N.0" (Q# Double literal).
     */
    static void qs_emit_qubit(FILE *out, u32 qubit_id);  /* forward decl */
static void qs_emit_expr(FILE * out, MQ_Expr * e, b8 want_double)
    {
        if (!e)
        {
            fprintf(out, "0");
            return;
        }

        switch (e->kind)
        {

        case MQ_Expr_BoolLit:
            fprintf(out, "%s", e->lit.bool_val ? "true" : "false");
            break;

        case MQ_Expr_IntLit:
            if (want_double)
                /* Q# has no implicit Int->Double; force a Double literal */
                fprintf(out, "%lld.0", (long long)e->lit.int_val);
            else
                fprintf(out, "%lld", (long long)e->lit.int_val);
            break;

        case MQ_Expr_FloatLit:
        {
            /* Always print as a proper Double literal Q# can parse.
             * %g can produce "2" for 2.0 – avoid that. */
            double v = e->lit.float_val;
            if (v == (double)(long long)v)
                fprintf(out, "%.1f", v); /* 2.0, 3.0, … */
            else
                fprintf(out, "%.17g", v); /* full precision for irrationals */
            break;
        }

        case MQ_Expr_Symbol:
        case MQ_Expr_Var:
            /* Translate "PI" → "Msk.PI()" */
            if (e->name.size == 2 && e->name.str &&
                strncmp((char *)e->name.str, "PI", 2) == 0)
                fprintf(out, "Msk.PI()");
            else
                fprintf(out, "%.*s", (int)e->name.size, e->name.str);
            break;

        case MQ_Expr_QubitRef:
            qs_emit_qubit(out, e->qubit_id);
            break;

        case MQ_Expr_RegIndex:
            fprintf(out, "%.*s[", (int)e->reg.name.size, e->reg.name.str);
            qs_emit_expr(out, e->reg.index_expr, false);
            fprintf(out, "]");
            break;

        case MQ_Expr_BinOp:
        {
            /*
             * Determine whether children need Double context.
             *
             * Rule: for arithmetic ops (+,-,*,/), if EITHER child is
             * "inherently Double" (FloatLit, a Call, or a BinOp that itself
             * is floating), propagate want_double=true to BOTH children.
             * This makes "PI() / 2" → "(Msk.PI() / 2.0)" naturally.
             */
            /* Range expression: stored as BinOp_Shl sentinel */
            if (e->bin.op == MQ_BinOp_Shl)
            {
                qs_emit_expr(out, e->bin.lhs, false);
                fprintf(out, "..");
                qs_emit_expr(out, e->bin.rhs, false);
                break;
            }
            static const char *ops[] = {
                "+", "-", "*", "/", "%", "^",
                "&&&", "|||", "^^^", "<<<", ">>>",
                "==", "!=", "<", "<=", ">", ">=",
                "and", "or"};

            b8 is_arith = (e->bin.op == MQ_BinOp_Add ||
                           e->bin.op == MQ_BinOp_Sub ||
                           e->bin.op == MQ_BinOp_Mul ||
                           e->bin.op == MQ_BinOp_Div);

/* Helper: is this expr node inherently floating-point? */
#define EXPR_IS_FLOAT(x) (                                \
    (x) && ((x)->kind == MQ_Expr_FloatLit ||              \
            (x)->kind == MQ_Expr_Call ||                  \
            ((x)->kind == MQ_Expr_Var && (x)->name.str && \
             strncmp((char *)(x)->name.str, "PI", 2) == 0)))

            b8 child_want_double = want_double;
            if (is_arith && (EXPR_IS_FLOAT(e->bin.lhs) || EXPR_IS_FLOAT(e->bin.rhs)))
                child_want_double = true;

#undef EXPR_IS_FLOAT

            fprintf(out, "(");
            qs_emit_expr(out, e->bin.lhs, child_want_double);
            fprintf(out, " %s ", ops[e->bin.op]);
            qs_emit_expr(out, e->bin.rhs, child_want_double);
            fprintf(out, ")");
            break;
        }

        case MQ_Expr_UnOp:
        {
            static const char *math_fns[] = {
                /* MQ_UnOp_Neg=0, Not=1, BitNot=2 handled below */
                "", "", "",
                "Msk.Sin", "Msk.Cos", "Msk.Tan",
                "Msk.Asin", "Msk.Acos", "Msk.Atan",
                "Msk.Sqrt", "Msk.Exp", "Msk.Log", "Msk.Abs"};
            if (e->un.op == MQ_UnOp_Neg)
            {
                fprintf(out, "-");
                qs_emit_expr(out, e->un.operand, want_double);
            }
            else if (e->un.op == MQ_UnOp_Not)
            {
                fprintf(out, "not ");
                qs_emit_expr(out, e->un.operand, false);
            }
            else if (e->un.op == MQ_UnOp_BitNot)
            {
                fprintf(out, "~~~");
                qs_emit_expr(out, e->un.operand, false);
            }
            else
            {
                u32 idx = (u32)e->un.op;
                fprintf(out, "%s(", math_fns[idx]);
                qs_emit_expr(out, e->un.operand, true); /* math fns take Double */
                fprintf(out, ")");
            }
            break;
        }

        case MQ_Expr_Call:
{
            /* range(start, end) → Q# syntax: start..end */
            if (e->call.name.size == 5 && e->call.name.str &&
                strncmp((char *)e->call.name.str, "range", 5) == 0 &&
                e->call.arg_count == 2)
            {
                qs_emit_expr(out, e->call.args[0], false);
                fprintf(out, "..");
                qs_emit_expr(out, e->call.args[1], false);
                break;
            }
            /* PI → Msk.PI() */
            if (e->call.name.size == 2 && e->call.name.str &&
                strncmp((char *)e->call.name.str, "PI", 2) == 0)
            {
                fprintf(out, "Msk.PI()");
                break;
            }
            fprintf(out, "%.*s(", (int)e->call.name.size, e->call.name.str);
            for (u32 i = 0; i < e->call.arg_count; i++)
            {
                if (i > 0) fprintf(out, ", ");
                qs_emit_expr(out, e->call.args[i], want_double);
            }
            fprintf(out, ")");
            break;
}

        case MQ_Expr_Array:
        {
            fprintf(out, "[");
            for (u32 i = 0; i < e->call.arg_count; i++)
            {
                if (i > 0)
                    fprintf(out, ", ");
                qs_emit_expr(out, e->call.args[i], false);
            }
            fprintf(out, "]");
            break;
        }

        case MQ_Expr_BitRead:
            fprintf(out, "%.*s[%u]",
                    (int)e->bit.reg_name.size, e->bit.reg_name.str,
                    e->bit.index);
            break;

        default:
            fprintf(out, "/* unknown_expr */");
            break;
        }
    }

    /* Convenience wrappers */
    static void qs_emit_expr_double(FILE * out, MQ_Expr * e) { qs_emit_expr(out, e, true); }
    static void qs_emit_expr_any(FILE * out, MQ_Expr * e) { qs_emit_expr(out, e, false); }

    /* ── one gate param (always Double context) ───────────────────────────────── */

    /* ── qubit-id → name table for the current scope ────────────────────────── *
     *
     * Each entry maps a contiguous range [base, base+size) to a named variable.
     * size == 0 means a single Qubit param (emitted bare, no index suffix).
     * size >  0 means a Qubit[] (emitted as name[id - base]).
     *
     * Entries are filled in two ways:
     *   - MQ_Stmt_DeclQubit (use statements in the body)
     *   - qsharp_emit priming from formal params before entering a routine body
     *
     * Lookup priority: single-qubit entries (size==0) first, then array entries.
     * ─────────────────────────────────────────────────────────────────────────── */

    #define QS_QMAP_MAX 16

    typedef struct { u32 base; u32 size; char name[64]; } QS_QMapEntry;

    static QS_QMapEntry s_qmap[QS_QMAP_MAX];
    static u32          s_qmap_count = 0;

    /* Reset the qubit map to empty */
    static void qmap_reset(void)  { s_qmap_count = 0; }

    /* Register a named range (size>0 → array, size==0 → bare single qubit) */
    static void qmap_add(u32 base, u32 size, const u8 *name_str, u32 name_len)
    {
        if (s_qmap_count >= QS_QMAP_MAX) return;
        QS_QMapEntry *e = &s_qmap[s_qmap_count++];
        e->base = base;
        e->size = size;
        u32 nlen = name_len < 63 ? name_len : 63;
        memcpy(e->name, name_str, nlen);
        e->name[nlen] = '\0';
    }

    static void qs_emit_qubit(FILE *out, u32 qubit_id)
    {
        /* Single-qubit entries first (size==0 means exact match) */
        for (u32 i = 0; i < s_qmap_count; i++)
        {
            QS_QMapEntry *e = &s_qmap[i];
            if (e->size == 0 && e->base == qubit_id)
            {
                fprintf(out, "%s", e->name);
                return;
            }
        }
        /* Array entries */
        for (u32 i = 0; i < s_qmap_count; i++)
        {
            QS_QMapEntry *e = &s_qmap[i];
            if (e->size > 0 && qubit_id >= e->base && qubit_id < e->base + e->size)
            {
                fprintf(out, "%s[%u]", e->name, qubit_id - e->base);
                return;
            }
        }
        /* Fallback: should not happen in well-formed IR */
        fprintf(out, "qs[%u]", qubit_id);
    }

    /* Legacy alias used by ResetAll / ApplyToEach helpers that need the
     * array-reg name as a plain string.  Return the first array entry's name. */
    static const char *qmap_array_name(void)
    {
        for (u32 i = 0; i < s_qmap_count; i++)
            if (s_qmap[i].size > 0) return s_qmap[i].name;
        return "qs";
    }
    static u32 qmap_array_base(void)
    {
        for (u32 i = 0; i < s_qmap_count; i++)
            if (s_qmap[i].size > 0) return s_qmap[i].base;
        return 0;
    }

    static void qs_emit_gate_param(FILE * out, MQ_Instruction * in, u8 pi)
    {
        if (in->gate.params_symbolic)
        {
            qs_emit_expr_double(out, in->gate.param_exprs[pi]);
        }
        else
        {
            double v = in->gate.params[pi];
            if (v == (double)(long long)v)
                fprintf(out, "%.1f", v);
            else
                fprintf(out, "%.17g", v);
        }
    }

    /* ── gate name string ─────────────────────────────────────────────────────── */

    static const char *qs_gate_name(MQ_GateType g)
    {
        switch (g)
        {
        case MQ_Gate_I:
            return "I";
        case MQ_Gate_H:
            return "H";
        case MQ_Gate_X:
            return "X";
        case MQ_Gate_Y:
            return "Y";
        case MQ_Gate_Z:
            return "Z";
        case MQ_Gate_S:
            return "S";
        case MQ_Gate_Sdg:
            return "Adjoint S";
        case MQ_Gate_T:
            return "T";
        case MQ_Gate_Tdg:
            return "Adjoint T";
        case MQ_Gate_P:
            return "R1";
        case MQ_Gate_RX:
            return "Rx";
        case MQ_Gate_RY:
            return "Ry";
        case MQ_Gate_RZ:
            return "Rz";
        case MQ_Gate_U:
            return "U";
        case MQ_Gate_SWAP:
            return "SWAP";
        case MQ_Gate_ISWAP:
            return "ISWAP";
        case MQ_Gate_RZZ:
            return "Rzz";
        case MQ_Gate_RXX:
            return "Rxx";
        case MQ_Gate_RYY:
            return "Ryy";
        case MQ_Gate_CCX:
            return "CCNOT";
        case MQ_Gate_CSWAP:
            return "CSWAP";
        default:
            return "CustomGate";
        }
    }

    /* ── emit one MQ_Instruction as Q# ───────────────────────────────────────── */

    static void qs_emit_instr(FILE * out, MQ_Instruction * in, u32 level)
    {
        switch (in->type)
        {

        case MQ_Instr_Gate:
        {
            /* Special custom gates ---------------------------------------- */
            if (in->gate.gate == MQ_Gate_Custom)
            {
                const char *cn = (char *)in->gate.custom_name.str;
                u32 cs = (u32)in->gate.custom_name.size;

                if (cs == 7 && strncmp(cn, "Message", 7) == 0)
                {
                    qs_indent(out, level);
                    fprintf(out, "Message(");
                    if (in->gate.params_symbolic && in->gate.param_count > 0)
                        qs_emit_expr_any(out, in->gate.param_exprs[0]);
                    else
                        fprintf(out, "\"\"");
                    fprintf(out, ");\n");
                    return;
                }
                if (cs == 11 && strncmp(cn, "DumpMachine", 11) == 0)
                {
                    qs_indent(out, level);
                    fprintf(out, "DumpMachine();\n");
                    return;
                }
                if (cs == 8 && strncmp(cn, "ResetAll", 8) == 0)
                {
                    qs_indent(out, level);
                    fprintf(out, "ResetAll(%s);\n", qmap_array_name());
                    return;
                }
                if (cs == 10 && strncmp(cn, "ApplyToEach", 10) == 0)
                {
                    /* ApplyToEach(gate, register) */
                    qs_indent(out, level);
                    fprintf(out, "ApplyToEach(");
                    if (in->gate.params_symbolic && in->gate.param_count > 0)
                    {
                        qs_emit_expr_any(out, in->gate.param_exprs[0]);
                        fprintf(out, ", ");
                    }
                    /* remaining qubits as array */
                    fprintf(out, "%s[%u .. %u]", qmap_array_name(), in->qubits[0] - qmap_array_base(), in->qubits[in->qubit_count - 1] - qmap_array_base());
                    fprintf(out, ");\n");
                    return;
                }

                /* Generic custom gate call: name([ctrls], (params, targets)) */
                qs_indent(out, level);
                if (in->gate.control_count > 0)
                {
                    fprintf(out, "Controlled %.*s([", cs, cn);
                    for (u8 ci = 0; ci < in->gate.control_count; ci++)
                    {
                        if (ci > 0)
                            fprintf(out, ", ");
                        qs_emit_qubit(out, in->gate.controls[ci]);
                    }
                    fprintf(out, "], (");
                    /* params then target */
                    for (u8 pi = 0; pi < in->gate.param_count; pi++)
                    {
                        qs_emit_gate_param(out, in, pi);
                        fprintf(out, ", ");
                    }
                    for (u8 qi = 0; qi < in->qubit_count; qi++)
                    {
                        if (qi > 0)
                            fprintf(out, ", ");
                        qs_emit_qubit(out, in->qubits[qi]);
                    }
                    fprintf(out, "));\n");
                }
                else
                {
                    fprintf(out, "%.*s(", cs, cn);
                    for (u8 pi = 0; pi < in->gate.param_count; pi++)
                    {
                        if (pi > 0)
                            fprintf(out, ", ");
                        qs_emit_gate_param(out, in, pi);
                    }
                    if (in->gate.param_count > 0 && in->qubit_count > 0)
                        fprintf(out, ", ");
                    for (u8 qi = 0; qi < in->qubit_count; qi++)
                    {
                        if (qi > 0)
                            fprintf(out, ", ");
                        qs_emit_qubit(out, in->qubits[qi]);
                    }
                    fprintf(out, ");\n");
                }
                return;
            }

            /* Standard gate ------------------------------------------------ */
            qs_indent(out, level);
            const char *gname = qs_gate_name(in->gate.gate);

            if (in->gate.control_count > 0)
            {
                /*
                 * Controlled form:
                 *   Controlled GateName([ctrl,...], (param..., target))
                 *
                 * For parametric gates (R1, Rx, Ry, Rz, U):
                 *   Controlled R1([qs[c]], (angle, qs[t]))
                 *
                 * For non-parametric gates (H, X, Z, SWAP, …):
                 *   Controlled H([qs[c]], qs[t])          -- single target
                 *   Controlled SWAP([qs[c]], (qs[t0], qs[t1])) -- multi-target
                 */
                fprintf(out, "Controlled %s([", gname);
                for (u8 ci = 0; ci < in->gate.control_count; ci++)
                {
                    if (ci > 0)
                        fprintf(out, ", ");
                    qs_emit_qubit(out, in->gate.controls[ci]);
                }
                fprintf(out, "], ");

                b8 need_tuple = (in->gate.param_count > 0) || (in->qubit_count > 1);
                if (need_tuple)
                    fprintf(out, "(");

                for (u8 pi = 0; pi < in->gate.param_count; pi++)
                {
                    qs_emit_gate_param(out, in, pi);
                    fprintf(out, ", ");
                }
                for (u8 qi = 0; qi < in->qubit_count; qi++)
                {
                    if (qi > 0)
                        fprintf(out, ", ");
                    qs_emit_qubit(out, in->qubits[qi]);
                }

                if (need_tuple)
                    fprintf(out, ")");
                fprintf(out, ");\n");
            }
            else
            {
                /*
                 * Uncontrolled form:
                 *   GateName(param, qs[t])   -- parametric
                 *   GateName(qs[t])           -- non-parametric
                 *   SWAP(qs[t0], qs[t1])      -- two-qubit
                 */
                fprintf(out, "%s(", gname);
                for (u8 pi = 0; pi < in->gate.param_count; pi++)
                {
                    if (pi > 0)
                        fprintf(out, ", ");
                    qs_emit_gate_param(out, in, pi);
                }
                if (in->gate.param_count > 0 && in->qubit_count > 0)
                    fprintf(out, ", ");
                for (u8 qi = 0; qi < in->qubit_count; qi++)
                {
                    if (qi > 0)
                        fprintf(out, ", ");
                    qs_emit_qubit(out, in->qubits[qi]);
                }
                fprintf(out, ");\n");
            }
            break;
        }

        case MQ_Instr_Measure:
        {
            qs_indent(out, level);
            if (in->measure.has_target)
            {
                fprintf(out, "mutable r%u = MResetZ(", in->measure.clbit);
                qs_emit_qubit(out, in->qubits[0]);
                fprintf(out, ");\n");
            }
            else
            {
                fprintf(out, "let _ = MResetZ(");
                qs_emit_qubit(out, in->qubits[0]);
                fprintf(out, ");\n");
            }
            break;
        }

        case MQ_Instr_Reset:
        {
            qs_indent(out, level);
            fprintf(out, "Reset(");
            qs_emit_qubit(out, in->qubits[0]);
            fprintf(out, ");\n");
            break;
        }

        case MQ_Instr_Barrier:
            /* Q# has no barrier; emit as comment */
            qs_indent(out, level);
            fprintf(out, "// barrier\n");
            break;

        default:
            break;
        }
    }

    /* ── statement emitter (forward decl for mutual recursion) ───────────────── */

    static void qs_emit_stmt(FILE * out, MQ_Stmt * s, u32 level);

    static void qs_emit_stmt(FILE * out, MQ_Stmt * s, u32 level)
    {
        if (!s)
            return;

        switch (s->kind)
        {

        case MQ_Stmt_Block:
            for (u32 i = 0; i < s->block.count; i++)
                qs_emit_stmt(out, s->block.stmts[i], level);
            break;

        case MQ_Stmt_DeclQubit:
            qs_indent(out, level);
            if (s->decl_qubit.count == 1)
            {
                /* Single qubit: use Qubit() — emit bare, no index */
                fprintf(out, "use %.*s = Qubit();\n",
                        (int)s->decl_qubit.name.size,
                        s->decl_qubit.name.str);
                /* Register as bare single-qubit entry (size==0) */
                qmap_add(s->decl_qubit.base_id, 0,
                         s->decl_qubit.name.str, (u32)s->decl_qubit.name.size);
            }
            else
            {
                /* Multiple qubits: use Qubit[N] */
                fprintf(out, "use %.*s = Qubit[%u];\n",
                        (int)s->decl_qubit.name.size,
                        s->decl_qubit.name.str,
                        s->decl_qubit.count);
                /* Register as array entry */
                qmap_add(s->decl_qubit.base_id, s->decl_qubit.count,
                         s->decl_qubit.name.str, (u32)s->decl_qubit.name.size);
            }
            break;

        case MQ_Stmt_Instr:
            qs_emit_instr(out, &s->instr, level);
            break;

        case MQ_Stmt_Adjoint:
            qs_indent(out, level);
            fprintf(out, "within {\n");
            qs_emit_stmt(out, s->adjoint_body, level + 1);
            qs_indent(out, level);
            fprintf(out, "} apply {\n");
            qs_indent(out, level);
            fprintf(out, "}\n");
            break;

        case MQ_Stmt_Comment:
            qs_indent(out, level);
            fprintf(out, "//%.*s\n",
                    (int)s->comment_text.size, s->comment_text.str);
            break;

        case MQ_Stmt_Call:
        {
            qs_indent(out, level);
            fprintf(out, "%.*s(",
                    (int)s->call.callee.size, s->call.callee.str);
            for (u32 i = 0; i < s->call.arg_count; i++)
            {
                if (i > 0)
                    fprintf(out, ", ");
                qs_emit_expr_any(out, s->call.args[i]);
            }
            fprintf(out, ");\n");
            break;
        }

        case MQ_Stmt_DeclClassical:
        {
            MQ_Type *t = s->decl_classical.type;
            b8 is_mutable = t && (t->width & QS_MUTABLE_FLAG);
            qs_indent(out, level);
            fprintf(out, is_mutable ? "mutable" : "let");
            fprintf(out, " %.*s = ",
                    (int)s->decl_classical.name.size,
                    s->decl_classical.name.str);
            if (s->decl_classical.init)
                qs_emit_expr_any(out, s->decl_classical.init);
            else
                fprintf(out, "0");
            fprintf(out, ";\n");
            break;
        }

        case MQ_Stmt_Set:
            qs_indent(out, level);
            fprintf(out, "set ");
            qs_emit_expr_any(out, s->set.lhs);
            if (s->set.augmented)
            {
                static const char *aug_ops[] = {
                    "+=", "-=", "*=", "/=", "%=", "^=",
                    "&&&=", "|||=", "^^^=", "<<<=", ">>>=",
                    "", "", "", "", "", "", "", ""};
                fprintf(out, " %s ", aug_ops[s->set.aug_op]);
            }
            else
            {
                fprintf(out, " = ");
            }
            qs_emit_expr_any(out, s->set.rhs);
            fprintf(out, ";\n");
            break;

        case MQ_Stmt_If:
            for (u32 i = 0; i < s->if_stmt.count; i++)
            {
                MQ_IfBranch *br = &s->if_stmt.branches[i];
                qs_indent(out, level);
                if (!br->cond)
                {
                    fprintf(out, "else {\n");
                }
                else
                {
                    fprintf(out, i == 0 ? "if " : "elif ");
                    qs_emit_expr_any(out, br->cond);
                    fprintf(out, " {\n");
                }
                qs_emit_stmt(out, br->body, level + 1);
                qs_indent(out, level);
                fprintf(out, "}\n");
            }
            break;

        case MQ_Stmt_For:
        {
            /* Save/restore the qmap so any use-statements inside the loop
             * body don't leak into the outer scope. */
            u32 saved_qmap_count = s_qmap_count;
            QS_QMapEntry saved_qmap[QS_QMAP_MAX];
            memcpy(saved_qmap, s_qmap, sizeof(QS_QMapEntry) * s_qmap_count);

            /* ── Detect ApplyToEach-lowered for-loop ─────────────────────────
             *
             * Pattern produced by lower_call_expr for ApplyToEach(Gate, reg):
             *
             *   MQ_Stmt_For {
             *     var_name  = "_i"
             *     iterable  = BinOp_Shl(0, reg_size-1)   -- range sentinel
             *     body      = MQ_Stmt_Block {
             *                   MQ_Stmt_Instr { Gate on placeholder qubit }
             *                 }
             *   }
             *
             * The placeholder qubit id is reg_base (always the first qubit of
             * the register).  We must emit:
             *
             *   for _i in 0..reg_size-1 { Gate(reg[_i]); }
             *
             * instead of:
             *
             *   for _i in 0..reg_size-1 { Gate(qs[0]); }   ← WRONG
             *
             * Detection: var_name == "_i"  AND  body is a single-instr block
             * (or a single MQ_Stmt_Instr directly) whose gate has qubit_count==1.
             * ────────────────────────────────────────────────────────────── */
            b8 is_apply_each_loop = false;
            string loop_var = s->for_loop.var_name;

            /* Check var_name == "_i" */
            if (loop_var.size == 2 && loop_var.str &&
                strncmp((char *)loop_var.str, "_i", 2) == 0)
            {
                /* Check body is a single gate instruction */
                MQ_Stmt *body = s->for_loop.body;
                MQ_Stmt *inner = NULL;

                if (body && body->kind == MQ_Stmt_Block &&
                    body->block.count == 1)
                    inner = body->block.stmts[0];
                else if (body && body->kind == MQ_Stmt_Instr)
                    inner = body;
                /* Also handle Adjoint-wrapped: MQ_Stmt_Adjoint { MQ_Stmt_Instr } */
                else if (body && body->kind == MQ_Stmt_Adjoint &&
                         body->adjoint_body &&
                         body->adjoint_body->kind == MQ_Stmt_Instr)
                    inner = body; /* keep adjoint wrapper, handle below */

                if (inner && (inner->kind == MQ_Stmt_Instr ||
                              inner->kind == MQ_Stmt_Adjoint))
                    is_apply_each_loop = true;
            }

            qs_indent(out, level);
            fprintf(out, "for %.*s in ",
                    (int)loop_var.size, loop_var.str);

            /* Emit the range: BinOp_Shl prints as "0..N" via qs_emit_expr */
            qs_emit_expr_any(out, s->for_loop.iterable);
            fprintf(out, " {\n");

            if (is_apply_each_loop)
            {
                /* ── Emit Gate(reg[_i]) using the loop variable ── */
                MQ_Stmt *body  = s->for_loop.body;
                MQ_Stmt *inner = NULL;
                b8 is_adjoint  = false;

                if (body->kind == MQ_Stmt_Block && body->block.count == 1)
                    inner = body->block.stmts[0];
                else
                    inner = body;

                if (inner->kind == MQ_Stmt_Adjoint)
                {
                    is_adjoint = true;
                    inner = inner->adjoint_body;
                }

                /* inner is now MQ_Stmt_Instr */
                MQ_Instruction *instr = &inner->instr;

                /* Find the register name for the placeholder qubit id */
                u32 placeholder_id = instr->qubits[0];
               const char *reg_name = NULL;

/* First try: use the encoded base_id from the loop type annotation
 * (set by lower_call_expr for ApplyToEach loops). This is unambiguous
 * regardless of register name or qubit count. */
u32 lookup_base = placeholder_id; // default
if (s->for_loop.var_type &&
    s->for_loop.var_type->element_type &&
    s->for_loop.var_type->element_type->kind == MQ_Type_QubitReg)
{
    lookup_base = s->for_loop.var_type->element_type->width;
}

for (u32 mi = 0; mi < s_qmap_count; mi++)
{
    QS_QMapEntry *e = &s_qmap[mi];
    if (e->size > 0 &&
        lookup_base >= e->base &&
        lookup_base < e->base + e->size)
    {
        reg_name = e->name;
        break;
    }
}

/* Last resort fallback — should never fire with valid IR */
if (!reg_name)
{
    for (u32 mi = 0; mi < s_qmap_count; mi++)
        if (s_qmap[mi].size > 0) { reg_name = s_qmap[mi].name; break; }
}
if (!reg_name) reg_name = "qs";

                qs_indent(out, level + 1);

                if (is_adjoint)
                    fprintf(out, "Adjoint ");

                if (instr->gate.gate == MQ_Gate_Custom)
                {
                    fprintf(out, "%.*s(%s[%.*s]);\n",
                            (int)instr->gate.custom_name.size,
                            (char *)instr->gate.custom_name.str,
                            reg_name,
                            (int)loop_var.size, (char *)loop_var.str);
                }
                else
                {
                    /* Emit params first if any, then reg[_i] */
                    if (instr->gate.param_count > 0)
                    {
                        fprintf(out, "%s(", qs_gate_name(instr->gate.gate));
                        for (u8 pi = 0; pi < instr->gate.param_count; pi++)
                        {
                            if (pi > 0) fprintf(out, ", ");
                            qs_emit_gate_param(out, instr, pi);
                        }
                        fprintf(out, ", %s[%.*s]);\n",
                                reg_name,
                                (int)loop_var.size, (char *)loop_var.str);
                    }
                    else
                    {
                        fprintf(out, "%s(%s[%.*s]);\n",
                                qs_gate_name(instr->gate.gate),
                                reg_name,
                                (int)loop_var.size, (char *)loop_var.str);
                    }
                }
            }
            else
            {
                /* Normal for-loop body — recurse as usual */
                qs_emit_stmt(out, s->for_loop.body, level + 1);
            }

            qs_indent(out, level);
            fprintf(out, "}\n");

            s_qmap_count = saved_qmap_count;
            memcpy(s_qmap, saved_qmap, sizeof(QS_QMapEntry) * saved_qmap_count);
            break;
        }

        case MQ_Stmt_While:
            qs_indent(out, level);
            fprintf(out, "while ");
            qs_emit_expr_any(out, s->while_loop.cond);
            fprintf(out, " {\n");
            qs_emit_stmt(out, s->while_loop.body, level + 1);
            qs_indent(out, level);
            fprintf(out, "}\n");
            break;

        case MQ_Stmt_Break:
            qs_indent(out, level);
            fprintf(out, "break;\n"); /* not valid Q# but keep for completeness */
            break;

        case MQ_Stmt_Continue:
            qs_indent(out, level);
            fprintf(out, "continue;\n");
            break;

        case MQ_Stmt_Return:
            qs_indent(out, level);
            fprintf(out, "return");
            if (s->return_val)
            {
                fprintf(out, " ");
                qs_emit_expr_any(out, s->return_val);
            }
            fprintf(out, ";\n");
            break;

        case MQ_Stmt_Pragma:
            qs_indent(out, level);
            fprintf(out, "// pragma %.*s %.*s\n",
                    (int)s->pragma.key.size, s->pragma.key.str,
                    (int)s->pragma.value.size, s->pragma.value.str);
            break;

        default:
            break;
        }
    }

    /* ── type string for routine parameters ──────────────────────────────────── */

    static const char *qs_param_type_str(MQ_FormalParam * p)
    {
        if (!p->type)
            return "Double";
        /* Callable (oracle) sentinel set during lowering */
        if (p->type->kind == MQ_Type_Void && p->type->width == 0xCAFEu)
            return "((Qubit[], Qubit) => Unit)";
        switch (p->type->kind)
        {
        case MQ_Type_QubitReg:
            return "Qubit[]";
        case MQ_Type_Qubit:
            return "Qubit";
        case MQ_Type_Bool:
            return "Bool";
        case MQ_Type_Int:
            return "Int";
        case MQ_Type_Float: /* fall through */
        case MQ_Type_Angle:
            return "Double";
        default:
            return "Double";
        }
    }

    /* ── count qubits referenced in a stmt tree ─────────────────────────────── */
    /*
     * Used to emit `use qs = Qubit[N]` at the top of routines that
     * receive a Qubit[] parameter (where N comes from the IR register table
     * or, as a fallback, from scanning the instruction qubits).
     */
    static u32 qs_max_qubit_id(MQ_Stmt * s)
    {
        if (!s)
            return 0;
        u32 m = 0;
        switch (s->kind)
        {
        case MQ_Stmt_Block:
            for (u32 i = 0; i < s->block.count; i++)
            {
                u32 v = qs_max_qubit_id(s->block.stmts[i]);
                if (v > m)
                    m = v;
            }
            break;
        case MQ_Stmt_Instr:
        {
            MQ_Instruction *in = &s->instr;
            for (u8 qi = 0; qi < in->qubit_count; qi++)
                if (in->qubits[qi] + 1 > m)
                    m = in->qubits[qi] + 1;
            for (u8 ci = 0; ci < in->gate.control_count; ci++)
                if (in->gate.controls[ci] + 1 > m)
                    m = in->gate.controls[ci] + 1;
            break;
        }
        case MQ_Stmt_Adjoint:
            m = qs_max_qubit_id(s->adjoint_body);
            break;
        case MQ_Stmt_If:
            for (u32 i = 0; i < s->if_stmt.count; i++)
            {
                u32 v = qs_max_qubit_id(s->if_stmt.branches[i].body);
                if (v > m)
                    m = v;
            }
            break;
        case MQ_Stmt_For:
            m = qs_max_qubit_id(s->for_loop.body);
            break;
        case MQ_Stmt_While:
            m = qs_max_qubit_id(s->while_loop.body);
            break;
        default:
            break;
        }
        return m;
    }

    /* ── public entry point ───────────────────────────────────────────────────── */

    void qsharp_emit(FILE * out, MQ_Program * prog)
    {
        if (!out || !prog)
            return;

        fprintf(out, "// Generated by MetaQuantum cross-compiler\n");
        fprintf(out, "namespace MetaQuantum.Output {\n\n");
        fprintf(out, "    open Microsoft.Quantum.Intrinsic;\n");
        fprintf(out, "    open Microsoft.Quantum.Measurement;\n");
        fprintf(out, "    open Microsoft.Quantum.Diagnostics;\n");
        fprintf(out, "    open Microsoft.Quantum.Math as Msk;\n\n");

        /* ── user-defined routines ── */
        for (u32 r = 0; r < prog->routine_count; r++)
        {
            MQ_Routine *rt = prog->routines[r];
            if (!rt || rt->is_intrinsic)
                continue;

            fprintf(out, "    operation %.*s(",
                    (int)rt->name.size, rt->name.str);

            for (u32 p = 0; p < rt->param_count; p++)
            {
                if (p > 0)
                    fprintf(out, ", ");
                fprintf(out, "%.*s : %s",
                        (int)rt->params[p].name.size,
                        rt->params[p].name.str,
                        qs_param_type_str(&rt->params[p]));
            }
            /* Detect return type from body: if any return_val is a ternary/string
             * expression, emit ": String", otherwise ": Unit". */
            {
                b8 returns_string = false;
                /* Walk the body once looking for a Return stmt with a non-null val */
                MQ_Stmt *body = rt->body;
                if (body && body->kind == MQ_Stmt_Block)
                {
                    for (u32 si = 0; si < body->block.count && !returns_string; si++)
                    {
                        MQ_Stmt *st = body->block.stmts[si];
                        if (st && st->kind == MQ_Stmt_Return && st->return_val)
                            returns_string = true;
                    }
                }
                else if (body && body->kind == MQ_Stmt_Return && body->return_val)
                {
                    returns_string = true;
                }
                fprintf(out, ") : %s {\n", returns_string ? "String" : "Unit");
            }

            /* ── qubit state: save outer qmap, build per-routine map from params ──
             *
             * Param qubit IDs are assigned by the lowering pass (QS_Ctx) in the
             * order the params are declared, starting from qubit_counter=0 for
             * each routine.  So for:
             *   operation Foo(inputQs : Qubit[], ancilla : Qubit)
             * inputQs occupies IDs [0 .. N-1] and ancilla occupies ID N.
             * N is derived from qs_max_qubit_id(body) - count_of_single_qubit_params.
             */
            u32 saved_qmap_count = s_qmap_count;
            QS_QMapEntry saved_qmap_entries[QS_QMAP_MAX];
            memcpy(saved_qmap_entries, s_qmap, sizeof(QS_QMapEntry) * s_qmap_count);
            qmap_reset();

            {
                /* Count single-qubit params so we know the QubitReg size */
                u32 single_qubit_params = 0;
                for (u32 p = 0; p < rt->param_count; p++)
                    if (rt->params[p].type && rt->params[p].type->kind == MQ_Type_Qubit)
                        single_qubit_params++;

                u32 body_max = qs_max_qubit_id(rt->body); /* max qubit id + 1 */
                /* QubitReg occupies IDs [0 .. body_max - single_qubit_params - 1] */
                u32 reg_size = (body_max > single_qubit_params)
                               ? body_max - single_qubit_params : 0;

                u32 flat_id = 0;
                for (u32 p = 0; p < rt->param_count; p++)
                {
                    MQ_FormalParam *fp = &rt->params[p];
                    if (!fp->type || fp->type->kind == MQ_Type_Void)
                    {
                        /* Callable param — no qubit ID */
                        continue;
                    }
                    if (fp->type->kind == MQ_Type_QubitReg)
                    {
                        /* Array param: register with computed size */
                        qmap_add(flat_id, reg_size,
                                 fp->name.str, (u32)fp->name.size);
                        flat_id += reg_size;
                    }
                    else if (fp->type->kind == MQ_Type_Qubit)
                    {
                        /* Single qubit: bare entry (size==0) */
                        qmap_add(flat_id, 0,
                                 fp->name.str, (u32)fp->name.size);
                        flat_id++;
                    }
                    else
                    {
                        flat_id++;
                    }
                }
            }

            /* Only emit `use qs = Qubit[N]` for routines with NO qubit params
             * AND which directly reference qubit IDs in their body WITHOUT
             * having their own DeclQubit statements (those handle themselves). */
            {
                b8 has_any_qubit_param = false;
                for (u32 p = 0; p < rt->param_count; p++)
                {
                    if (rt->params[p].type &&
                        (rt->params[p].type->kind == MQ_Type_QubitReg ||
                         rt->params[p].type->kind == MQ_Type_Qubit))
                    {
                        has_any_qubit_param = true;
                        break;
                    }
                }

                b8 has_decl_qubit_in_body = false;
                if (rt->body && rt->body->kind == MQ_Stmt_Block)
                {
                    for (u32 si = 0; si < rt->body->block.count; si++)
                    {
                        MQ_Stmt *st = rt->body->block.stmts[si];
                        if (st && st->kind == MQ_Stmt_DeclQubit)
                        {
                            has_decl_qubit_in_body = true;
                            break;
                        }
                    }
                }

                if (!has_any_qubit_param && !has_decl_qubit_in_body)
                {
                    u32 nq = qs_max_qubit_id(rt->body);
                    if (nq > 0)
                    {
                        fprintf(out, "        use qs = Qubit[%u];\n", nq);
                        qmap_add(0, nq, (const u8 *)"qs", 2);
                    }
                }
            }

            qs_emit_stmt(out, rt->body, 2);

            /* Restore outer qmap */
            s_qmap_count = saved_qmap_count;
            memcpy(s_qmap, saved_qmap_entries, sizeof(QS_QMapEntry) * saved_qmap_count);

            fprintf(out, "    }\n\n");
        }

        /* ── circuits as @EntryPoint operations ── */
        for (u32 c = 0; c < prog->circuit_count; c++)
        {
            MQ_Circuit *circ = prog->circuits[c];
            if (!circ)
                continue;

            fprintf(out, "    @EntryPoint()\n");
            fprintf(out, "    operation %.*s(",
                    (int)circ->name.size, circ->name.str);

            for (u32 p = 0; p < circ->param_count; p++)
            {
                if (p > 0)
                    fprintf(out, ", ");
                fprintf(out, "%.*s : Double",
                        (int)circ->param_names[p].size,
                        circ->param_names[p].str);
            }
            fprintf(out, ") : Unit {\n");

            /* Reset qmap for circuit scope; DeclQubit stmts inside the body
             * will populate it as they are emitted. */
            u32 saved_circ_qmap_count = s_qmap_count;
            QS_QMapEntry saved_circ_qmap[QS_QMAP_MAX];
            memcpy(saved_circ_qmap, s_qmap, sizeof(QS_QMapEntry) * s_qmap_count);
            qmap_reset();

            // Only emit bulk qubit allocation if the circuit has top-level DeclQubit stmts
            // (i.e. qubits NOT locally scoped inside loops). The DeclQubit emitter
            // will handle per-register use statements as it walks the body.
            // Nothing to pre-emit here; DeclQubit stmts in the body do it.

            // qs_emit_stmt(out, circ->body, 2);
            /* Emit one `use reg = Qubit[N];` per register from the IR metadata,
             * then populate the qmap so gate emissions can resolve qubit names.
             * DeclQubit stmts in the body are skipped below to avoid duplication. */
            for (u32 ri = 0; ri < circ->register_count; ri++)
            {
                MQ_Register *reg = &circ->registers[ri];
                if (reg->kind != MQ_Reg_Quantum) continue;
                if (reg->size == 1)
                    fprintf(out, "        use %.*s = Qubit();\n",
                            (int)reg->name.size, reg->name.str);
                else
                    fprintf(out, "        use %.*s = Qubit[%u];\n",
                            (int)reg->name.size, reg->name.str, reg->size);
                qmap_add(reg->base_id, reg->size,
                         reg->name.str, (u32)reg->name.size);
            }

            /* Walk body, skipping DeclQubit nodes (already emitted above). */
            if (circ->body && circ->body->kind == MQ_Stmt_Block)
            {
                for (u32 si = 0; si < circ->body->block.count; si++)
                {
                    MQ_Stmt *st = circ->body->block.stmts[si];
                    if (st && st->kind == MQ_Stmt_DeclQubit) continue;
                    qs_emit_stmt(out, st, 2);
                }
            }
            else
            {
                qs_emit_stmt(out, circ->body, 2);
            }


            s_qmap_count = saved_circ_qmap_count;
            memcpy(s_qmap, saved_circ_qmap, sizeof(QS_QMapEntry) * saved_circ_qmap_count);

            fprintf(out, "    }\n");
        }
        

        fprintf(out, "}\n");
    }

 ///last  3 