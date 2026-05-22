/* qiskit.c  —  Tree-sitter Python/Qiskit frontend + IR lowering
 *
 * Pass 1 : qiskit_parse_file        — read source, run Tree-sitter (Python grammar)
 * Pass 2 : qiskit_print_tree /
 *           qiskit_print_named_nodes — debug printers
 * Pass 3 : qiskit_tree_to_ir        — CST -> MQ_Program IR
 *
 * Qiskit Python idioms recognised:
 *   QuantumCircuit(nq, nc)           -> MQ_Circuit  + DeclQubit
 *   QuantumRegister(n, "name")       -> MQ_Register (quantum)
 *   ClassicalRegister(n, "name")     -> MQ_Register (classical)
 *   qc.h / qc.x / qc.cx / qc.rx … -> MQ_Instruction (gate)
 *   qc.measure(q, c)                 -> MQ_Instruction (measure)
 *   qc.reset(q)                      -> MQ_Instruction (reset)
 *   qc.barrier()                     -> MQ_Instruction (barrier)
 *   qc.delay(dt, q)                  -> MQ_Instruction (delay)
 *   def <name>(…):                   -> MQ_Routine
 *   for / while / if                 -> MQ_Stmt control flow
 *   assignments                      -> MQ_Stmt_DeclClassical / Stmt_Set
 *
 * Host-side Python (imports, prints, simulator calls, etc.) is skipped.
 */

#include "qiskit.h"
#include "ir.h"
#include "../base/mem.h"
#include "../base/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QK_SNIPPET_MAX 64

/* ═══════════════════════════════════════════════════════════════════════════
 * §0  Shared internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static char *read_file(const char *path, u32 *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[qiskit] cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)    { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[r] = '\0';
    *out_len = (u32)r;
    return buf;
}

/* Extract the source slice covered by a TSNode into arena memory. */
static string node_text(M_Arena *arena, TSNode node, const char *src)
{
    u32 start = ts_node_start_byte(node);
    u32 end   = ts_node_end_byte(node);
    u32 len   = end - start;
    if (len == 0) return (string){0};
    u8 *buf = (u8 *)arena_alloc(arena, len + 1);
    memcpy(buf, src + start, len);
    buf[len] = '\0';
    return (string){ .str = buf, .size = len };
}

static b8 node_is(TSNode n, const char *type)
{
    return strcmp(ts_node_type(n), type) == 0;
}

/* Return the first named child whose type matches, or a null node. */
static TSNode find_child(TSNode parent, const char *type)
{
    u32 count = ts_node_named_child_count(parent);
    for (u32 i = 0; i < count; i++) {
        TSNode c = ts_node_named_child(parent, i);
        if (node_is(c, type)) return c;
    }
    TSNode null_node;
    memset(&null_node, 0, sizeof(TSNode));
    return null_node;
}

