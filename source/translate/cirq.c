#include "cirq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PY_SNIPPET_MAX 64

/* ────────────────────────────────────────────────────────────── */
/* Read entire file into memory                                 */
/* ────────────────────────────────────────────────────────────── */

static char *read_file(const char *path, u32 *out_len)
{
    FILE *f = fopen(path, "rb");

    if (!f) {
        fprintf(stderr, "[python] cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);

    long size = ftell(f);

    rewind(f);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);

    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);

    fclose(f);

    buf[read] = '\0';

    *out_len = (u32)read;

    return buf;
}

/* ────────────────────────────────────────────────────────────── */
/* Copy readable source snippet                                 */
/* ────────────────────────────────────────────────────────────── */

static void copy_snippet(char *dst,
                         size_t dst_size,
                         const char *src,
                         u32 src_len,
                         u32 max_chars)
{
    while (src_len > 0 &&
          (*src == ' '  ||
           *src == '\t' ||
           *src == '\n' ||
           *src == '\r'))
    {
        src++;
        src_len--;
    }

    b8 truncated = (src_len > max_chars);

    u32 copy_len = truncated ? max_chars : src_len;

    if (copy_len >= (u32)dst_size - 4) {
        copy_len = (u32)dst_size - 4;
    }

    memcpy(dst, src, copy_len);

    if (truncated) {

        memcpy(dst + copy_len, "...", 3);

        dst[copy_len + 3] = '\0';

    } else {

        dst[copy_len] = '\0';
    }

    for (char *p = dst; *p; p++) {

        if (*p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
}

/* ────────────────────────────────────────────────────────────── */
/* Recursive tree printer                                       */
/* ────────────────────────────────────────────────────────────── */

static void print_node(TSNode node,
                       const char *source,
                       int depth)
{
    const char *type = ts_node_type(node);

    b8 is_named  = ts_node_is_named(node);
    b8 has_error = ts_node_has_error(node);

    u32 start_byte = ts_node_start_byte(node);
    u32 end_byte   = ts_node_end_byte(node);

    u32 span = end_byte - start_byte;

    TSPoint sp = ts_node_start_point(node);
    TSPoint ep = ts_node_end_point(node);

    /* indentation */

    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    /* node type */

    if (is_named) {

        printf("\033[1;36m%s\033[0m", type);

    } else {

        printf("\033[0;33m\"%s\"\033[0m", type);
    }

    /* source position */

    printf(" \033[0;90m[%u:%u - %u:%u]\033[0m",
           sp.row + 1,
           sp.column,
           ep.row + 1,
           ep.column);

    /* parse error */

    if (has_error) {
        printf(" \033[1;31m<ERROR>\033[0m");
    }

    /* source snippet */

    if (span > 0 && span < 200) {

        char snippet[PY_SNIPPET_MAX + 4];

        copy_snippet(
            snippet,
            sizeof(snippet),
            source + start_byte,
            span,
            PY_SNIPPET_MAX
        );

        printf("  \033[0;32m`%s`\033[0m", snippet);
    }

    printf("\n");

    /* recurse */

    u32 child_count = ts_node_child_count(node);

    for (u32 i = 0; i < child_count; i++) {

        TSNode child = ts_node_child(node, i);

        print_node(child, source, depth + 1);
    }
}

/* ────────────────────────────────────────────────────────────── */
/* Public API                                                    */
/* ────────────────────────────────────────────────────────────── */

Cirq_ParseResult cirq_parse_file(const char *path)
{
    Cirq_ParseResult result;

    memset(&result, 0, sizeof(result));

    result.ok = false;

    /* 1. Read source */

    result.source = read_file(path, &result.source_len);

    if (!result.source) {
        return result;
    }

    /* 2. Create parser */

    result.parser = ts_parser_new();

    if (!result.parser) {

        fprintf(stderr,
                "[python] ts_parser_new() failed\n");

        free(result.source);

        result.source = NULL;

        return result;
    }

    /* 3. Attach Python grammar */

    if (!ts_parser_set_language(
            result.parser,
            tree_sitter_python()))
    {
        fprintf(stderr,
                "[python] ts_parser_set_language() failed\n");

        ts_parser_delete(result.parser);

        free(result.source);

        result.parser = NULL;
        result.source = NULL;

        return result;
    }

    /* 4. Parse */

    result.tree = ts_parser_parse_string(
        result.parser,
        NULL,
        result.source,
        result.source_len
    );

    if (!result.tree) {

        fprintf(stderr,
                "[python] parsing failed\n");

        ts_parser_delete(result.parser);

        free(result.source);

        result.parser = NULL;
        result.source = NULL;

        return result;
    }

    /* 5. Check parse errors */

    TSNode root = ts_tree_root_node(result.tree);

    if (ts_node_has_error(root)) {

        fprintf(stderr,
                "[python] warning: parse errors detected\n");
    }

    result.ok = true;

    return result;
}

void cirq_print_tree(const Cirq_ParseResult *result)
{
    if (!result || !result->ok) {

        fprintf(stderr,
                "[python] py_print_tree: invalid result\n");

        return;
    }

    TSNode root = ts_tree_root_node(result->tree);

    printf("\n=== Python Syntax Tree ===\n\n");

    print_node(root, result->source, 0);

    printf("\n");
}

void cirq_parse_result_free(Cirq_ParseResult *result)
{
    if (!result) {
        return;
    }

    if (result->tree) {

        ts_tree_delete(result->tree);

        result->tree = NULL;
    }

    if (result->parser) {

        ts_parser_delete(result->parser);

        result->parser = NULL;
    }

    if (result->source) {

        free(result->source);

        result->source = NULL;
    }

    result->ok = false;
}

/* ────────────────────────────────────────────────────────────── */
/* Pass 3 – Tree to IR                                            */
/* ────────────────────────────────────────────────────────────── */

#define CQ_MAX_REGS 64

typedef struct {
    M_Arena *arena;
    const char *src;
    MQ_Program *prog;
    MQ_Circuit *circuit;
    
    u32 qubit_counter;
    struct {
        string name;
        u32 base_id;
        u32 size;
    } regs[CQ_MAX_REGS];
    u32 reg_count;
} CQ_Ctx;

static string cq_node_text(M_Arena *arena, TSNode node, const char *src)
{
    u32 start = ts_node_start_byte(node);
    u32 end   = ts_node_end_byte(node);
    u32 len   = end - start;
    if (len == 0) return (string){0};
    u8 *buf = (u8*)arena_alloc(arena, len + 1);
    memcpy(buf, src + start, len);
    buf[len] = '\0';
    return (string){ .str = buf, .size = len };
}

static b8 cq_node_is(TSNode n, const char *type) {
    return strcmp(ts_node_type(n), type) == 0;
}

static b8 cq_node_null(TSNode n) { return ts_node_is_null(n); }

static u32 cq_ctx_resolve_qubit(CQ_Ctx *ctx, string reg_name, u32 index)
{
    for (u32 i = 0; i < ctx->reg_count; i++) {
        if (str_eq(ctx->regs[i].name, reg_name)) {
            return ctx->regs[i].base_id + index;
        }
    }
    return ctx->qubit_counter++;
}

static u32 cq_ctx_add_register(CQ_Ctx *ctx, string name, u32 size)
{
    u32 base = ctx->qubit_counter;
    ctx->qubit_counter += size;
    if (ctx->reg_count < CQ_MAX_REGS) {
        ctx->regs[ctx->reg_count].name    = name;
        ctx->regs[ctx->reg_count].base_id = base;
        ctx->regs[ctx->reg_count].size    = size;
        ctx->reg_count++;
    }
    return base;
}

static MQ_GateType cq_gate_from_name(const char *name)
{
    if (strcmp(name, "H") == 0) return MQ_Gate_H;
    if (strcmp(name, "X") == 0) return MQ_Gate_X;
    if (strcmp(name, "Y") == 0) return MQ_Gate_Y;
    if (strcmp(name, "Z") == 0) return MQ_Gate_Z;
    if (strcmp(name, "S") == 0) return MQ_Gate_S;
    if (strcmp(name, "T") == 0) return MQ_Gate_T;
    if (strcmp(name, "CNOT") == 0) return MQ_Gate_X; // controlled
    if (strcmp(name, "CX") == 0) return MQ_Gate_X;
    if (strcmp(name, "CZ") == 0) return MQ_Gate_Z;
    if (strcmp(name, "CCZ") == 0) return MQ_Gate_Z;
    if (strcmp(name, "CCX") == 0) return MQ_Gate_CCX;
    if (strcmp(name, "SWAP") == 0) return MQ_Gate_SWAP;
    if (strcmp(name, "rx") == 0 || strcmp(name, "Rx") == 0) return MQ_Gate_RX;
    if (strcmp(name, "ry") == 0 || strcmp(name, "Ry") == 0) return MQ_Gate_RY;
    if (strcmp(name, "rz") == 0 || strcmp(name, "Rz") == 0) return MQ_Gate_RZ;
    return MQ_Gate_Custom;
}

static MQ_Expr *cq_lower_expr(CQ_Ctx *ctx, TSNode node)
{
    if (cq_node_null(node)) return NULL;
    const char *type = ts_node_type(node);

    if (strcmp(type, "integer") == 0) {
        string txt = cq_node_text(ctx->arena, node, ctx->src);
        i64 val = txt.str ? (i64)strtoll((char*)txt.str, NULL, 10) : 0;
        return mq_expr_int(ctx->arena, val);
    }
    if (strcmp(type, "float") == 0) {
        string txt = cq_node_text(ctx->arena, node, ctx->src);
        f64 val = txt.str ? strtod((char*)txt.str, NULL) : 0.0;
        return mq_expr_float(ctx->arena, val);
    }
    if (strcmp(type, "identifier") == 0) {
        return mq_expr_var(ctx->arena, cq_node_text(ctx->arena, node, ctx->src));
    }
    if (strcmp(type, "binary_operator") == 0) {
        TSNode lhs_n = ts_node_child(node, 0);
        TSNode op_n  = ts_node_child(node, 1);
        TSNode rhs_n = ts_node_child(node, 2);
        MQ_Expr *l = cq_lower_expr(ctx, lhs_n);
        MQ_Expr *r = cq_lower_expr(ctx, rhs_n);
        string op = cq_node_text(ctx->arena, op_n, ctx->src);
        MQ_BinOp bop = MQ_BinOp_Add;
        if (op.str) {
            const char *s = (char*)op.str;
            if (strcmp(s, "+") == 0) bop = MQ_BinOp_Add;
            else if (strcmp(s, "-") == 0) bop = MQ_BinOp_Sub;
            else if (strcmp(s, "*") == 0) bop = MQ_BinOp_Mul;
            else if (strcmp(s, "/") == 0) bop = MQ_BinOp_Div;
            else if (strcmp(s, "**") == 0) bop = MQ_BinOp_Pow;
        }
        return mq_expr_binop(ctx->arena, bop, l, r);
    }
    return mq_expr_var(ctx->arena, cq_node_text(ctx->arena, node, ctx->src));
}

static u32 cq_resolve_qubit(CQ_Ctx *ctx, TSNode node)
{
    if (cq_node_is(node, "identifier")) {
        string name = cq_node_text(ctx->arena, node, ctx->src);
        return cq_ctx_resolve_qubit(ctx, name, 0);
    }
    if (cq_node_is(node, "subscript")) {
        TSNode value_n = ts_node_child_by_field_name(node, "value", 5);
        TSNode sub_n = ts_node_child_by_field_name(node, "subscript", 9);
        
        string name = cq_node_text(ctx->arena, value_n, ctx->src);
        string idx_str = cq_node_text(ctx->arena, sub_n, ctx->src);
        u32 idx = idx_str.str ? (u32)strtoul((char*)idx_str.str, NULL, 10) : 0;
        
        return cq_ctx_resolve_qubit(ctx, name, idx);
    }
    return 0;
}

static MQ_Instruction cq_lower_gate_call(CQ_Ctx *ctx, TSNode gate_call, TSNode args_node, string gate_name, MQ_Expr *power_expr)
{
    MQ_Instruction zero; memset(&zero,0,sizeof(zero));
    
    char name_buf[64] = {0};
    u32 n = (gate_name.size < sizeof(name_buf)-1) ? (u32)gate_name.size : (u32)sizeof(name_buf)-1;
    if (gate_name.str) { memcpy(name_buf, gate_name.str, n); name_buf[n] = '\0'; }
    
    if (strcmp(name_buf, "measure") == 0) {
        u32 q = 0;
        u32 argc = ts_node_named_child_count(args_node);
        if (argc > 0) {
            TSNode first_arg = ts_node_named_child(args_node, 0);
            if (!cq_node_is(first_arg, "keyword_argument")) {
                q = cq_resolve_qubit(ctx, first_arg);
            }
        }
        return mq_instr_measure_discard(q);
    }

    MQ_GateType type = cq_gate_from_name(name_buf);
    
    u32 qubits[MQ_MAX_GATE_QUBITS];
    u8 qcnt = 0;
    
    u32 argc = ts_node_named_child_count(args_node);
    for (u32 i = 0; i < argc && qcnt < MQ_MAX_GATE_QUBITS; i++) {
        TSNode arg = ts_node_named_child(args_node, i);
        if (!cq_node_is(arg, "keyword_argument")) {
            qubits[qcnt++] = cq_resolve_qubit(ctx, arg);
        }
    }

    if (type == MQ_Gate_Custom) {
        return mq_instr_gate_custom(gate_name, qubits, qcnt, power_expr ? &power_expr : NULL, power_expr ? 1 : 0);
    }
    
    if (type == MQ_Gate_X || type == MQ_Gate_Z || type == MQ_Gate_CCX) {
        if (strcmp(name_buf, "CNOT") == 0 || strcmp(name_buf, "CX") == 0 || strcmp(name_buf, "CZ") == 0) {
            if (qcnt >= 2) {
                if (power_expr) {
                    MQ_Expr *pi_expr = mq_expr_var(ctx->arena, str_lit("PI"));
                    MQ_Expr *param = mq_expr_binop(ctx->arena, MQ_BinOp_Mul, power_expr, pi_expr);
                    MQ_Instruction instr = mq_instr_gate_sym(MQ_Gate_P, &qubits[1], 1, &param, 1);
                    mq_instr_add_control(&instr, qubits[0], 1);
                    return instr;
                }
                MQ_Instruction instr = mq_instr_gate(type, &qubits[1], 1);
                mq_instr_add_control(&instr, qubits[0], 1);
                return instr;
            }
        }
        if (strcmp(name_buf, "CCZ") == 0) {
            if (qcnt >= 3) {
                MQ_Instruction instr = mq_instr_gate(MQ_Gate_Z, &qubits[2], 1);
                mq_instr_add_control(&instr, qubits[0], 1);
                mq_instr_add_control(&instr, qubits[1], 1);
                return instr;
            }
        }
    }
    
    if (power_expr) {
        return mq_instr_gate_sym(type, qubits, qcnt, &power_expr, 1);
    }
    
    return mq_instr_gate(type, qubits, qcnt);
}

static MQ_Stmt *cq_lower_stmt(CQ_Ctx *ctx, TSNode node)
{
    const char *type = ts_node_type(node);
    TSNode expr = node;

    if (strcmp(type, "expression_statement") == 0) {
        expr = ts_node_named_child(node, 0);
    }

    if (cq_node_is(expr, "call")) {
        TSNode fn_node = ts_node_child_by_field_name(expr, "function", 8);
        if (cq_node_is(fn_node, "attribute")) {
            TSNode attr_name = ts_node_child_by_field_name(fn_node, "attribute", 9);
            string attr_str = cq_node_text(ctx->arena, attr_name, ctx->src);
            if (attr_str.str && strcmp((char*)attr_str.str, "append") == 0) {
                TSNode args_n = ts_node_child_by_field_name(expr, "arguments", 9);
                if (ts_node_named_child_count(args_n) > 0) {
                    TSNode gate_call = ts_node_named_child(args_n, 0);
                    
                    MQ_Expr *power_expr = NULL;
                    if (cq_node_is(gate_call, "binary_operator")) {
                        TSNode op_n = ts_node_child(gate_call, 1);
                        string op_str = cq_node_text(ctx->arena, op_n, ctx->src);
                        if (op_str.str && strcmp((char*)op_str.str, "**") == 0) {
                            power_expr = cq_lower_expr(ctx, ts_node_child(gate_call, 2));
                            gate_call = ts_node_child(gate_call, 0);
                        }
                    }
                    
                    if (cq_node_is(gate_call, "call")) {
                        TSNode gfn = ts_node_child_by_field_name(gate_call, "function", 8);
                        TSNode gargs = ts_node_child_by_field_name(gate_call, "arguments", 9);
                        
                        string gate_name = {0};
                        if (cq_node_is(gfn, "attribute")) {
                            gate_name = cq_node_text(ctx->arena, ts_node_child_by_field_name(gfn, "attribute", 9), ctx->src);
                        } else {
                            gate_name = cq_node_text(ctx->arena, gfn, ctx->src);
                        }
                        
                        MQ_Instruction instr = cq_lower_gate_call(ctx, gate_call, gargs, gate_name, power_expr);
                        return mq_stmt_instr(ctx->arena, instr);
                    }
                }
            }
        }
        return NULL;
    }
    
    if (strcmp(type, "assignment") == 0) {
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        TSNode right = ts_node_child_by_field_name(node, "right", 5);
        
        if (cq_node_is(right, "call")) {
            TSNode fn_node = ts_node_child_by_field_name(right, "function", 8);
            if (cq_node_is(fn_node, "attribute")) {
                TSNode attr_name = ts_node_child_by_field_name(fn_node, "attribute", 9);
                string attr_str = cq_node_text(ctx->arena, attr_name, ctx->src);
                
                if (attr_str.str && strcmp((char*)attr_str.str, "range") == 0) {
                    TSNode args_n = ts_node_child_by_field_name(right, "arguments", 9);
                    if (ts_node_named_child_count(args_n) > 0) {
                        TSNode num_node = ts_node_named_child(args_n, 0);
                        string num_str = cq_node_text(ctx->arena, num_node, ctx->src);
                        u32 count = num_str.str ? (u32)strtoul((char*)num_str.str, NULL, 10) : 1;
                        
                        if (cq_node_is(left, "pattern_list")) {
                            u32 c = ts_node_named_child_count(left);
                            for (u32 i = 0; i < c; i++) {
                                TSNode ident = ts_node_named_child(left, i);
                                string qname = cq_node_text(ctx->arena, ident, ctx->src);
                                cq_ctx_add_register(ctx, qname, 1);
                            }
                        } else {
                            string qname = cq_node_text(ctx->arena, left, ctx->src);
                            cq_ctx_add_register(ctx, qname, count);
                        }
                    }
                }
                
                if (attr_str.str && strcmp((char*)attr_str.str, "Circuit") == 0) {
                    if (cq_node_is(left, "identifier")) {
                        string circ_name = cq_node_text(ctx->arena, left, ctx->src);
                        ctx->circuit->name = circ_name;
                    }
                }
            }
        }
        return NULL;
    }
    
    return NULL;
}

static void cq_lower_module(CQ_Ctx *ctx, TSNode module_node)
{
    u32 count = ts_node_named_child_count(module_node);
    
    // Check if the module contains a function definition (like build_circuit)
    TSNode target_node = module_node;
    u32 target_count = count;
    
    for (u32 i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(module_node, i);
        if (strcmp(ts_node_type(child), "function_definition") == 0) {
            TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (cq_node_is(body, "block")) {
                target_node = body;
                target_count = ts_node_named_child_count(body);
                break;
            }
        }
    }
    
    MQ_Stmt **stmts = (MQ_Stmt**)arena_alloc(ctx->arena, sizeof(MQ_Stmt*) * target_count);
    u32 scount = 0;
    
    for (u32 i = 0; i < target_count; i++) {
        TSNode child = ts_node_named_child(target_node, i);
        MQ_Stmt *s = cq_lower_stmt(ctx, child);
        if (s) {
            stmts[scount++] = s;
        }
    }
    
    ctx->circuit->body = mq_stmt_block(ctx->arena, stmts, scount);
}

MQ_Program *cirq_tree_to_ir(const Cirq_ParseResult *result, M_Arena *arena)
{
    if (!result || !result->ok || !result->tree) return NULL;
    
    CQ_Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.arena = arena;
    ctx.src = result->source;
    ctx.prog = mq_program(arena, str_lit("cirq_prog"), MQ_Lang_Cirq);
    
    ctx.circuit = arena_alloc_zero(arena, sizeof(MQ_Circuit));
    ctx.circuit->name = str_lit("main");
    
    TSNode root = ts_tree_root_node(result->tree);
    cq_lower_module(&ctx, root);
    
    MQ_Register *regs = arena_alloc_array(arena, MQ_Register, ctx.reg_count);
    for (u32 i = 0; i < ctx.reg_count; i++) {
        regs[i] = mq_register_quantum(ctx.regs[i].name, ctx.regs[i].size, ctx.regs[i].base_id, NULL);
    }
    
    MQ_Circuit *real_circ = mq_circuit(arena, ctx.circuit->name, regs, ctx.reg_count, ctx.qubit_counter, 0);
    real_circ->body = ctx.circuit->body;
    
    mq_program_add_circuit(arena, ctx.prog, real_circ);
    
    return ctx.prog;
}

static void cq_indent(FILE *out, u32 level)
{
    for (u32 i = 0; i < level; i++)
        fprintf(out, "    ");
}

static const char *cq_gate_name(MQ_GateType g)
{
    switch (g)
    {
    case MQ_Gate_I:    return "I";
    case MQ_Gate_H:    return "H";
    case MQ_Gate_X:    return "X";
    case MQ_Gate_Y:    return "Y";
    case MQ_Gate_Z:    return "Z";
    case MQ_Gate_S:    return "S";
    case MQ_Gate_T:    return "T";
    case MQ_Gate_P:    return "ZPowGate";
    case MQ_Gate_RX:   return "rx";
    case MQ_Gate_RY:   return "ry";
    case MQ_Gate_RZ:   return "rz";
    case MQ_Gate_SWAP: return "SWAP";
    case MQ_Gate_ISWAP:return "ISWAP";
    case MQ_Gate_CCX:  return "CCX";
    case MQ_Gate_CSWAP:return "CSWAP";
    default:           return "Gate";
    }
}

static void cq_print_qubit(FILE *out, MQ_Circuit *circ, u32 id)
{
    for (u32 ri = 0; ri < circ->register_count; ri++) {
        MQ_Register *reg = &circ->registers[ri];
        if (reg->kind == MQ_Reg_Classical) continue;
        if (id >= reg->base_id && id < reg->base_id + reg->size) {
            if (reg->size == 1) {
                fprintf(out, "%.*s", (int)reg->name.size, reg->name.str);
            } else {
                fprintf(out, "%.*s[%u]", (int)reg->name.size, reg->name.str, id - reg->base_id);
            }
            return;
        }
    }
    fprintf(out, "q[%u]", id);
}

static void cq_emit_expr(FILE *out, MQ_Expr *e)
{
    if (!e) return;
    switch (e->kind)
    {
    case MQ_Expr_FloatLit: fprintf(out, "%g", e->lit.float_val); break;
    case MQ_Expr_IntLit:   fprintf(out, "%lld", (long long)e->lit.int_val); break;
    case MQ_Expr_Var:
    case MQ_Expr_Symbol:   fprintf(out, "%.*s", (int)e->name.size, e->name.str); break;
    case MQ_Expr_BinOp:
        fprintf(out, "(");
        cq_emit_expr(out, e->bin.lhs);
        switch (e->bin.op) {
            case MQ_BinOp_Add: fprintf(out, " + "); break;
            case MQ_BinOp_Sub: fprintf(out, " - "); break;
            case MQ_BinOp_Mul: fprintf(out, " * "); break;
            case MQ_BinOp_Div: fprintf(out, " / "); break;
            case MQ_BinOp_Pow: fprintf(out, " ** "); break;
            case MQ_BinOp_Mod: fprintf(out, " %% "); break;
            default:           fprintf(out, " ? "); break;
        }
        cq_emit_expr(out, e->bin.rhs);
        fprintf(out, ")");
        break;
    default: fprintf(out, "expr"); break;
    }
}

static void cq_emit_instr(FILE *out, MQ_Instruction *in, MQ_Circuit *circ, u32 level)
{
    cq_indent(out, level);
    
    if (in->type == MQ_Instr_Gate) {
        fprintf(out, "circuit.append(");
        
        const char *gname = cq_gate_name(in->gate.gate);
        if (in->gate.gate == MQ_Gate_Custom) {
            fprintf(out, "%.*s", (int)in->gate.custom_name.size, in->gate.custom_name.str);
        } else {
            fprintf(out, "cirq.%s", gname);
        }
        
        if (in->gate.param_count > 0) {
            fprintf(out, "(");
            for (u8 pi = 0; pi < in->gate.param_count; pi++) {
                if (pi > 0) fprintf(out, ", ");
                if (in->gate.params_symbolic) {
                    cq_emit_expr(out, in->gate.param_exprs[pi]);
                } else {
                    fprintf(out, "%g", in->gate.params[pi]);
                }
            }
            fprintf(out, ")");
        }
        
        fprintf(out, "(");
        for (u8 q = 0; q < in->qubit_count; q++) {
            if (q > 0) fprintf(out, ", ");
            cq_print_qubit(out, circ, in->qubits[q]);
        }
        fprintf(out, ")");
        
        u8 ctrl_count = in->gate.control_count;
        if (ctrl_count > 0) {
            fprintf(out, ".controlled_by(");
            for (u8 c = 0; c < ctrl_count; c++) {
                if (c > 0) fprintf(out, ", ");
                cq_print_qubit(out, circ, in->gate.controls[c]);
            }
            fprintf(out, ")");
        }
        
        fprintf(out, ")\n");
    } else if (in->type == MQ_Instr_Measure) {
        fprintf(out, "circuit.append(cirq.measure(");
        cq_print_qubit(out, circ, in->qubits[0]);
        if (in->measure.has_target) {
            fprintf(out, ", key='c%u'", in->measure.clbit);
        }
        fprintf(out, "))\n");
    }
}

static void cq_emit_stmt(FILE *out, MQ_Stmt *s, MQ_Circuit *circ, u32 level)
{
    if (!s) return;
    switch (s->kind) {
    case MQ_Stmt_Block:
        for (u32 i = 0; i < s->block.count; i++) {
            cq_emit_stmt(out, s->block.stmts[i], circ, level);
        }
        break;
    case MQ_Stmt_Instr:
        cq_emit_instr(out, &s->instr, circ, level);
        break;
    default:
        break;
    }
}

void cirq_emit(FILE *out, MQ_Program *prog)
{
    if (!out || !prog) return;

    fprintf(out, "# Generated by MetaQuantum cross-compiler\n");
    fprintf(out, "# Source language : Cirq (Python)\n\n");
    fprintf(out, "import cirq\n");
    fprintf(out, "import math\n\n");
    
    for (u32 c = 0; c < prog->circuit_count; c++) {
        MQ_Circuit *circ = prog->circuits[c];
        if (!circ) continue;
        
        fprintf(out, "def build_%.*s():\n", (int)circ->name.size, circ->name.str);
        
        for (u32 ri = 0; ri < circ->register_count; ri++) {
            MQ_Register *reg = &circ->registers[ri];
            if (reg->kind == MQ_Reg_Classical) continue;
            
            cq_indent(out, 1);
            if (reg->size == 1) {
                fprintf(out, "%.*s = cirq.NamedQubit('%.*s')\n", 
                    (int)reg->name.size, reg->name.str,
                    (int)reg->name.size, reg->name.str);
            } else {
                fprintf(out, "%.*s = [cirq.NamedQubit(f'%.*s_{i}') for i in range(%u)]\n",
                    (int)reg->name.size, reg->name.str,
                    (int)reg->name.size, reg->name.str,
                    reg->size);
            }
        }
        
        if (circ->register_count == 0 && circ->total_qubits > 0) {
            cq_indent(out, 1);
            fprintf(out, "q = [cirq.NamedQubit(f'q_{i}') for i in range(%u)]\n", circ->total_qubits);
        }
        
        cq_indent(out, 1);
        fprintf(out, "circuit = cirq.Circuit()\n");
        
        cq_emit_stmt(out, circ->body, circ, 1);
        
        cq_indent(out, 1);
        fprintf(out, "return circuit\n\n");
    }
}