/* Return the first named child with any of the given type strings. */
static TSNode find_child_any(TSNode parent, const char **types, u32 ntypes)
{
    u32 count = ts_node_named_child_count(parent);
    for (u32 i = 0; i < count; i++) {
        TSNode c = ts_node_named_child(parent, i);
        const char *ct = ts_node_type(c);
        for (u32 t = 0; t < ntypes; t++)
            if (strcmp(ct, types[t]) == 0) return c;
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
        { src++; src_len--; }
    b8 trunc = (src_len > max_chars);
    u32 copy_len = trunc ? max_chars : src_len;
    if (copy_len >= (u32)dst_size - 4) copy_len = (u32)dst_size - 4;
    memcpy(dst, src, copy_len);
    if (trunc) { memcpy(dst + copy_len, "...", 3); dst[copy_len + 3] = '\0'; }
    else         dst[copy_len] = '\0';
    for (char *p = dst; *p; p++)
        if (*p == '\r' || *p == '\n') *p = ' ';
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §1  Pass 1 – parse
 * ═══════════════════════════════════════════════════════════════════════════ */

Qiskit_ParseResult qiskit_parse_file(const char *path)
{
    Qiskit_ParseResult result;
    memset(&result, 0, sizeof(result));

    result.source = read_file(path, &result.source_len);
    if (!result.source) return result;

    result.parser = ts_parser_new();
    if (!result.parser) {
        fprintf(stderr, "[qiskit] ts_parser_new() failed\n");
        free(result.source); result.source = NULL;
        return result;
    }

    if (!ts_parser_set_language(result.parser, tree_sitter_python())) {
        fprintf(stderr, "[qiskit] ts_parser_set_language() failed\n");
        ts_parser_delete(result.parser); free(result.source);
        result.parser = NULL; result.source = NULL;
        return result;
    }

    result.tree = ts_parser_parse_string(result.parser, NULL,
                                         result.source, result.source_len);
    if (!result.tree) {
        fprintf(stderr, "[qiskit] parse returned NULL\n");
        ts_parser_delete(result.parser); free(result.source);
        result.parser = NULL; result.source = NULL;
        return result;
    }

    TSNode root = ts_tree_root_node(result.tree);
    if (ts_node_has_error(root))
        fprintf(stderr, "[qiskit] warning: parse errors in '%s'\n", path);

    result.ok = true;
    return result;
}

void qiskit_parse_result_free(Qiskit_ParseResult *result)
{
    if (!result) return;
    if (result->tree)   { ts_tree_delete(result->tree);   result->tree   = NULL; }
    if (result->parser) { ts_parser_delete(result->parser); result->parser = NULL; }
    if (result->source) { free(result->source);           result->source = NULL; }
    result->ok = false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §2  Pass 2 – debug printers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_node(TSNode node, const char *source, int depth)
{
    const char *type   = ts_node_type(node);
    b8  is_named       = ts_node_is_named(node);
    b8  has_error      = ts_node_has_error(node);
    u32 start_byte     = ts_node_start_byte(node);
    u32 end_byte       = ts_node_end_byte(node);
    u32 span           = end_byte - start_byte;
    TSPoint sp         = ts_node_start_point(node);
    TSPoint ep         = ts_node_end_point(node);

    for (int i = 0; i < depth; i++) printf("  ");

    if (is_named)
        printf("\033[1;36m%s\033[0m", type);
    else
        printf("\033[0;33m\"%s\"\033[0m", type);

    printf(" \033[0;90m[%u:%u \xe2\x80\x93 %u:%u]\033[0m",
           sp.row + 1, sp.column, ep.row + 1, ep.column);

    if (has_error)
        printf(" \033[1;31m<ERROR>\033[0m");

    u32 child_count = ts_node_child_count(node);
    if (child_count == 0 && span > 0 && span < 200) {
        char snippet[QK_SNIPPET_MAX + 4];
        copy_snippet(snippet, sizeof(snippet), source + start_byte, span, QK_SNIPPET_MAX);
        printf("  \033[0;32m`%s`\033[0m", snippet);
    }
    printf("\n");
    for (u32 i = 0; i < child_count; i++)
        print_node(ts_node_child(node, i), source, depth + 1);
}

void qiskit_print_tree(const Qiskit_ParseResult *result)
{
    if (!result || !result->ok) {
        fprintf(stderr, "[qiskit] qiskit_print_tree: invalid result\n");
        return;
    }
    TSNode root = ts_tree_root_node(result->tree);
    printf("\n=== Qiskit/Python Syntax Tree ===\n\n");
    print_node(root, result->source, 0);
    printf("\n");
}

void qiskit_print_named_nodes(const Qiskit_ParseResult *result)
{
    if (!result || !result->ok) return;
    printf("\n=== Qiskit Named Nodes ===\n");
    printf("%-40s  %-8s  %s\n", "Node Type", "Line", "Source");
    printf("%-40s  %-8s  %s\n",
           "----------------------------------------",
           "--------",
           "----------------------------------------------");

    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(result->tree));
    b8 visited_children = false;
    while (true) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (!visited_children && ts_node_is_named(node)) {
            const char *type = ts_node_type(node);
            TSPoint sp = ts_node_start_point(node);
            u32 start  = ts_node_start_byte(node);
            u32 span   = ts_node_end_byte(node) - start;
            char snippet[QK_SNIPPET_MAX + 4] = "";
            if (span < 120)
                copy_snippet(snippet, sizeof(snippet),
                             result->source + start, span, QK_SNIPPET_MAX);
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
 * §Pass3  tree → MQ_Program IR  (mirrors qsharp_tree_to_ir / QS_Ctx pattern)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Architecture:
 *   QK_Ctx  — per-parse mutable state (arena, src pointer, prog, circuit,
 *             qubit counter, register table).  Same shape as QS_Ctx.
 *
 *   qk_lower_expr   — TSNode → MQ_Expr*
 *   qk_lower_instr  — TSNode (call) → MQ_Instruction  (quantum ops only)
 *   qk_lower_stmt   — TSNode → MQ_Stmt*  (dispatches on Python node type)
 *   qk_lower_block  — TSNode (block/module) → MQ_Stmt* (Stmt_Block)
 *   qk_lower_circuit — scans the module for register decls + gate calls,
 *                      builds one MQ_Circuit, registers it in prog.
 *
 * Separation of concerns:
 *   - No emitter code here.  This pass ONLY builds the IR.
 *   - No Python runtime semantics (AerSimulator, transpile, …) appear in
 *     the output IR; they are silently dropped.
 *   - QuantumCircuit(n) sets the qubit metadata internally; no duplicate
 *     DeclQubit statements are emitted into the circuit body.
 * ─────────────────────────────────────────────────────────────────────────── */

/* ── context ──────────────────────────────────────────────────────────────── */

#define QK_MAX_REGS 64

typedef struct
{
    M_Arena    *arena;
    const char *src;          /* raw source text (owned by Qiskit_ParseResult) */
    MQ_Program *prog;
    MQ_Circuit *circuit;      /* non-NULL while inside a circuit scope */
    u32         qubit_counter;
    struct {
        string name;
        u32    base_id;
        u32    size;
    } regs[QK_MAX_REGS];
    u32 reg_count;
} QK_Ctx;

/* ── register helpers ─────────────────────────────────────────────────────── */

static u32 qk_ctx_add_register(QK_Ctx *ctx, string name, u32 size)
{
    u32 base = ctx->qubit_counter;
    ctx->qubit_counter += size;
    if (ctx->reg_count < QK_MAX_REGS)
    {
        ctx->regs[ctx->reg_count].name    = name;
        ctx->regs[ctx->reg_count].base_id = base;
        ctx->regs[ctx->reg_count].size    = size;
        ctx->reg_count++;
    }
    return base;
}

/* Resolve reg[idx] → flat qubit id. */
static u32 qk_ctx_resolve_qubit(QK_Ctx *ctx, string reg_name, u32 idx)
{
    for (u32 i = 0; i < ctx->reg_count; i++)
        if (str_eq(ctx->regs[i].name, reg_name))
            return ctx->regs[i].base_id + idx;
    /* Unknown register: allocate on the fly (shouldn't happen in well-formed source). */
    return ctx->qubit_counter++;
}

/* ── gate name → MQ_GateType ────────────────────────────────────────────── */

static MQ_GateType qk_gate_from_name_str(const char *n)
{
    if (!n) return MQ_Gate_Custom;
    if (strcmp(n,"i")     == 0 || strcmp(n,"id")    == 0) return MQ_Gate_I;
    if (strcmp(n,"h")     == 0)                           return MQ_Gate_H;
    if (strcmp(n,"x")     == 0)                           return MQ_Gate_X;
    if (strcmp(n,"y")     == 0)                           return MQ_Gate_Y;
    if (strcmp(n,"z")     == 0)                           return MQ_Gate_Z;
    if (strcmp(n,"s")     == 0)                           return MQ_Gate_S;
    if (strcmp(n,"sdg")   == 0)                           return MQ_Gate_Sdg;
    if (strcmp(n,"t")     == 0)                           return MQ_Gate_T;
    if (strcmp(n,"tdg")   == 0)                           return MQ_Gate_Tdg;
    if (strcmp(n,"p")     == 0)                           return MQ_Gate_P;
    if (strcmp(n,"rx")    == 0)                           return MQ_Gate_RX;
    if (strcmp(n,"ry")    == 0)                           return MQ_Gate_RY;
    if (strcmp(n,"rz")    == 0)                           return MQ_Gate_RZ;
    if (strcmp(n,"u")     == 0 || strcmp(n,"u3")   == 0) return MQ_Gate_U;
    if (strcmp(n,"swap")  == 0)                           return MQ_Gate_SWAP;
    if (strcmp(n,"iswap") == 0)                           return MQ_Gate_ISWAP;
    if (strcmp(n,"rzz")   == 0)                           return MQ_Gate_RZZ;
    if (strcmp(n,"rxx")   == 0)                           return MQ_Gate_RXX;
    if (strcmp(n,"ryy")   == 0)                           return MQ_Gate_RYY;
    if (strcmp(n,"ccx")   == 0 || strcmp(n,"ccnot") == 0) return MQ_Gate_CCX;
    if (strcmp(n,"cswap") == 0)                           return MQ_Gate_CSWAP;
    /* Controlled-gate shorthands — base gate; control extracted separately */
    if (strcmp(n,"cx")    == 0 || strcmp(n,"cnot")  == 0) return MQ_Gate_X;
    if (strcmp(n,"cy")    == 0) return MQ_Gate_Y;
    if (strcmp(n,"cz")    == 0) return MQ_Gate_Z;
    if (strcmp(n,"ch")    == 0) return MQ_Gate_H;
    if (strcmp(n,"cp")    == 0) return MQ_Gate_P;
    if (strcmp(n,"crx")   == 0) return MQ_Gate_RX;
    if (strcmp(n,"cry")   == 0) return MQ_Gate_RY;
    if (strcmp(n,"crz")   == 0) return MQ_Gate_RZ;
    if (strcmp(n,"cu")    == 0) return MQ_Gate_U;
    return MQ_Gate_Custom;
}

/* Returns 1 if the Qiskit gate name implies the *first* positional qubit
 * argument is a control qubit rather than a target. */
static b8 qk_gate_has_implicit_ctrl(const char *n)
{
    if (!n) return 0;
    return (strcmp(n,"cx")==0 || strcmp(n,"cnot")==0 ||
            strcmp(n,"cy")==0 || strcmp(n,"cz")==0   ||
            strcmp(n,"ch")==0 || strcmp(n,"cp")==0   ||
            strcmp(n,"crx")==0|| strcmp(n,"cry")==0  ||
            strcmp(n,"crz")==0|| strcmp(n,"cu")==0);
}

/* Number of leading float parameters expected before qubit arguments. */
static u8 qk_gate_expected_params(MQ_GateType g)
{
    switch (g) {
        case MQ_Gate_P:
        case MQ_Gate_RX:
        case MQ_Gate_RY:
        case MQ_Gate_RZ:
        case MQ_Gate_RZZ:
        case MQ_Gate_RXX:
        case MQ_Gate_RYY:  return 1;
        case MQ_Gate_U:    return 3;
        default:           return 0;
    }
}

/* ── forward declarations ─────────────────────────────────────────────────── */

static MQ_Expr *qk_lower_expr (QK_Ctx *ctx, TSNode node);
static MQ_Stmt *qk_lower_stmt (QK_Ctx *ctx, TSNode node);
static MQ_Stmt *qk_lower_block(QK_Ctx *ctx, TSNode block);

/* ── expression lowering ─────────────────────────────────────────────────── */

static MQ_Expr *qk_lower_expr(QK_Ctx *ctx, TSNode node)
{
    if (node_null(node)) return NULL;
    const char *type = ts_node_type(node);

    /* integer literal */
    if (strcmp(type,"integer") == 0) {
        string t = node_text(ctx->arena, node, ctx->src);
        return mq_expr_int(ctx->arena, t.str ? (i64)strtoll((char*)t.str,NULL,10) : 0);
    }
    /* float literal */
    if (strcmp(type,"float") == 0) {
        string t = node_text(ctx->arena, node, ctx->src);
        return mq_expr_float(ctx->arena, t.str ? strtod((char*)t.str,NULL) : 0.0);
    }
    /* bool */
    if (strcmp(type,"true")  == 0) return mq_expr_bool(ctx->arena, 1);
    if (strcmp(type,"false") == 0) return mq_expr_bool(ctx->arena, 0);

    /* identifier */
    if (strcmp(type,"identifier") == 0)
        return mq_expr_var(ctx->arena, node_text(ctx->arena, node, ctx->src));

    /* attribute: obj.attr → emit the full text as a symbol */
    if (strcmp(type,"attribute") == 0)
        return mq_expr_var(ctx->arena, node_text(ctx->arena, node, ctx->src));

    /* subscript: reg[index] */
    if (strcmp(type,"subscript") == 0)
    {
        /* tree-sitter-python field names: "value" = object, "subscript" = index */
        TSNode obj_node = ts_node_child_by_field_name(node,"value",5);
        TSNode idx_node = ts_node_child_by_field_name(node,"subscript",9);
        if (node_null(idx_node))
            idx_node = ts_node_named_child(node,1);

        string reg_name = node_text(ctx->arena, obj_node, ctx->src);
        MQ_Expr *idx    = qk_lower_expr(ctx, idx_node);
        return mq_expr_reg_index(ctx->arena, reg_name, idx);
    }

    /* binary_operator */
    if (strcmp(type,"binary_operator") == 0)
    {
        TSNode lhs_n = ts_node_child(node,0);
        TSNode op_n  = ts_node_child(node,1);
        TSNode rhs_n = ts_node_child(node,2);
        MQ_Expr *l = qk_lower_expr(ctx, lhs_n);
        MQ_Expr *r = qk_lower_expr(ctx, rhs_n);
        string op  = node_text(ctx->arena, op_n, ctx->src);

        MQ_BinOp bop = MQ_BinOp_Add;
        if (op.str) {
            const char *s = (char*)op.str;
            if      (strcmp(s,"+")==0)  bop = MQ_BinOp_Add;
            else if (strcmp(s,"-")==0)  bop = MQ_BinOp_Sub;
            else if (strcmp(s,"*")==0)  bop = MQ_BinOp_Mul;
            else if (strcmp(s,"/")==0)  bop = MQ_BinOp_Div;
            else if (strcmp(s,"%")==0)  bop = MQ_BinOp_Mod;
            else if (strcmp(s,"**")==0) bop = MQ_BinOp_Pow;
            else if (strcmp(s,"==")==0) bop = MQ_BinOp_Eq;
            else if (strcmp(s,"!=")==0) bop = MQ_BinOp_Ne;
            else if (strcmp(s,"<")==0)  bop = MQ_BinOp_Lt;
            else if (strcmp(s,">")==0)  bop = MQ_BinOp_Gt;
            else if (strcmp(s,"<=")==0) bop = MQ_BinOp_Le;
            else if (strcmp(s,">=")==0) bop = MQ_BinOp_Ge;
            else if (strcmp(s,"&")==0)  bop = MQ_BinOp_And;
            else if (strcmp(s,"|")==0)  bop = MQ_BinOp_Or;
            else if (strcmp(s,"^")==0)  bop = MQ_BinOp_Xor;
        }
        return mq_expr_binop(ctx->arena, bop, l, r);
    }

    /* boolean_operator: "and" / "or" */
    if (strcmp(type,"boolean_operator") == 0)
    {
        TSNode lhs_n = ts_node_named_child(node,0);
        TSNode op_n  = ts_node_child(node,1);
        TSNode rhs_n = ts_node_named_child(node,1);
        MQ_Expr *l = qk_lower_expr(ctx, lhs_n);
        MQ_Expr *r = qk_lower_expr(ctx, rhs_n);
        string op  = node_text(ctx->arena, op_n, ctx->src);
        MQ_BinOp bop = (op.str && strcmp((char*)op.str,"and")==0)
                       ? MQ_BinOp_LogAnd : MQ_BinOp_LogOr;
        return mq_expr_binop(ctx->arena, bop, l, r);
    }

    /* call expression inside an expression context */
    if (strcmp(type,"call") == 0)
    {
        TSNode fn_n   = ts_node_child_by_field_name(node,"function",8);
        TSNode args_n = ts_node_child_by_field_name(node,"arguments",9);
        string fn_name = node_text(ctx->arena, fn_n, ctx->src);
        u32 argc = ts_node_named_child_count(args_n);
        MQ_Expr **args = (MQ_Expr**)arena_alloc(ctx->arena, sizeof(MQ_Expr*)*argc);
        for (u32 i = 0; i < argc; i++)
            args[i] = qk_lower_expr(ctx, ts_node_named_child(args_n,i));
        return mq_expr_call(ctx->arena, fn_name, args, argc);
    }

    /* Unhandled — raw source text as a symbol fallback */
    return mq_expr_var(ctx->arena, node_text(ctx->arena, node, ctx->src));
}

/* ── qubit argument extraction ───────────────────────────────────────────── *
 *
 * Qiskit supports two calling conventions for qubit arguments:
 *   subscript style:  qc.h(q[0])   → subscript node, index via "subscript" field
 *   flat-int  style:  qc.h(0)      → integer node,   value IS the qubit id
 *
 * For parametric gates, leading args are floats and trailing args are qubits.
 * expected_params tells us how many leading floats to consume before treating
 * integers as qubit ids.
 * ─────────────────────────────────────────────────────────────────────────── */

static u32 qk_subscript_qubit_id(QK_Ctx *ctx, TSNode subscript_node)
{
    TSNode obj_node = ts_node_child_by_field_name(subscript_node,"value",5);
    TSNode idx_node = ts_node_child_by_field_name(subscript_node,"subscript",9);
    if (node_null(idx_node))
        idx_node = ts_node_named_child(subscript_node,1);

    string reg_name = node_text(ctx->arena, obj_node, ctx->src);
    string idx_str  = node_text(ctx->arena, idx_node, ctx->src);
    u32 idx = idx_str.str ? (u32)strtoul((char*)idx_str.str,NULL,10) : 0;
    return qk_ctx_resolve_qubit(ctx, reg_name, idx);
}

/* ── instruction lowering (quantum calls only) ───────────────────────────── */

static MQ_Instruction qk_lower_call_to_instr(QK_Ctx *ctx, TSNode call_node)
{
    MQ_Instruction zero; memset(&zero,0,sizeof(zero));

    TSNode fn_n   = ts_node_child_by_field_name(call_node,"function",8);
    TSNode args_n = ts_node_child_by_field_name(call_node,"arguments",9);

    /* Resolve method name: qc.h → "h",  h → "h" */
    char gate_buf[64] = {0};
    if (node_is(fn_n,"attribute"))
    {
        TSNode attr = ts_node_child_by_field_name(fn_n,"attribute",9);
        string s    = node_text(ctx->arena, attr, ctx->src);
        u32 n = (s.size < sizeof(gate_buf)-1) ? (u32)s.size : (u32)sizeof(gate_buf)-1;
        if (s.str) { memcpy(gate_buf,s.str,n); gate_buf[n]='\0'; }
    }
    else
    {
        string s = node_text(ctx->arena, fn_n, ctx->src);
        u32 n = (s.size < sizeof(gate_buf)-1) ? (u32)s.size : (u32)sizeof(gate_buf)-1;
        if (s.str) { memcpy(gate_buf,s.str,n); gate_buf[n]='\0'; }
    }

    /* ── measure ── */
    if (strcmp(gate_buf,"measure") == 0)
    {
        u32 argc = ts_node_named_child_count(args_n);
        if (argc >= 2)
        {
            u32 qi = qk_subscript_qubit_id(ctx, ts_node_named_child(args_n,0));
            TSNode carg = ts_node_named_child(args_n,1);
            TSNode cidx = ts_node_child_by_field_name(carg,"subscript",9);
            if (node_null(cidx)) cidx = ts_node_named_child(carg,1);
            string cs = node_text(ctx->arena, cidx, ctx->src);
            u32 ci = cs.str ? (u32)strtoul((char*)cs.str,NULL,10) : 0;
            return mq_instr_measure(qi, ci);
        }
        else if (argc == 1)
        {
            u32 qi = qk_subscript_qubit_id(ctx, ts_node_named_child(args_n,0));
            return mq_instr_measure_discard(qi);
        }
        return zero;
    }

    /* ── reset ── */
    if (strcmp(gate_buf,"reset") == 0)
    {
        u32 argc = ts_node_named_child_count(args_n);
        if (argc >= 1) {
            u32 qi = qk_subscript_qubit_id(ctx, ts_node_named_child(args_n,0));
            return mq_instr_reset(qi);
        }
        return zero;
    }

    /* ── barrier ── */
    if (strcmp(gate_buf,"barrier") == 0)
    {
        u32 argc = ts_node_named_child_count(args_n);
        u32 qubits[MQ_MAX_GATE_QUBITS]; u8 qcnt = 0;
        for (u32 i = 0; i < argc && qcnt < MQ_MAX_GATE_QUBITS; i++) {
            TSNode a = ts_node_named_child(args_n,i);
            if (node_is(a,"subscript"))
                qubits[qcnt++] = qk_subscript_qubit_id(ctx, a);
        }
        return mq_instr_barrier(qubits, qcnt);
    }

    /* ── delay ── */
    if (strcmp(gate_buf,"delay") == 0)
    {
        u32 argc = ts_node_named_child_count(args_n);
        f64 dur = 0.0; MQ_TimeUnit unit = MQ_Time_Dt;
        u32 qubits[MQ_MAX_GATE_QUBITS]; u8 qcnt = 0;
        for (u32 i = 0; i < argc; i++) {
            TSNode a = ts_node_named_child(args_n,i);
            if (node_is(a,"subscript") && qcnt < MQ_MAX_GATE_QUBITS)
                qubits[qcnt++] = qk_subscript_qubit_id(ctx, a);
            else if (node_is(a,"integer")||node_is(a,"float")) {
                string sv = node_text(ctx->arena,a,ctx->src);
                if (sv.str) dur = strtod((char*)sv.str,NULL);
            } else if (node_is(a,"string")) {
                string sv = node_text(ctx->arena,a,ctx->src);
                if (sv.str) {
                    const char *u = (char*)sv.str;
                    if (strstr(u,"ns")) unit = MQ_Time_ns;
                    else if (strstr(u,"us")) unit = MQ_Time_us;
                    else if (strstr(u,"ms")) unit = MQ_Time_ms;
                    else if (strstr(u,"\"s\"")||strstr(u,"'s'")) unit = MQ_Time_s;
                }
            }
        }
        return mq_instr_delay(qubits, qcnt, dur, unit);
    }

    /* ── measure_all ── special: no args, covers every qubit */
    if (strcmp(gate_buf,"measure_all") == 0)
    {
        /* Emit as a global barrier placeholder — the emitter recognises
         * qubit_count==0 as "all qubits" for barrier, and we reuse that
         * convention here too.  The MQ output is:  barrier <reg>;  */
        u32 dummy[1] = {0};
        return mq_instr_barrier(dummy, 0);
    }

    /* ── generic gate ── */
    MQ_GateType gate     = qk_gate_from_name_str(gate_buf);
    b8 has_ctrl          = qk_gate_has_implicit_ctrl(gate_buf);
    u8 expect_params     = qk_gate_expected_params(gate);

    u32 qubits[MQ_MAX_GATE_QUBITS] = {0}; u8 qcnt = 0;
    MQ_Expr *params[MQ_MAX_GATE_PARAMS]  = {0}; u8 pcnt = 0;
    u32 ctrl_qubit = 0; b8 got_ctrl = 0;

    u32 argc = ts_node_named_child_count(args_n);
    for (u32 i = 0; i < argc; i++)
    {
        TSNode a = ts_node_named_child(args_n,i);

        b8 is_sub = node_is(a,"subscript");
        b8 is_int = node_is(a,"integer");

        /* Is this a qubit arg?
         *   subscript → always a qubit
         *   integer   → qubit only after we've consumed all expected params */
        b8 is_qubit = is_sub || (is_int && pcnt >= expect_params);

        if (is_qubit)
        {
            u32 qi;
            if (is_sub)
                qi = qk_subscript_qubit_id(ctx, a);
            else { /* flat int */
                string sv = node_text(ctx->arena,a,ctx->src);
                qi = sv.str ? (u32)strtoul((char*)sv.str,NULL,10) : 0;
            }

            if (has_ctrl && !got_ctrl) { ctrl_qubit = qi; got_ctrl = 1; }
            else if (qcnt < MQ_MAX_GATE_QUBITS) qubits[qcnt++] = qi;
        }
        else
        {
            if (pcnt < MQ_MAX_GATE_PARAMS)
                params[pcnt++] = qk_lower_expr(ctx, a);
        }
    }

    /* Build instruction */
    string gate_name_str;
    {
        u8 *gn = (u8*)arena_alloc(ctx->arena, strlen(gate_buf)+1);
        memcpy(gn, gate_buf, strlen(gate_buf)+1);
        gate_name_str = (string){ .str = gn, .size = (u32)strlen(gate_buf) };
    }

    MQ_Instruction instr;
    if (gate == MQ_Gate_Custom)
    {
        /* Merge ctrl into qubit list for custom gates */
        u32 all[MQ_MAX_GATE_QUBITS]; u8 all_n = 0;
        if (got_ctrl && all_n < MQ_MAX_GATE_QUBITS) all[all_n++] = ctrl_qubit;
        for (u8 j = 0; j < qcnt && all_n < MQ_MAX_GATE_QUBITS; j++) all[all_n++] = qubits[j];
        instr = mq_instr_gate_custom(gate_name_str, all, all_n,
                                     pcnt ? params : NULL, pcnt);
    }
    else if (pcnt > 0)
    {
        instr = mq_instr_gate_sym(gate, qubits, qcnt, params, pcnt);
        if (got_ctrl) mq_instr_add_control(&instr, ctrl_qubit, 1);
    }
    else
    {
        instr = mq_instr_gate(gate, qubits, qcnt);
        if (got_ctrl) mq_instr_add_control(&instr, ctrl_qubit, 1);
    }

    TSPoint sp = ts_node_start_point(call_node);
    instr.source_line = sp.row + 1;
    instr.source_col  = sp.column;
    return instr;
}

/* ── noise / simulation call filter ─────────────────────────────────────── *
 * Returns 1 for calls that should be dropped (no quantum semantic content).
 * ─────────────────────────────────────────────────────────────────────────── */

static b8 qk_is_sim_call(const char *name)
{
    if (!name) return 0;
    return (strcmp(name,"AerSimulator")==0  ||
            strcmp(name,"transpile")==0      ||
            strcmp(name,"execute")==0        ||
            strcmp(name,"run")==0            ||
            strcmp(name,"result")==0         ||
            strcmp(name,"get_counts")==0     ||
            strcmp(name,"get_statevector")==0||
            strcmp(name,"get_memory")==0     ||
            strcmp(name,"print")==0          ||
            strcmp(name,"pprint")==0         ||
            strcmp(name,"StatevectorSimulator")==0 ||
            strcmp(name,"BasicAer")==0       ||
            strcmp(name,"Aer")==0);
}

/* ── host-only Python RHS filter ────────────────────────────────────────── *
 * Returns 1 for RHS expression node types that are pure Python host-side
 * constructs (comprehensions, containers, strings, lambdas) with no quantum
 * semantic meaning.  Assignments with such RHS are dropped to a comment.
 * ─────────────────────────────────────────────────────────────────────────── */

static b8 qk_is_host_only_rhs(TSNode rhs)
{
    if (node_null(rhs)) return 0;
    const char *t = ts_node_type(rhs);
    return (strcmp(t, "dictionary")              == 0 ||
            strcmp(t, "dictionary_comprehension") == 0 ||
            strcmp(t, "set")                     == 0 ||
            strcmp(t, "set_comprehension")       == 0 ||
            strcmp(t, "list")                    == 0 ||
            strcmp(t, "list_comprehension")      == 0 ||
            strcmp(t, "generator_expression")    == 0 ||
            strcmp(t, "lambda")                  == 0 ||
            strcmp(t, "concatenated_string")     == 0 ||
            strcmp(t, "string")                  == 0);
}

/* ── statement lowering ──────────────────────────────────────────────────── */

static MQ_Stmt *qk_lower_stmt(QK_Ctx *ctx, TSNode node)
{
    if (node_null(node)) return NULL;
    const char *type = ts_node_type(node);
    TSPoint sp = ts_node_start_point(node);

    /* ── imports → drop silently ── */
    if (strcmp(type,"import_statement")==0 ||
        strcmp(type,"import_from_statement")==0)
        return NULL;

    /* ── comments → drop silently ── */
    if (strcmp(type,"comment")==0)
        return NULL;

    /* ── expression_statement ── */
    if (strcmp(type,"expression_statement")==0)
    {
        TSNode expr = ts_node_named_child(node,0);
        if (node_null(expr)) return NULL;

        if (!node_is(expr,"call")) return NULL;

        /* Resolve callee name (bare or attribute.method) */
        TSNode fn_n = ts_node_child_by_field_name(expr,"function",8);
        char name_buf[64] = {0};
        if (node_is(fn_n,"attribute"))
        {
            TSNode attr = ts_node_child_by_field_name(fn_n,"attribute",9);
            string s    = node_text(ctx->arena, attr, ctx->src);
            u32 n = (s.size < sizeof(name_buf)-1) ? (u32)s.size : (u32)sizeof(name_buf)-1;
            if (s.str) { memcpy(name_buf,s.str,n); name_buf[n]='\0'; }
        }
        else
        {
            string s = node_text(ctx->arena, fn_n, ctx->src);
            u32 n = (s.size < sizeof(name_buf)-1) ? (u32)s.size : (u32)sizeof(name_buf)-1;
            if (s.str) { memcpy(name_buf,s.str,n); name_buf[n]='\0'; }
        }

        /* Drop simulation calls */
        if (qk_is_sim_call(name_buf))
            return NULL;

        /* measure_all() → pragma so emitter can expand it */
        if (strcmp(name_buf,"measure_all")==0) {
            MQ_Stmt *s = mq_stmt_pragma(ctx->arena,
                                        str_lit("measure_all"), str_lit("1"));
            s->source_line = sp.row+1;
            return s;
        }

        /* Quantum gate/measure/reset/barrier/delay */
        MQ_Instruction instr = qk_lower_call_to_instr(ctx, expr);

        /* A zero instruction (unknown call) → emit as comment */
        if (instr.type == MQ_Instr_Gate && instr.qubit_count == 0 &&
    instr.gate.custom_name.size == 0)
            return mq_stmt_comment(ctx->arena, node_text(ctx->arena,node,ctx->src));

        MQ_Stmt *s = mq_stmt_instr(ctx->arena, instr);
        s->source_line = sp.row+1;
        return s;
    }

    /* ── bare call (quantum gate method call) ── */
    if (strcmp(type,"call")==0)
    {
        /* Resolve callee name (bare or attribute.method) */
        TSNode fn_n = ts_node_child_by_field_name(node,"function",8);
        char name_buf[64] = {0};
        if (node_is(fn_n,"attribute"))
        {
            TSNode attr = ts_node_child_by_field_name(fn_n,"attribute",9);
            string s    = node_text(ctx->arena, attr, ctx->src);
            u32 n = (s.size < sizeof(name_buf)-1) ? (u32)s.size : (u32)sizeof(name_buf)-1;
            if (s.str) { memcpy(name_buf,s.str,n); name_buf[n]='\0'; }
        }
        else
        {
            string s = node_text(ctx->arena, fn_n, ctx->src);
            u32 n = (s.size < sizeof(name_buf)-1) ? (u32)s.size : (u32)sizeof(name_buf)-1;
            if (s.str) { memcpy(name_buf,s.str,n); name_buf[n]='\0'; }
        }

        /* Drop simulation calls */
        if (qk_is_sim_call(name_buf))
            return NULL;

        /* measure_all() → pragma so emitter can expand it */
        if (strcmp(name_buf,"measure_all")==0) {
            MQ_Stmt *s = mq_stmt_pragma(ctx->arena,
                                        str_lit("measure_all"), str_lit("1"));
            s->source_line = sp.row+1;
            return s;
        }

        /* Quantum gate/measure/reset/barrier/delay */
        MQ_Instruction instr = qk_lower_call_to_instr(ctx, node);

        /* A zero instruction (unknown call) → emit as comment */
        if (instr.type == MQ_Instr_Gate && instr.qubit_count == 0 &&
    instr.gate.custom_name.size == 0)
            return mq_stmt_comment(ctx->arena, node_text(ctx->arena,node,ctx->src));

        MQ_Stmt *s = mq_stmt_instr(ctx->arena, instr);
        s->source_line = sp.row+1;
        return s;
    }

    /* ── assignment ── */
    if (strcmp(type,"assignment")==0)
    {
        TSNode lhs_n = ts_node_child_by_field_name(node,"left",4);
        TSNode rhs_n = ts_node_child_by_field_name(node,"right",5);

        if (node_is(rhs_n,"call"))
        {
            TSNode callee = ts_node_child_by_field_name(rhs_n,"function",8);

            /* Resolve callee — may be an attribute (simulator.run) */
            char cn_buf[64] = {0};
            char method_buf[64] = {0};
            {
                string full = node_text(ctx->arena, callee, ctx->src);
                u32 n = (full.size < sizeof(cn_buf)-1) ? (u32)full.size : (u32)sizeof(cn_buf)-1;
                if (full.str) { memcpy(cn_buf,full.str,n); cn_buf[n]='\0'; }
            }
            if (node_is(callee,"attribute")) {
                TSNode attr = ts_node_child_by_field_name(callee,"attribute",9);
                string ms = node_text(ctx->arena, attr, ctx->src);
                u32 n = (ms.size < sizeof(method_buf)-1) ? (u32)ms.size : (u32)sizeof(method_buf)-1;
                if (ms.str) { memcpy(method_buf,ms.str,n); method_buf[n]='\0'; }
            }

            /* QuantumCircuit(n) or QuantumCircuit(n, m) → internal metadata only,
             * NO DeclQubit emitted into the body (avoids duplicate declarations).
             * The register table is populated here; the emitter reads it. */
            if (strcmp(cn_buf,"QuantumCircuit")==0)
            {
                TSNode args_n = ts_node_child_by_field_name(rhs_n,"arguments",9);
                u32 nq = 0, nc_bits = 0;
                u32 argc = ts_node_named_child_count(args_n);
                if (argc >= 1) {
                    string sv = node_text(ctx->arena, ts_node_named_child(args_n,0), ctx->src);
                    if (sv.str) nq = (u32)strtoul((char*)sv.str,NULL,10);
                }
                if (argc >= 2) {
                    string sv = node_text(ctx->arena, ts_node_named_child(args_n,1), ctx->src);
                    if (sv.str) nc_bits = (u32)strtoul((char*)sv.str,NULL,10);
                }

                /* Derive the circuit variable name (lhs identifier) */
                string circ_var = node_text(ctx->arena, lhs_n, ctx->src);

                if (ctx->circuit && nq > 0)
                {
                    /* Register already-named quantum register "q" (or circ_var) */
                   string qreg_name = circ_var;

                    /* Only add if not already present */
                    b8 already = false;
                    for (u32 ri = 0; ri < ctx->circuit->register_count; ri++) {
                        if (str_eq(ctx->circuit->registers[ri].name, qreg_name)) {
                            already = true; break;
                        }
                    }
                    if (!already)
                    {
                        u32 base = qk_ctx_add_register(ctx, qreg_name, nq);

                        MQ_QubitMeta *meta = (MQ_QubitMeta*)arena_alloc(
                            ctx->arena, sizeof(MQ_QubitMeta)*nq);
                        for (u32 qi = 0; qi < nq; qi++)
                            meta[qi] = (MQ_QubitMeta){
                                .id = base+qi, .style = MQ_Qubit_Named,
                                .name = qreg_name,
                                .register_name = qreg_name,
                                .register_index = qi };

                        u32 ri = ctx->circuit->register_count;
                        MQ_Register *new_regs = (MQ_Register*)arena_alloc(
                            ctx->arena, sizeof(MQ_Register)*(ri+1));
                        if (ri > 0)
                            memcpy(new_regs, ctx->circuit->registers, sizeof(MQ_Register)*ri);
                        new_regs[ri] = mq_register_quantum(qreg_name, nq, base, meta);
                        ctx->circuit->registers      = new_regs;
                        ctx->circuit->register_count = ri+1;
                        ctx->circuit->total_qubits  += nq;

                        /* Classical register if nc_bits given */
                        if (nc_bits > 0) {
                            string creg_name = str_lit("c");
                            u32 cri = ctx->circuit->register_count;
                            MQ_Register *cr = (MQ_Register*)arena_alloc(
                                ctx->arena, sizeof(MQ_Register)*(cri+1));
                            if (cri > 0)
                                memcpy(cr, ctx->circuit->registers, sizeof(MQ_Register)*cri);
                            cr[cri] = mq_register_classical(creg_name, nc_bits, base+nq);
                            ctx->circuit->registers      = cr;
                            ctx->circuit->register_count = cri+1;
                            ctx->circuit->total_cbits   += nc_bits;
                        }
                    }
                }
                /* Suppress from body — circuit var assignment is metadata, not code */
                (void)circ_var;
                return NULL;
            }

            /* QuantumRegister(n, 'name') */
            if (strcmp(cn_buf,"QuantumRegister")==0)
            {
                TSNode args_n = ts_node_child_by_field_name(rhs_n,"arguments",9);
                u32 nq = 0;
                if (ts_node_named_child_count(args_n) >= 1) {
                    string sv = node_text(ctx->arena, ts_node_named_child(args_n,0), ctx->src);
                    if (sv.str) nq = (u32)strtoul((char*)sv.str,NULL,10);
                }
                string var_name = node_text(ctx->arena, lhs_n, ctx->src);
                u32 base = qk_ctx_add_register(ctx, var_name, nq);

                if (ctx->circuit) {
                    b8 already = false;
                    for (u32 ri = 0; ri < ctx->circuit->register_count; ri++)
                        if (str_eq(ctx->circuit->registers[ri].name, var_name))
                            { already = true; break; }
                    if (!already) {
                        MQ_QubitMeta *meta = (MQ_QubitMeta*)arena_alloc(
                            ctx->arena, sizeof(MQ_QubitMeta)*nq);
                        for (u32 qi = 0; qi < nq; qi++)
                            meta[qi] = (MQ_QubitMeta){
                                .id = base+qi, .style = MQ_Qubit_Named,
                                .name = var_name,
                                .register_name = var_name,
                                .register_index = qi };
                        u32 ri = ctx->circuit->register_count;
                        MQ_Register *nr = (MQ_Register*)arena_alloc(
                            ctx->arena, sizeof(MQ_Register)*(ri+1));
                        if (ri > 0)
                            memcpy(nr, ctx->circuit->registers, sizeof(MQ_Register)*ri);
                        nr[ri] = mq_register_quantum(var_name, nq, base, meta);
                        ctx->circuit->registers      = nr;
                        ctx->circuit->register_count = ri+1;
                        ctx->circuit->total_qubits  += nq;
                    }
                }
                /* Suppress from body — metadata only */
                return NULL;
            }

            /* ClassicalRegister(n, 'name') */
            if (strcmp(cn_buf,"ClassicalRegister")==0)
            {
                TSNode args_n = ts_node_child_by_field_name(rhs_n,"arguments",9);
                u32 nb = 0;
                if (ts_node_named_child_count(args_n) >= 1) {
                    string sv = node_text(ctx->arena, ts_node_named_child(args_n,0), ctx->src);
                    if (sv.str) nb = (u32)strtoul((char*)sv.str,NULL,10);
                }
                string var_name = node_text(ctx->arena, lhs_n, ctx->src);
                if (ctx->circuit) {
                    u32 base_c = ctx->qubit_counter; /* cbits follow qubits */
                    u32 ri = ctx->circuit->register_count;
                    MQ_Register *nr = (MQ_Register*)arena_alloc(
                        ctx->arena, sizeof(MQ_Register)*(ri+1));
                    if (ri > 0)
                        memcpy(nr, ctx->circuit->registers, sizeof(MQ_Register)*ri);
                    nr[ri] = mq_register_classical(var_name, nb, base_c);
                    ctx->circuit->registers      = nr;
                    ctx->circuit->register_count = ri+1;
                    ctx->circuit->total_cbits   += nb;
                    /* Emit a DeclClassical so the emitter can print "creg name[n]" */
                    MQ_Type *t = mq_type_int(ctx->arena, (i32)nb);
                    t->width   = nb; /* width encodes register size for creg emitter */
                    MQ_Stmt *s = mq_stmt_decl_classical(ctx->arena, var_name, t, NULL);
                    s->source_line = sp.row+1;
                    return s;
                }
                return NULL;
            }

            /* Simulation boilerplate assignments → drop */
            if (qk_is_sim_call(cn_buf) || qk_is_sim_call(method_buf))
                return NULL;
        }

        if (qk_is_host_only_rhs(rhs_n))
            return NULL;


        /* Generic classical assignment */
        MQ_Expr *lhs = qk_lower_expr(ctx, lhs_n);
        MQ_Expr *rhs = qk_lower_expr(ctx, rhs_n);
        MQ_Stmt *s   = mq_stmt_set(ctx->arena, lhs, rhs);
        s->source_line = sp.row+1;
        return s;
    }

    /* ── augmented assignment: lhs op= rhs ── */
    if (strcmp(type,"augmented_assignment")==0)
    {
        TSNode lhs_n = ts_node_child_by_field_name(node,"left",4);
        TSNode op_n  = ts_node_child(node,1);
        TSNode rhs_n = ts_node_child_by_field_name(node,"right",5);
        MQ_Expr *lhs = qk_lower_expr(ctx, lhs_n);
        MQ_Expr *rhs = qk_lower_expr(ctx, rhs_n);
        string op    = node_text(ctx->arena, op_n, ctx->src);
        MQ_BinOp bop = MQ_BinOp_Add;
        if (op.str) {
            const char *s = (char*)op.str;
            if (strcmp(s,"+=")==0) bop=MQ_BinOp_Add;
            else if (strcmp(s,"-=")==0) bop=MQ_BinOp_Sub;
            else if (strcmp(s,"*=")==0) bop=MQ_BinOp_Mul;
            else if (strcmp(s,"/=")==0) bop=MQ_BinOp_Div;
            else if (strcmp(s,"%=")==0) bop=MQ_BinOp_Mod;
        }
        MQ_Stmt *st = mq_stmt_set_aug(ctx->arena, lhs, bop, rhs);
        st->source_line = sp.row+1;
        return st;
    }

    /* ── if ── */
    if (strcmp(type,"if_statement")==0)
    {
        TSNode cond_n = ts_node_named_child(node,0);
        TSNode body_n = ts_node_named_child(node,1);
        MQ_Expr *cond = qk_lower_expr(ctx, cond_n);
        MQ_Stmt *body = qk_lower_block(ctx, body_n);

        /* elif / else via "alternative" field */
        TSNode alt = ts_node_child_by_field_name(node,"alternative",11);
        if (!node_null(alt))
        {
            MQ_IfBranch *brs = (MQ_IfBranch*)arena_alloc(ctx->arena, sizeof(MQ_IfBranch)*2);
            brs[0] = mq_if_branch(cond, body);
            if (node_is(alt,"elif_clause")) {
                TSNode ec = ts_node_named_child(alt,0);
                TSNode eb = ts_node_named_child(alt,1);
                brs[1] = mq_if_branch(qk_lower_expr(ctx,ec), qk_lower_block(ctx,eb));
            } else { /* else_clause */
                brs[1] = mq_else_branch(qk_lower_block(ctx, ts_node_named_child(alt,0)));
            }
            MQ_Stmt *s = mq_stmt_if(ctx->arena, brs, 2);
            s->source_line = sp.row+1;
            return s;
        }
        MQ_IfBranch *br = (MQ_IfBranch*)arena_alloc(ctx->arena, sizeof(MQ_IfBranch));
        *br = mq_if_branch(cond, body);
        MQ_Stmt *s = mq_stmt_if(ctx->arena, br, 1);
        s->source_line = sp.row+1;
        return s;
    }

    /* ── for ── */
    if (strcmp(type,"for_statement")==0)
    {
        TSNode lhs_n  = ts_node_child_by_field_name(node,"left",4);
        TSNode iter_n = ts_node_child_by_field_name(node,"right",5);
        TSNode body_n = ts_node_child_by_field_name(node,"body",4);
        string var_name = node_null(lhs_n) ? str_lit("_") :
                          node_text(ctx->arena, lhs_n, ctx->src);
        MQ_Stmt *s = mq_stmt_for(ctx->arena, var_name, NULL,
                                  qk_lower_expr(ctx, iter_n),
                                  qk_lower_block(ctx, body_n));
        s->source_line = sp.row+1;
        return s;
    }

    /* ── while ── */
    if (strcmp(type,"while_statement")==0)
    {
        TSNode cond_n = ts_node_named_child(node,0);
        TSNode body_n = ts_node_named_child(node,1);
        MQ_Stmt *s = mq_stmt_while(ctx->arena,
                                    qk_lower_expr(ctx, cond_n),
                                    qk_lower_block(ctx, body_n));
        s->source_line = sp.row+1;
        return s;
    }

    /* ── return ── */
    if (strcmp(type,"return_statement")==0)
    {
        TSNode val_n = ts_node_named_child(node,0);
        MQ_Stmt *s = mq_stmt_return(ctx->arena,
                                     node_null(val_n)?NULL:qk_lower_expr(ctx,val_n));
        s->source_line = sp.row+1;
        return s;
    }

    /* ── break / continue ── */
    if (strcmp(type,"break_statement")==0)   return mq_stmt_break(ctx->arena);
    if (strcmp(type,"continue_statement")==0) return mq_stmt_continue(ctx->arena);

    /* ── function_definition → lower body as a routine ── */
    if (strcmp(type,"function_definition")==0)
    {
        TSNode name_n   = find_child(node,"identifier");
        TSNode params_n = find_child(node,"parameters");
        TSNode body_n   = find_child(node,"block");

        string fn_name = node_null(name_n) ? str_lit("unnamed")
                                           : node_text(ctx->arena, name_n, ctx->src);

        /* Lower parameters */
        u32 pcount = 0;
        MQ_FormalParam *params = NULL;
        if (!node_null(params_n))
        {
            u32 pn = ts_node_named_child_count(params_n);
            params = (MQ_FormalParam*)arena_alloc(ctx->arena, sizeof(MQ_FormalParam)*pn);
            for (u32 i = 0; i < pn; i++)
            {
                TSNode p = ts_node_named_child(params_n,i);
                string pname = node_text(ctx->arena, p, ctx->src);
                /* Default type: Float (most quantum params are angles) */
                MQ_Type *pt = mq_type_scalar(ctx->arena, MQ_Type_Float);
                params[pcount++] = mq_formal_param(pname, pt, NULL);
            }
        }

        /* Save / restore circuit context so the routine body is clean */
        MQ_Circuit *saved_circuit = ctx->circuit;
        u32         saved_qctr    = ctx->qubit_counter;
        u32         saved_rcnt    = ctx->reg_count;
        ctx->circuit       = NULL;
        ctx->qubit_counter = 0;
        ctx->reg_count     = 0;

        MQ_Stmt *body = qk_lower_block(ctx, body_n);

        ctx->circuit       = saved_circuit;
        ctx->qubit_counter = saved_qctr;
        ctx->reg_count     = saved_rcnt;

        MQ_Routine *routine = mq_routine(ctx->arena, fn_name,
                                          MQ_Routine_Operation,
                                          params, pcount, NULL, body);
        routine->source_line = sp.row+1;
        mq_program_add_routine(ctx->arena, ctx->prog, routine);
        return NULL; /* routines don't appear inline in the body */
    }

    /* Unknown / unhandled — silent drop */
    return NULL;
}

/* ── block lowering ─────────────────────────────────────────────────────── */

static MQ_Stmt *qk_lower_block(QK_Ctx *ctx, TSNode block_node)
{
    if (node_null(block_node)) return NULL;

    u32 total = ts_node_child_count(block_node);
    MQ_Stmt **stmts = (MQ_Stmt**)arena_alloc(ctx->arena, sizeof(MQ_Stmt*)*(total+1));
    u32 count = 0;

    for (u32 i = 0; i < total; i++)
    {
        TSNode child = ts_node_child(block_node, i);
        if (!ts_node_is_named(child)) continue; /* skip punctuation */
        MQ_Stmt *s = qk_lower_stmt(ctx, child);
        if (s) stmts[count++] = s;
    }
    return mq_stmt_block(ctx->arena, stmts, count);
}

/* ── circuit lowering ───────────────────────────────────────────────────── *
 *
 * Scans the module-level block once, collecting:
 *   - QuantumCircuit / QuantumRegister / ClassicalRegister → register metadata
 *   - gate calls, measure, reset, barrier → body statements
 *   - simulation boilerplate                → dropped (commented)
 *   - function_definition                  → MQ_Routine (added to prog)
 *
 * One MQ_Circuit ("main") is created and added to prog.
 * ─────────────────────────────────────────────────────────────────────────── */

static void qk_lower_circuit(QK_Ctx *ctx, TSNode module_node)
{
    /* Pre-scan: find the QuantumCircuit(...) assignment to extract circuit name. */
    string circ_name = str_lit("main");
    {
        u32 total = ts_node_child_count(module_node);
        for (u32 i = 0; i < total; i++)
        {
            TSNode child = ts_node_child(module_node, i);
            if (!ts_node_is_named(child)) continue;
            if (!node_is(child, "assignment")) continue;

            TSNode rhs_n = ts_node_child_by_field_name(child, "right", 5);
            if (node_null(rhs_n) || !node_is(rhs_n, "call")) continue;

            TSNode callee = ts_node_child_by_field_name(rhs_n, "function", 8);
            string cn = node_text(ctx->arena, callee, ctx->src);
            if (!cn.str) continue;

            char cn_buf[64] = {0};
            u32 n = (cn.size < sizeof(cn_buf)-1) ? (u32)cn.size : (u32)sizeof(cn_buf)-1;
            memcpy(cn_buf, cn.str, n);
            if (strcmp(cn_buf, "QuantumCircuit") != 0) continue;

            TSNode lhs_n = ts_node_child_by_field_name(child, "left", 4);
            if (!node_null(lhs_n))
                circ_name = node_text(ctx->arena, lhs_n, ctx->src);
            break;
        }
    }

    /* Create the circuit shell with zero qubits; register scanning will fill it. */
    MQ_Circuit *circ = mq_circuit(ctx->arena, circ_name, NULL, 0, 0, 0);
    ctx->circuit = circ;

    /* Lower the whole module body — register decls populate ctx->circuit
     * internally and return NULL; gate calls return MQ_Stmt_Instr nodes. */
    MQ_Stmt *body = qk_lower_block(ctx, module_node);
    circ->body = body;

    /* Finalise measure_map. */
    if (circ->total_qubits > 0)
    {
        circ->measure_map = (i32*)arena_alloc(ctx->arena,
                                               sizeof(i32)*circ->total_qubits);
        for (u32 qi = 0; qi < circ->total_qubits; qi++)
            circ->measure_map[qi] = -1;
    }

    mq_program_add_circuit(ctx->arena, ctx->prog, circ);
    ctx->circuit = NULL;
}

/* ── public entry points ────────────────────────────────────────────────── */

MQ_Program *qiskit_tree_to_ir(const Qiskit_ParseResult *result, M_Arena *arena)
{
    if (!result || !result->ok || !result->tree)
    {
        fprintf(stderr, "[qiskit] qiskit_tree_to_ir: invalid parse result\n");
        return NULL;
    }

    TSNode root = ts_tree_root_node(result->tree);

    MQ_Program *prog = mq_program(arena, str_lit("QiskitProgram"), MQ_Lang_Qiskit);
    prog->mq_ir_version[0] = MQ_IR_VERSION_MAJOR;
    prog->mq_ir_version[1] = MQ_IR_VERSION_MINOR;
    prog->mq_ir_version[2] = MQ_IR_VERSION_PATCH;

    QK_Ctx ctx = {
        .arena         = arena,
        .src           = result->source,
        .prog          = prog,
        .circuit       = NULL,
        .qubit_counter = 0,
        .reg_count     = 0,
    };

    qk_lower_circuit(&ctx, root);
    return prog;
}

MQ_Program *qiskit_parse(M_Arena *arena, string src)
{
    if (!src.str || src.size == 0)
    {
        fprintf(stderr, "[qiskit] qiskit_parse: empty source\n");
        return NULL;
    }

    /* ── Pass 1: run Tree-sitter ── */
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "[qiskit] ts_parser_new() failed\n");
        return NULL;
    }
    if (!ts_parser_set_language(parser, tree_sitter_python())) {
        fprintf(stderr, "[qiskit] ts_parser_set_language() failed\n");
        ts_parser_delete(parser);
        return NULL;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL,
                                          (const char*)src.str, (uint32_t)src.size);
    if (!tree) {
        fprintf(stderr, "[qiskit] parse returned NULL\n");
        ts_parser_delete(parser);
        return NULL;
    }

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_has_error(root))
        fprintf(stderr, "[qiskit] warning: parse errors in source\n");

    /* ── Pass 3: lower CST → IR ── */
    MQ_Program *prog = mq_program(arena, str_lit("QiskitProgram"), MQ_Lang_Qiskit);
    prog->mq_ir_version[0] = MQ_IR_VERSION_MAJOR;
    prog->mq_ir_version[1] = MQ_IR_VERSION_MINOR;
    prog->mq_ir_version[2] = MQ_IR_VERSION_PATCH;

    QK_Ctx ctx = {
        .arena         = arena,
        .src           = (const char*)src.str,
        .prog          = prog,
        .circuit       = NULL,
        .qubit_counter = 0,
        .reg_count     = 0,
    };

    qk_lower_circuit(&ctx, root);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return prog;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §0  Qubit-id → name table  (per-scope, mirrors QS_QMapEntry in qsharp.c)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Each entry covers a contiguous range [base, base+size):
 *   size == 0  → single bare qubit  (emitted as "name")
 *   size  > 0  → qubit register     (emitted as "name[id - base]")
 *
 * The table is a module-level singleton that is reset and re-populated for
 * every circuit / routine scope, exactly as in qsharp.c.
 * ─────────────────────────────────────────────────────────────────────────── */
 
#define QK_QMAP_MAX 32
 
typedef struct
{
    u32  base;
    u32  size;
    char name[64];
} QK_QMapEntry;
 
static QK_QMapEntry s_qkmap[QK_QMAP_MAX];
static u32          s_qkmap_count = 0;
 
static void qkmap_reset(void) { s_qkmap_count = 0; }
 
static void qkmap_add(u32 base, u32 size, const u8 *name_str, u32 name_len)
{
    if (s_qkmap_count >= QK_QMAP_MAX) return;
    QK_QMapEntry *e = &s_qkmap[s_qkmap_count++];
    e->base = base;
    e->size = size;
    u32 nlen = (name_len < 63u) ? name_len : 63u;
    memcpy(e->name, name_str, nlen);
    e->name[nlen] = '\0';
}
 
/* Resolve a qubit flat-id to "reg[index]" (or bare "reg" for singletons).
 * Used for metalanguage emission. */
static void qk_emit_qubit(FILE *out, u32 qubit_id)
{
    /* Single-qubit entries first (size == 0 → exact match). */
    for (u32 i = 0; i < s_qkmap_count; i++)
    {
        QK_QMapEntry *e = &s_qkmap[i];
        if (e->size == 0 && e->base == qubit_id)
        {
            fprintf(out, "%s", e->name);
            return;
        }
    }
    /* Array entries. */
    for (u32 i = 0; i < s_qkmap_count; i++)
    {
        QK_QMapEntry *e = &s_qkmap[i];
        if (e->size > 0 && qubit_id >= e->base && qubit_id < e->base + e->size)
        {
            fprintf(out, "%s[%u]", e->name, qubit_id - e->base);
            return;
        }
    }
    /* Fallback — should not occur in well-formed IR. */
    fprintf(out, "q[%u]", qubit_id);
}

/* For Qiskit Python: emit just the integer qubit index, not register names. */
static void qk_emit_qubit_py(FILE *out, u32 qubit_id)
{
    fprintf(out, "%u", qubit_id);
}
 
/* Return the name of the first array register in the current scope.
 * Used when a barrier covers all qubits and no individual ids are stored. */
static const char *qkmap_first_array_name(void)
{
    for (u32 i = 0; i < s_qkmap_count; i++)
        if (s_qkmap[i].size > 0) return s_qkmap[i].name;
    return "q";
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §1  Indentation helper
 * ═══════════════════════════════════════════════════════════════════════════ */
 
static void qk_indent(FILE *out, u32 level)
{
    for (u32 i = 0; i < level; i++)
        fprintf(out, "    ");
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §2  Expression emitter
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * want_float: when true, integer literals are printed with a ".0" suffix so
 * that downstream consumers that enforce typed angles do not choke.
 * ─────────────────────────────────────────────────────────────────────────── */
 
static void qk_emit_expr(FILE *out, MQ_Expr *e, b8 want_float);
 
static void qk_emit_expr(FILE *out, MQ_Expr *e, b8 want_float)
{
    if (!e)
    {
        fprintf(out, "0");
        return;
    }
 
    switch (e->kind)
    {
 
    case MQ_Expr_BoolLit:
        fprintf(out, "%s", e->lit.bool_val ? "True" : "False");
        break;
 
    case MQ_Expr_IntLit:
        if (want_float)
            fprintf(out, "%lld.0", (long long)e->lit.int_val);
        else
            fprintf(out, "%lld", (long long)e->lit.int_val);
        break;
 
    case MQ_Expr_FloatLit:
    {
        double v = e->lit.float_val;
        if (v == (double)(long long)v)
            fprintf(out, "%.1f", v);
        else
            fprintf(out, "%.17g", v);
        break;
    }
 
    case MQ_Expr_Symbol:
    case MQ_Expr_Var:
        /* Translate common Python/Qiskit angle constants. */
        if (e->name.str)
        {
            const char *n = (const char *)e->name.str;
            if (strcmp(n, "pi") == 0 || strcmp(n, "PI") == 0)
            {
                fprintf(out, "pi");
                break;
            }
            if (strcmp(n, "np.pi") == 0 || strcmp(n, "math.pi") == 0)
            {
                fprintf(out, "pi");
                break;
            }
        }
        fprintf(out, "%.*s", (int)e->name.size, e->name.str);
        break;
 
    case MQ_Expr_QubitRef:
        qk_emit_qubit_py(out, e->qubit_id);
        break;
 
    case MQ_Expr_RegIndex:
        fprintf(out, "%.*s[", (int)e->reg.name.size, e->reg.name.str);
        qk_emit_expr(out, e->reg.index_expr, false);
        fprintf(out, "]");
        break;
 
    case MQ_Expr_BinOp:
    {
        /* Range sentinel (re-used MQ_BinOp_Shl slot in qsharp.c convention). */
        if (e->bin.op == MQ_BinOp_Shl)
        {
            qk_emit_expr(out, e->bin.lhs, false);
            fprintf(out, "..");
            qk_emit_expr(out, e->bin.rhs, false);
            break;
        }
 
        static const char *ops[] = {
            "+", "-", "*", "/", "%", "**",
            "&",  "|",  "^",  "<<", ">>",
            "==", "!=", "<",  "<=", ">", ">=",
            "&&", "||"};
 
        /* Propagate float context through arithmetic when either child is. */
#define EXPR_IS_FLOAT(x) (                                       \
    (x) && ((x)->kind == MQ_Expr_FloatLit ||                     \
            ((x)->kind == MQ_Expr_Var && (x)->name.str &&        \
             (strcmp((char *)(x)->name.str, "pi")  == 0 ||       \
              strcmp((char *)(x)->name.str, "PI")  == 0 ||       \
              strcmp((char *)(x)->name.str, "np.pi") == 0))))
 
        b8 is_arith = (e->bin.op == MQ_BinOp_Add ||
                       e->bin.op == MQ_BinOp_Sub ||
                       e->bin.op == MQ_BinOp_Mul ||
                       e->bin.op == MQ_BinOp_Div);
        b8 child_float = want_float;
        if (is_arith && (EXPR_IS_FLOAT(e->bin.lhs) || EXPR_IS_FLOAT(e->bin.rhs)))
            child_float = true;
 
#undef EXPR_IS_FLOAT
 
        fprintf(out, "(");
        qk_emit_expr(out, e->bin.lhs, child_float);
        fprintf(out, " %s ", ops[e->bin.op]);
        qk_emit_expr(out, e->bin.rhs, child_float);
        fprintf(out, ")");
        break;
    }
 
    case MQ_Expr_UnOp:
    {
        static const char *math_fns[] = {
            /* 0:Neg  1:Not  2:BitNot  then math below */
            "", "", "",
            "sin", "cos", "tan",
            "asin", "acos", "atan",
            "sqrt", "exp", "log", "abs"};
 
        if (e->un.op == MQ_UnOp_Neg)
        {
            fprintf(out, "-");
            qk_emit_expr(out, e->un.operand, want_float);
        }
        else if (e->un.op == MQ_UnOp_Not)
        {
            fprintf(out, "!");
            qk_emit_expr(out, e->un.operand, false);
        }
        else if (e->un.op == MQ_UnOp_BitNot)
        {
            fprintf(out, "~");
            qk_emit_expr(out, e->un.operand, false);
        }
        else
        {
            u32 idx = (u32)e->un.op;
            fprintf(out, "%s(", math_fns[idx]);
            qk_emit_expr(out, e->un.operand, true);
            fprintf(out, ")");
        }
        break;
    }
 
    case MQ_Expr_Call:
    {
        fprintf(out, "%.*s(", (int)e->call.name.size, e->call.name.str);
        for (u32 i = 0; i < e->call.arg_count; i++)
        {
            if (i > 0) fprintf(out, ", ");
            qk_emit_expr(out, e->call.args[i], want_float);
        }
        fprintf(out, ")");
        break;
    }
 
    case MQ_Expr_Array:
    {
        fprintf(out, "[");
        for (u32 i = 0; i < e->call.arg_count; i++)
        {
            if (i > 0) fprintf(out, ", ");
            qk_emit_expr(out, e->call.args[i], false);
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
        fprintf(out, "/* ?expr */");
        break;
    }
}
 
/* Convenience wrappers matching qsharp.c naming pattern. */
static void qk_emit_expr_float(FILE *out, MQ_Expr *e) { qk_emit_expr(out, e, true);  }
static void qk_emit_expr_any  (FILE *out, MQ_Expr *e) { qk_emit_expr(out, e, false); }
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §3  Gate name table  (MQ_GateType → metalanguage mnemonic)
 * ═══════════════════════════════════════════════════════════════════════════ */
 
static const char *qk_gate_name(MQ_GateType g)
{
    switch (g)
    {
    case MQ_Gate_I:    return "id";
    case MQ_Gate_H:    return "h";
    case MQ_Gate_X:    return "x";
    case MQ_Gate_Y:    return "y";
    case MQ_Gate_Z:    return "z";
    case MQ_Gate_S:    return "s";
    case MQ_Gate_Sdg:  return "sdg";
    case MQ_Gate_T:    return "t";
    case MQ_Gate_Tdg:  return "tdg";
    case MQ_Gate_P:    return "p";
    case MQ_Gate_RX:   return "rx";
    case MQ_Gate_RY:   return "ry";
    case MQ_Gate_RZ:   return "rz";
    case MQ_Gate_U:    return "u";
    //case MQ_Gate_CX:   return "cx";
    case MQ_Gate_SWAP: return "swap";
    case MQ_Gate_ISWAP:return "iswap";
    case MQ_Gate_RZZ:  return "rzz";
    case MQ_Gate_RXX:  return "rxx";
    case MQ_Gate_RYY:  return "ryy";
    case MQ_Gate_CCX:  return "ccx";
    case MQ_Gate_CSWAP:return "cswap";
    default:           return "gate";
    }
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §4  Instruction emitter
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Metalanguage gate syntax:
 *
 *   Non-parametric, no control:    GateName q[i], q[j];
 *   Parametric, no control:        GateName(angle) q[i];
 *   Non-parametric, controlled:    C-GateName q[ctrl], q[tgt];
 *   Parametric, controlled:        C-GateName(angle) q[ctrl], q[tgt];
 *   Multi-control:                 CC-GateName q[c0], q[c1], q[tgt];
 *
 * Special forms:
 *   measure q[i] -> c[j];
 *   reset   q[i];
 *   barrier q[i], q[j], …;
 * ─────────────────────────────────────────────────────────────────────────── */
 
static void qk_emit_gate_param(FILE *out, MQ_Instruction *in, u8 pi)
{
    if (in->gate.params_symbolic)
        qk_emit_expr_float(out, in->gate.param_exprs[pi]);
    else
    {
        double v = in->gate.params[pi];
        if (v == (double)(long long)v)
            fprintf(out, "%.1f", v);
        else
            fprintf(out, "%.17g", v);
    }
}
 
static void qk_emit_instr(FILE *out, MQ_Instruction *in, u32 level)
{
    switch (in->type)
    {
 
    /* ── gate ──────────────────────────────────────────────────────────── */
    case MQ_Instr_Gate:
    {
        qk_indent(out, level);
        
        /* Output: qc.gateName(...) */
        fprintf(out, "qc.");
 
        /* Resolve the gate name string. */
        const char *gname;
        char        custom_buf[128];
 
        if (in->gate.gate == MQ_Gate_Custom)
        {
            u32 csz = (u32)in->gate.custom_name.size;
            u32 copy = (csz < sizeof(custom_buf) - 1) ? csz
                                                       : (u32)sizeof(custom_buf) - 1;
            if (in->gate.custom_name.str)
                memcpy(custom_buf, in->gate.custom_name.str, copy);
            custom_buf[copy] = '\0';
 
            /* Lower-case for Python method names (e.g., h, cx, rx). */
            for (u32 ci = 0; ci < copy; ci++)
                if (custom_buf[ci] >= 'A' && custom_buf[ci] <= 'Z')
                    custom_buf[ci] = (char)(custom_buf[ci] + 32);
 
            gname = custom_buf;
        }
        else
        {
            gname = qk_gate_name(in->gate.gate);
        }
 
        /* Build the controlled gate suffix from control_count. */
        u8 nctrl = in->gate.control_count;
        if (nctrl > 0)
        {
            /* Controlled gates: c (1 control), cc (2 controls), etc. */
            for (u8 ci = 0; ci < nctrl; ci++)
                fprintf(out, "c");
        }
 
        /* Gate name (lowercase for Qiskit Python method). */
        fprintf(out, "%s(", gname);
 
        /* Parameter list (if any). */
        if (in->gate.param_count > 0)
        {
            for (u8 pi = 0; pi < in->gate.param_count; pi++)
            {
                if (pi > 0) fprintf(out, ", ");
                qk_emit_gate_param(out, in, pi);
            }
            fprintf(out, ", ");
        }
 
        /* Control qubits first. */
        for (u8 ci = 0; ci < nctrl; ci++)
        {
            qk_emit_qubit_py(out, in->gate.controls[ci]);
            fprintf(out, ", ");
        }
 
        /* Target qubits. */
        for (u8 qi = 0; qi < in->qubit_count; qi++)
        {
            if (qi > 0) fprintf(out, ", ");
            qk_emit_qubit_py(out, in->qubits[qi]);
        }
 
        fprintf(out, ")\n");
        break;
    }
 
    /* ── measure ───────────────────────────────────────────────────────── */
    case MQ_Instr_Measure:
    {
        qk_indent(out, level);
        fprintf(out, "qc.measure(");
        qk_emit_qubit_py(out, in->qubits[0]);
        fprintf(out, ")\n");
        break;
    }
 
    /* ── reset ─────────────────────────────────────────────────────────── */
    case MQ_Instr_Reset:
    {
        qk_indent(out, level);
        fprintf(out, "qc.reset(");
        qk_emit_qubit_py(out, in->qubits[0]);
        fprintf(out, ")\n");
        break;
    }
 
    /* ── barrier ───────────────────────────────────────────────────────── */
    case MQ_Instr_Barrier:
    {
        qk_indent(out, level);
        fprintf(out, "qc.barrier(");
        if (in->qubit_count == 0)
        {
            /* Global barrier — emit over all qubits. */
            fprintf(out, ")");
        }
        else
        {
            for (u8 qi = 0; qi < in->qubit_count; qi++)
            {
                if (qi > 0) fprintf(out, ", ");
                qk_emit_qubit_py(out, in->qubits[qi]);
            }
            fprintf(out, ")");
        }
        fprintf(out, "\n");
        break;
    }
 
    /* ── delay ─────────────────────────────────────────────────────────── */
    case MQ_Instr_Delay:
    {
        qk_indent(out, level);
        fprintf(out, "qc.delay(");
        if (in->gate.params_symbolic && in->gate.param_count > 0)
            qk_emit_expr_float(out, in->gate.param_exprs[0]);
        else if (in->gate.param_count > 0)
            fprintf(out, "%.17g", in->gate.params[0]);
        else
            fprintf(out, "0");
        fprintf(out, ")");
        if (in->qubit_count > 0)
        {
            fprintf(out, "  # on ");
            qk_emit_qubit_py(out, in->qubits[0]);
        }
        fprintf(out, "\n");
        break;
    }
 
    default:
        qk_indent(out, level);
        fprintf(out, "# unsupported_instr(%d)\n", (int)in->type);
        break;
    }
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §5  Statement emitter  (mutually recursive with §4)
 * ═══════════════════════════════════════════════════════════════════════════ */
 
static void qk_emit_stmt(FILE *out, MQ_Stmt *s, u32 level);
 
/* ── §5a  classical-type annotation helper ────────────────────────────── */
 
static const char *qk_type_str(MQ_Type *t)
{
    if (!t) return "int";
    switch (t->kind)
    {
    case MQ_Type_Bool:     return "bool";
    case MQ_Type_Int:      return "int";
    case MQ_Type_Float:
    case MQ_Type_Angle:    return "float";
    case MQ_Type_QubitReg: return "qubit[]";
    case MQ_Type_Qubit:    return "qubit";
    default:               return "int";
    }
}
 
/* ── §5b  parameter type annotation for routine signatures ────────────── */
 
static const char *qk_param_type_str(MQ_FormalParam *p)
{
    if (!p->type) return "float";
    switch (p->type->kind)
    {
    case MQ_Type_QubitReg: return "qubit[]";
    case MQ_Type_Qubit:    return "qubit";
    case MQ_Type_Bool:     return "bool";
    case MQ_Type_Int:      return "int";
    case MQ_Type_Float:
    case MQ_Type_Angle:    return "float";
    default:               return "float";
    }
}
 
/* ── §5c  mutable flag (mirrors qsharp.c QS_MUTABLE_FLAG convention) ──── */
#define QK_MUTABLE_FLAG (1u << 31)
 
/* ── §5d  main statement emitter ──────────────────────────────────────── */
 
static void qk_emit_stmt(FILE *out, MQ_Stmt *s, u32 level)
{
    if (!s) return;
 
    switch (s->kind)
    {
 
    /* ── block: recurse into children ─────────────────────────────────── */
    case MQ_Stmt_Block:
        for (u32 i = 0; i < s->block.count; i++)
            qk_emit_stmt(out, s->block.stmts[i], level);
        break;
 
    /* ── qubit declaration ────────────────────────────────────────────── *
     *
     * In Python/Qiskit, we already created the QuantumCircuit(n) in the
     * circuit function signature, so we just skip these declarations.
     * They were needed for the IR but not for Python emission.
     * ─────────────────────────────────────────────────────────────────── */
    case MQ_Stmt_DeclQubit:
    {
        /* Skip — qubits were already allocated in circuit initialization. */
        break;
    }
 
    /* ── classical variable declaration ──────────────────────────────── */
    case MQ_Stmt_DeclClassical:
    {
        MQ_Type *t = s->decl_classical.type;
        qk_indent(out, level);
        fprintf(out, "%.*s",
                (int)s->decl_classical.name.size,
                s->decl_classical.name.str);
        if (s->decl_classical.init)
        {
            fprintf(out, " = ");
            qk_emit_expr_any(out, s->decl_classical.init);
        }
        fprintf(out, "\n");
        break;
    }
 
    /* ── classical assignment ─────────────────────────────────────────── */
    case MQ_Stmt_Set:
    {
        qk_indent(out, level);
        qk_emit_expr_any(out, s->set.lhs);
        if (s->set.augmented)
        {
            static const char *aug_ops[] = {
                "+=", "-=", "*=", "/=", "%=", "**=",
                "&=", "|=", "^=", "<<=", ">>=",
                "", "", "", "", "", "", "", ""};
            fprintf(out, " %s ", aug_ops[s->set.aug_op]);
        }
        else
        {
            fprintf(out, " = ");
        }
        qk_emit_expr_any(out, s->set.rhs);
        fprintf(out, "\n");
        break;
    }
 
    /* ── quantum instruction ──────────────────────────────────────────── */
    case MQ_Stmt_Instr:
        qk_emit_instr(out, &s->instr, level);
        break;
 
    /* ── adjoint wrapper ──────────────────────────────────────────────── *
     * (Not directly used in Python Qiskit; emit as comment)
     * ─────────────────────────────────────────────────────────────────── */
    case MQ_Stmt_Adjoint:
        qk_indent(out, level);
        fprintf(out, "# adjoint\n");
        qk_emit_stmt(out, s->adjoint_body, level);
        break;
 
    /* ── comment ──────────────────────────────────────────────────────── */
    case MQ_Stmt_Comment:
        qk_indent(out, level);
        fprintf(out, "#%.*s\n",
                (int)s->comment_text.size, s->comment_text.str);
        break;
 
    /* ── generic call statement ───────────────────────────────────────── */
    case MQ_Stmt_Call:
    {
        qk_indent(out, level);
        fprintf(out, "%.*s(",
                (int)s->call.callee.size, s->call.callee.str);
        for (u32 i = 0; i < s->call.arg_count; i++)
        {
            if (i > 0) fprintf(out, ", ");
            qk_emit_expr_any(out, s->call.args[i]);
        }
        fprintf(out, ")\n");
        break;
    }
 
    /* ── if / elif / else ─────────────────────────────────────────────── */
    case MQ_Stmt_If:
        for (u32 i = 0; i < s->if_stmt.count; i++)
        {
            MQ_IfBranch *br = &s->if_stmt.branches[i];
            qk_indent(out, level);
            if (!br->cond)
            {
                fprintf(out, "else:\n");
            }
            else
            {
                fprintf(out, i == 0 ? "if " : "elif ");
                qk_emit_expr_any(out, br->cond);
                fprintf(out, ":\n");
            }
            qk_emit_stmt(out, br->body, level + 1);
        }
        break;
 
    /* ── for loop ─────────────────────────────────────────────────────── */
    case MQ_Stmt_For:
    {
        /* Save qkmap so DeclQubit inside a loop body doesn't leak. */
        u32          saved_count = s_qkmap_count;
        QK_QMapEntry saved_map[QK_QMAP_MAX];
        memcpy(saved_map, s_qkmap, sizeof(QK_QMapEntry) * s_qkmap_count);
 
        qk_indent(out, level);
        fprintf(out, "for %.*s in ",
                (int)s->for_loop.var_name.size,
                s->for_loop.var_name.str);
        qk_emit_expr_any(out, s->for_loop.iterable);
        fprintf(out, ":\n");
        qk_emit_stmt(out, s->for_loop.body, level + 1);
 
        s_qkmap_count = saved_count;
        memcpy(s_qkmap, saved_map, sizeof(QK_QMapEntry) * saved_count);
        break;
    }
 
    /* ── while loop ───────────────────────────────────────────────────── */
    case MQ_Stmt_While:
        qk_indent(out, level);
        fprintf(out, "while ");
        qk_emit_expr_any(out, s->while_loop.cond);
        fprintf(out, ":\n");
        qk_emit_stmt(out, s->while_loop.body, level + 1);
        break;
 
    /* ── break / continue ─────────────────────────────────────────────── */
    case MQ_Stmt_Break:
        qk_indent(out, level);
        fprintf(out, "break\n");
        break;
 
    case MQ_Stmt_Continue:
        qk_indent(out, level);
        fprintf(out, "continue\n");
        break;
 
    /* ── return ───────────────────────────────────────────────────────── */
    case MQ_Stmt_Return:
        qk_indent(out, level);
        fprintf(out, "return");
        if (s->return_val)
        {
            fprintf(out, " ");
            qk_emit_expr_any(out, s->return_val);
        }
        fprintf(out, "\n");
        break;
 
    /* ── pragma ───────────────────────────────────────────────────────── */
    case MQ_Stmt_Pragma:
        qk_indent(out, level);
        /* Special handling for measure_all pragma. */
        if (s->pragma.key.size > 0 && s->pragma.key.str)
        {
            if (strstr((const char *)s->pragma.key.str, "measure_all") != NULL)
            {
                fprintf(out, "qc.measure_all()\n");
            }
            else
            {
                fprintf(out, "# pragma %.*s %.*s\n",
                        (int)s->pragma.key.size,   s->pragma.key.str,
                        (int)s->pragma.value.size, s->pragma.value.str);
            }
        }
        break;
 
    default:
        qk_indent(out, level);
        fprintf(out, "# unhandled_stmt(%d)\n", (int)s->kind);
        break;
    }
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §6  Classical register declaration emitter
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Scans a circuit's body for DeclClassical statements that look like they
 * came from ClassicalRegister(n, "name") lowering, and emits:
 *
 *   creg <name>[<n>];
 *
 * This is a pre-pass so all classical declarations appear at the top of the
 * circuit block before any instructions, matching QASM convention.
 * ─────────────────────────────────────────────────────────────────────────── */
 
static void qk_emit_creg_prepass(FILE *out, MQ_Stmt *body, u32 level)
{
    if (!body) return;
 
    if (body->kind == MQ_Stmt_Block)
    {
        for (u32 i = 0; i < body->block.count; i++)
        {
            MQ_Stmt *s = body->block.stmts[i];
            if (!s || s->kind != MQ_Stmt_DeclClassical) continue;
            MQ_Type *t = s->decl_classical.type;
            if (!t || t->kind != MQ_Type_Int) continue;
 
            /* Heuristic: DeclClassical with type=Int and no init (or init=0)
             * that came from ClassicalRegister lowering.  width encodes the
             * register size (masking out the mutable flag). */
            u32 sz = t->width & ~QK_MUTABLE_FLAG;
            if (sz == 0) sz = 1;
            if (sz > 1024) continue; /* sanity guard */
 
            qk_indent(out, level);
            fprintf(out, "creg %.*s[%u];\n",
                    (int)s->decl_classical.name.size,
                    s->decl_classical.name.str,
                    sz);
        }
    }
    else if (body->kind == MQ_Stmt_DeclClassical)
    {
        MQ_Type *t = body->decl_classical.type;
        if (t && t->kind == MQ_Type_Int)
        {
            u32 sz = t->width & ~QK_MUTABLE_FLAG;
            if (sz == 0) sz = 1;
            if (sz <= 1024)
            {
                qk_indent(out, level);
                fprintf(out, "creg %.*s[%u];\n",
                        (int)body->decl_classical.name.size,
                        body->decl_classical.name.str,
                        sz);
            }
        }
    }
}
 
/* ═══════════════════════════════════════════════════════════════════════════
 * §7  Public entry point: mq_ir_to_code
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   1. File header comment
 *   2. User-defined routines (operation blocks)
 *   3. Circuits (@EntryPoint equivalent)
 * ─────────────────────────────────────────────────────────────────────────── */
 
void mq_ir_to_code(FILE *f, MQ_Program *prog)
{
    if (!f || !prog) return;
 
    /* ── file header ─────────────────────────────────────────────────── */
    fprintf(f, "# Generated by MetaQuantum cross-compiler\n");
    fprintf(f, "# Source language : Qiskit (Python)\n");
    fprintf(f, "# MQ IR version   : %u.%u.%u\n\n",
            prog->mq_ir_version[0],
            prog->mq_ir_version[1],
            prog->mq_ir_version[2]);
    
    /* ── imports ─────────────────────────────────────────────────────── */
    fprintf(f, "from qiskit import QuantumCircuit, QuantumRegister, ClassicalRegister\n");
    fprintf(f, "from qiskit.circuit import Parameter\n");
    fprintf(f, "import math\n\n");
 
/* ── user-defined functions (def blocks from the Qiskit source) ────── */
    for (u32 r = 0; r < prog->routine_count; r++)
    {
        MQ_Routine *rt = prog->routines[r];
        if (!rt || rt->is_intrinsic) continue;
 
        /* Python function signature: def <name>(<params>): */
        fprintf(f, "def %.*s(",
                (int)rt->name.size, rt->name.str);
 
        for (u32 p = 0; p < rt->param_count; p++)
        {
            if (p > 0) fprintf(f, ", ");
            fprintf(f, "%.*s",
                    (int)rt->params[p].name.size,
                    rt->params[p].name.str);
        }
 
        fprintf(f, "):\n");
 
        /* Build qkmap for this routine's scope from its formal parameters. */
        u32          saved_qkmap_count = s_qkmap_count;
        QK_QMapEntry saved_qkmap[QK_QMAP_MAX];
        memcpy(saved_qkmap, s_qkmap, sizeof(QK_QMapEntry) * s_qkmap_count);
        qkmap_reset();
 
        {
            u32 flat_id = 0;
            for (u32 p = 0; p < rt->param_count; p++)
            {
                MQ_FormalParam *fp = &rt->params[p];
                if (!fp->type || fp->type->kind == MQ_Type_Void) continue;
 
                if (fp->type->kind == MQ_Type_QubitReg)
                {
                    u32 sz = fp->type->width & ~QK_MUTABLE_FLAG;
                    if (sz == 0) sz = 1;
                    qkmap_add(flat_id, sz,
                              fp->name.str, (u32)fp->name.size);
                    flat_id += sz;
                }
                else if (fp->type->kind == MQ_Type_Qubit)
                {
                    qkmap_add(flat_id, 0,
                              fp->name.str, (u32)fp->name.size);
                    flat_id++;
                }
                else
                {
                    flat_id++;
                }
            }
        }
 
        qk_emit_stmt(f, rt->body, 1);
 
        /* Restore outer scope qkmap. */
        s_qkmap_count = saved_qkmap_count;
        memcpy(s_qkmap, saved_qkmap, sizeof(QK_QMapEntry) * saved_qkmap_count);
 
        fprintf(f, "\n\n");
    }
    
    /* ── main circuit generation ──────────────────────────────────────── */
    for (u32 c = 0; c < prog->circuit_count; c++)
    {
        MQ_Circuit *circ = prog->circuits[c];
        if (!circ) continue;
 
        /* ── Python function definition for this circuit ────────────── */
        fprintf(f, "def build_%.*s",
                (int)circ->name.size, circ->name.str);
 
        if (circ->param_count > 0)
        {
            fprintf(f, "(");
            for (u32 p = 0; p < circ->param_count; p++)
            {
                if (p > 0) fprintf(f, ", ");
                fprintf(f, "%.*s",
                        (int)circ->param_names[p].size,
                        circ->param_names[p].str);
            }
            fprintf(f, ")");
        }
        else
        {
            fprintf(f, "()");
        }
 
        fprintf(f, ":\n");
        
        /* ── Create QuantumCircuit instance ──────────────────────────– */
        fprintf(f, "    qc = QuantumCircuit(%u)\n", circ->total_qubits);
 
        /* ── save outer qkmap; build circuit scope ────────────────── */
        u32          saved_circ_count = s_qkmap_count;
        QK_QMapEntry saved_circ_map[QK_QMAP_MAX];
        memcpy(saved_circ_map, s_qkmap, sizeof(QK_QMapEntry) * s_qkmap_count);
        qkmap_reset();
 
        /* Seed the qkmap from the circuit's register table so instructions
         * that appear before any DeclQubit stmt can still resolve qubit IDs. */
        for (u32 ri = 0; ri < circ->register_count; ri++)
        {
            MQ_Register *reg = &circ->registers[ri];
            if (reg->kind == MQ_Reg_Classical) continue;
            qkmap_add(reg->base_id, reg->size,
                      reg->name.str, (u32)reg->name.size);
        }
 
        /* Fallback: if the register table is empty but total_qubits > 0,
         * use a generic 'q' mapping. */
        if (circ->register_count == 0 && circ->total_qubits > 0)
        {
            qkmap_add(0u, circ->total_qubits, (const u8 *)"q", 1u);
        }
 
        /* ── emit circuit body ──────────────────────────────────── */
        if (circ->body && circ->body->kind == MQ_Stmt_Block)
        {
            for (u32 si = 0; si < circ->body->block.count; si++)
            {
                MQ_Stmt *s = circ->body->block.stmts[si];
                if (!s) continue;
                /* Skip top-level declarations — they've been handled. */
                if (s->kind == MQ_Stmt_DeclQubit)    continue;
                if (s->kind == MQ_Stmt_DeclClassical) continue;
                qk_emit_stmt(f, s, 1);
            }
        }
        else if (circ->body)
        {
            /* Non-block body (rare). */
            if (circ->body->kind != MQ_Stmt_DeclQubit &&
                circ->body->kind != MQ_Stmt_DeclClassical)
                qk_emit_stmt(f, circ->body, 1);
        }
        
        /* ── return statement ─────────────────────────────────────– */
        fprintf(f, "    return qc\n\n");
 
        /* ── restore outer qkmap ─────────────────────────────────– */
        s_qkmap_count = saved_circ_count;
        memcpy(s_qkmap, saved_circ_map, sizeof(QK_QMapEntry) * saved_circ_count);
    }
    
    /* ── main entry point ─────────────────────────────────────────– */
    if (prog->circuit_count > 0 && prog->circuits[0])
    {
        fprintf(f, "if __name__ == '__main__':\n");
        fprintf(f, "    qc = build_%.*s()\n", 
                (int)prog->circuits[0]->name.size, 
                prog->circuits[0]->name.str);
        // fprintf(f, "    qc.draw()\n");
    }
}