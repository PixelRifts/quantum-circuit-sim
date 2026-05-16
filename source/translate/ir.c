#include "ir.h"

#include "ir.h"
#include <string.h>

// ============================================================
// Internal helpers
// ============================================================

static MQ_Expr *expr_alloc(M_Arena *arena, MQ_ExprKind kind) {
    MQ_Expr *e = arena_alloc_zero(arena, sizeof(MQ_Expr));
    e->kind = kind;
    return e;
}

static MQ_Stmt *stmt_alloc(M_Arena *arena, MQ_StmtKind kind) {
    MQ_Stmt *s = arena_alloc_zero(arena, sizeof(MQ_Stmt));
    s->kind = kind;
    return s;
}

static MQ_Expr **copy_expr_array(M_Arena *arena, MQ_Expr **src, u32 count) {
    if (!src || count == 0) return 0;
    MQ_Expr **dst = arena_alloc_array(arena, MQ_Expr *, count);
    memcpy(dst, src, sizeof(MQ_Expr *) * count);
    return dst;
}

// ============================================================
// Types
// ============================================================

MQ_Type *mq_type_scalar(M_Arena *arena, MQ_TypeKind kind) {
    MQ_Type *t  = arena_alloc_zero(arena, sizeof(MQ_Type));
    t->kind     = kind;
    return t;
}

MQ_Type *mq_type_int(M_Arena *arena, u32 width) {
    MQ_Type *t  = arena_alloc_zero(arena, sizeof(MQ_Type));
    t->kind     = MQ_Type_Int;
    t->width    = width;
    return t;
}

MQ_Type *mq_type_array(M_Arena *arena, MQ_Type *element_type, u32 count) {
    MQ_Type *t      = arena_alloc_zero(arena, sizeof(MQ_Type));
    t->kind         = MQ_Type_Array;
    t->width        = count;
    t->element_type = element_type;
    return t;
}

MQ_Type *mq_type_reg(M_Arena *arena, MQ_TypeKind kind, u32 width) {
    MQ_Type *t  = arena_alloc_zero(arena, sizeof(MQ_Type));
    t->kind     = kind;
    t->width    = width;
    return t;
}

// ============================================================
// Expressions
// ============================================================

MQ_Expr *mq_expr_bool(M_Arena *arena, b8 val) {
    MQ_Expr *e      = expr_alloc(arena, MQ_Expr_BoolLit);
    e->lit.bool_val = val;
    return e;
}

MQ_Expr *mq_expr_int(M_Arena *arena, i64 val) {
    MQ_Expr *e     = expr_alloc(arena, MQ_Expr_IntLit);
    e->lit.int_val = val;
    return e;
}

MQ_Expr *mq_expr_float(M_Arena *arena, f64 val) {
    MQ_Expr *e       = expr_alloc(arena, MQ_Expr_FloatLit);
    e->lit.float_val = val;
    return e;
}

MQ_Expr *mq_expr_symbol(M_Arena *arena, string name) {
    MQ_Expr *e = expr_alloc(arena, MQ_Expr_Symbol);
    e->name    = name;
    return e;
}

MQ_Expr *mq_expr_var(M_Arena *arena, string name) {
    MQ_Expr *e = expr_alloc(arena, MQ_Expr_Var);
    e->name    = name;
    return e;
}

MQ_Expr *mq_expr_qubit_ref(M_Arena *arena, u32 qubit_id) {
    MQ_Expr *e   = expr_alloc(arena, MQ_Expr_QubitRef);
    e->qubit_id  = qubit_id;
    return e;
}

MQ_Expr *mq_expr_reg_index(M_Arena *arena, string reg_name, MQ_Expr *index_expr) {
    MQ_Expr *e       = expr_alloc(arena, MQ_Expr_RegIndex);
    e->reg.name      = reg_name;
    e->reg.index_expr = index_expr;
    return e;
}

MQ_Expr *mq_expr_binop(M_Arena *arena, MQ_BinOp op, MQ_Expr *lhs, MQ_Expr *rhs) {
    MQ_Expr *e  = expr_alloc(arena, MQ_Expr_BinOp);
    e->bin.op   = op;
    e->bin.lhs  = lhs;
    e->bin.rhs  = rhs;
    return e;
}

MQ_Expr *mq_expr_unop(M_Arena *arena, MQ_UnOp op, MQ_Expr *operand) {
    MQ_Expr *e     = expr_alloc(arena, MQ_Expr_UnOp);
    e->un.op       = op;
    e->un.operand  = operand;
    return e;
}

MQ_Expr *mq_expr_call(M_Arena *arena, string name, MQ_Expr **args, u32 arg_count) {
    MQ_Expr *e       = expr_alloc(arena, MQ_Expr_Call);
    e->call.name      = name;
    e->call.args      = copy_expr_array(arena, args, arg_count);
    e->call.arg_count = arg_count;
    return e;
}

MQ_Expr *mq_expr_array(M_Arena *arena, MQ_Expr **elems, u32 count) {
    MQ_Expr *e       = expr_alloc(arena, MQ_Expr_Array);
    e->call.name      = (string){0};
    e->call.args      = copy_expr_array(arena, elems, count);
    e->call.arg_count = count;
    return e;
}

MQ_Expr *mq_expr_bit_read(M_Arena *arena, string reg_name, u32 index) {
    MQ_Expr *e      = expr_alloc(arena, MQ_Expr_BitRead);
    e->bit.reg_name = reg_name;
    e->bit.index    = index;
    return e;
}

// ============================================================
// Instructions
// ============================================================

static void copy_qubits(MQ_Instruction *instr, u32 *qubits, u8 qubit_count) {
    instr->qubit_count = qubit_count;
    for (u8 i = 0; i < qubit_count; i++)
        instr->qubits[i] = qubits[i];
}

MQ_Instruction mq_instr_gate(MQ_GateType gate, u32 *qubits, u8 qubit_count) {
    MQ_Instruction instr = {0};
    instr.type      = MQ_Instr_Gate;
    instr.gate.gate = gate;
    copy_qubits(&instr, qubits, qubit_count);
    return instr;
}

MQ_Instruction mq_instr_gate_p(MQ_GateType gate, u32 *qubits, u8 qubit_count,
                               f64 *params, u8 param_count) {
    MQ_Instruction instr      = mq_instr_gate(gate, qubits, qubit_count);
    instr.gate.param_count    = param_count;
    instr.gate.params_symbolic = 0;
    for (u8 i = 0; i < param_count; i++)
        instr.gate.params[i] = params[i];
    return instr;
}

MQ_Instruction mq_instr_gate_sym(MQ_GateType gate, u32 *qubits, u8 qubit_count,
                                 MQ_Expr **param_exprs, u8 param_count) {
    MQ_Instruction instr      = mq_instr_gate(gate, qubits, qubit_count);
    instr.gate.param_count    = param_count;
    instr.gate.params_symbolic = 1;
    for (u8 i = 0; i < param_count; i++)
        instr.gate.param_exprs[i] = param_exprs[i];
    return instr;
}

MQ_Instruction mq_instr_gate_custom(string name, u32 *qubits, u8 qubit_count,
                                    MQ_Expr **param_exprs, u8 param_count) {
    MQ_Instruction instr       = mq_instr_gate(MQ_Gate_Custom, qubits, qubit_count);
    instr.gate.custom_name     = name;
    instr.gate.param_count     = param_count;
    instr.gate.params_symbolic = (param_count > 0);
    for (u8 i = 0; i < param_count; i++)
        instr.gate.param_exprs[i] = param_exprs[i];
    return instr;
}

MQ_Instruction mq_instr_measure(u32 qubit, u32 clbit) {
    MQ_Instruction instr   = {0};
    instr.type             = MQ_Instr_Measure;
    instr.qubits[0]        = qubit;
    instr.qubit_count      = 1;
    instr.measure.clbit    = clbit;
    instr.measure.has_target = 1;
    return instr;
}

MQ_Instruction mq_instr_measure_discard(u32 qubit) {
    MQ_Instruction instr     = {0};
    instr.type               = MQ_Instr_Measure;
    instr.qubits[0]          = qubit;
    instr.qubit_count        = 1;
    instr.measure.has_target = 0;
    return instr;
}

MQ_Instruction mq_instr_reset(u32 qubit) {
    MQ_Instruction instr = {0};
    instr.type           = MQ_Instr_Reset;
    instr.qubits[0]      = qubit;
    instr.qubit_count    = 1;
    return instr;
}

MQ_Instruction mq_instr_barrier(u32 *qubits, u8 qubit_count) {
    MQ_Instruction instr = {0};
    instr.type           = MQ_Instr_Barrier;
    copy_qubits(&instr, qubits, qubit_count);
    return instr;
}

MQ_Instruction mq_instr_delay(u32 *qubits, u8 qubit_count, f64 duration, MQ_TimeUnit unit) {
    MQ_Instruction instr    = {0};
    instr.type              = MQ_Instr_Delay;
    instr.delay.duration    = duration;
    instr.delay.unit        = unit;
    copy_qubits(&instr, qubits, qubit_count);
    return instr;
}

void mq_instr_add_control(MQ_Instruction *instr, u32 qubit, u8 control_state) {
    u8 i = instr->gate.control_count++;
    instr->gate.controls[i]       = qubit;
    instr->gate.control_states[i] = control_state;
}

void mq_instr_set_classical_cond(MQ_Instruction *instr, u32 classical_bit, u32 classical_val) {
    instr->gate.has_classical_cond = 1;
    instr->gate.classical_bit      = classical_bit;
    instr->gate.classical_val      = classical_val;
}

// ============================================================
// Statements
// ============================================================

MQ_Stmt *mq_stmt_block(M_Arena *arena, MQ_Stmt **stmts, u32 count) {
    MQ_Stmt *s    = stmt_alloc(arena, MQ_Stmt_Block);
    s->block.count = count;
    if (count > 0) {
        s->block.stmts = arena_alloc_array(arena, MQ_Stmt *, count);
        memcpy(s->block.stmts, stmts, sizeof(MQ_Stmt *) * count);
    }
    return s;
}

MQ_Stmt *mq_stmt_instr(M_Arena *arena, MQ_Instruction instr) {
    MQ_Stmt *s = stmt_alloc(arena, MQ_Stmt_Instr);
    s->instr   = instr;
    return s;
}

MQ_Stmt *mq_stmt_adjoint(M_Arena *arena, MQ_Stmt *body) {
    MQ_Stmt *s       = stmt_alloc(arena, MQ_Stmt_Adjoint);
    s->adjoint_body  = body;
    return s;
}

MQ_Stmt *mq_stmt_decl_qubit(M_Arena *arena, string name, u32 count, u32 base_id) {
    MQ_Stmt *s            = stmt_alloc(arena, MQ_Stmt_DeclQubit);
    s->decl_qubit.name    = name;
    s->decl_qubit.count   = count;
    s->decl_qubit.base_id = base_id;
    return s;
}

MQ_Stmt *mq_stmt_decl_classical(M_Arena *arena, string name, MQ_Type *type, MQ_Expr *init) {
    MQ_Stmt *s               = stmt_alloc(arena, MQ_Stmt_DeclClassical);
    s->decl_classical.name   = name;
    s->decl_classical.type   = type;
    s->decl_classical.init   = init;
    return s;
}

MQ_Stmt *mq_stmt_decl_param(M_Arena *arena, string name, MQ_Expr *default_val) {
    MQ_Stmt *s                   = stmt_alloc(arena, MQ_Stmt_DeclParam);
    s->decl_param.name           = name;
    s->decl_param.default_val    = default_val;
    return s;
}

MQ_Stmt *mq_stmt_set(M_Arena *arena, MQ_Expr *lhs, MQ_Expr *rhs) {
    MQ_Stmt *s      = stmt_alloc(arena, MQ_Stmt_Set);
    s->set.lhs      = lhs;
    s->set.rhs      = rhs;
    s->set.augmented = 0;
    return s;
}

MQ_Stmt *mq_stmt_set_aug(M_Arena *arena, MQ_Expr *lhs, MQ_BinOp op, MQ_Expr *rhs) {
    MQ_Stmt *s      = stmt_alloc(arena, MQ_Stmt_Set);
    s->set.lhs      = lhs;
    s->set.rhs      = rhs;
    s->set.aug_op   = op;
    s->set.augmented = 1;
    return s;
}

MQ_Stmt *mq_stmt_if(M_Arena *arena, MQ_IfBranch *branches, u32 count) {
    MQ_Stmt *s         = stmt_alloc(arena, MQ_Stmt_If);
    s->if_stmt.count   = count;
    s->if_stmt.branches = arena_alloc_array(arena, MQ_IfBranch, count);
    memcpy(s->if_stmt.branches, branches, sizeof(MQ_IfBranch) * count);
    return s;
}

MQ_Stmt *mq_stmt_for(M_Arena *arena, string var_name, MQ_Type *var_type,
                     MQ_Expr *iterable, MQ_Stmt *body) {
    MQ_Stmt *s             = stmt_alloc(arena, MQ_Stmt_For);
    s->for_loop.var_name   = var_name;
    s->for_loop.var_type   = var_type;
    s->for_loop.iterable   = iterable;
    s->for_loop.body       = body;
    return s;
}

MQ_Stmt *mq_stmt_while(M_Arena *arena, MQ_Expr *cond, MQ_Stmt *body) {
    MQ_Stmt *s          = stmt_alloc(arena, MQ_Stmt_While);
    s->while_loop.cond  = cond;
    s->while_loop.body  = body;
    return s;
}

MQ_Stmt *mq_stmt_break(M_Arena *arena) {
    return stmt_alloc(arena, MQ_Stmt_Break);
}

MQ_Stmt *mq_stmt_continue(M_Arena *arena) {
    return stmt_alloc(arena, MQ_Stmt_Continue);
}

MQ_Stmt *mq_stmt_return(M_Arena *arena, MQ_Expr *val) {
    MQ_Stmt *s    = stmt_alloc(arena, MQ_Stmt_Return);
    s->return_val = val;
    return s;
}

MQ_Stmt *mq_stmt_call(M_Arena *arena, string callee,
                      MQ_Expr **args, u32 arg_count) {
    MQ_Stmt *s         = stmt_alloc(arena, MQ_Stmt_Call);
    s->call.callee     = callee;
    s->call.args       = copy_expr_array(arena, args, arg_count);
    s->call.arg_count  = arg_count;
    return s;
}

MQ_Stmt *mq_stmt_comment(M_Arena *arena, string text) {
    MQ_Stmt *s      = stmt_alloc(arena, MQ_Stmt_Comment);
    s->comment_text = text;
    return s;
}

MQ_Stmt *mq_stmt_pragma(M_Arena *arena, string key, string value) {
    MQ_Stmt *s    = stmt_alloc(arena, MQ_Stmt_Pragma);
    s->pragma.key   = key;
    s->pragma.value = value;
    return s;
}

MQ_IfBranch mq_if_branch(MQ_Expr *cond, MQ_Stmt *body) {
    return (MQ_IfBranch){ .cond = cond, .body = body };
}

MQ_IfBranch mq_else_branch(MQ_Stmt *body) {
    return (MQ_IfBranch){ .cond = 0, .body = body };
}

// ============================================================
// Registers
// ============================================================

MQ_Register mq_register_quantum(string name, u32 size, u32 base_id,
                                MQ_QubitMeta *meta) {
    MQ_Register r  = {0};
    r.kind         = MQ_Reg_Quantum;
    r.name         = name;
    r.size         = size;
    r.base_id      = base_id;
    r.qubit_meta   = meta;
    return r;
}

MQ_Register mq_register_classical(string name, u32 size, u32 base_id) {
    MQ_Register r  = {0};
    r.kind         = MQ_Reg_Classical;
    r.name         = name;
    r.size         = size;
    r.base_id      = base_id;
    return r;
}

// ============================================================
// Routines
// ============================================================

MQ_FormalParam mq_formal_param(string name, MQ_Type *type, MQ_Expr *default_val) {
    return (MQ_FormalParam){ .name = name, .type = type, .default_val = default_val };
}

MQ_Routine *mq_routine(M_Arena *arena, string name, MQ_RoutineKind kind,
                       MQ_FormalParam *params, u32 param_count,
                       MQ_Type *return_type, MQ_Stmt *body) {
    MQ_Routine *r  = arena_alloc_zero(arena, sizeof(MQ_Routine));
    r->name        = name;
    r->kind        = kind;
    r->param_count = param_count;
    r->return_type = return_type;
    r->body        = body;
    if (param_count > 0) {
        r->params = arena_alloc_array(arena, MQ_FormalParam, param_count);
        memcpy(r->params, params, sizeof(MQ_FormalParam) * param_count);
    }
    return r;
}

MQ_Routine *mq_routine_intrinsic(M_Arena *arena, string name,
                                 MQ_RoutineKind kind, string intrinsic_name) {
    MQ_Routine *r    = arena_alloc_zero(arena, sizeof(MQ_Routine));
    r->name          = name;
    r->kind          = kind;
    r->is_intrinsic  = 1;
    r->intrinsic_name = intrinsic_name;
    return r;
}

// ============================================================
// Circuit
// ============================================================

MQ_Circuit *mq_circuit(M_Arena *arena, string name,
                       MQ_Register *registers, u32 register_count,
                       u32 total_qubits, u32 total_cbits) {
    MQ_Circuit *c    = arena_alloc_zero(arena, sizeof(MQ_Circuit));
    c->name          = name;
    c->register_count = register_count;
    c->total_qubits  = total_qubits;
    c->total_cbits   = total_cbits;
    
    if (register_count > 0) {
        c->registers = arena_alloc_array(arena, MQ_Register, register_count);
        memcpy(c->registers, registers, sizeof(MQ_Register) * register_count);
    }
    
    c->measure_map = arena_alloc_array(arena, i32, total_qubits);
    for (u32 i = 0; i < total_qubits; i++)
        c->measure_map[i] = -1;
    
    c->body = mq_stmt_block(arena, 0, 0);
    
    return c;
}

u32 mq_circuit_add_param(M_Arena *arena, MQ_Circuit *circuit,
                         string name, MQ_Expr *default_val) {
    u32 idx = circuit->param_count++;
    
    string  *new_names     = arena_alloc_array(arena, string,   circuit->param_count);
    MQ_Expr **new_defaults = arena_alloc_array(arena, MQ_Expr *, circuit->param_count);
    
    if (idx > 0) {
        memcpy(new_names,    circuit->param_names,    sizeof(string)    * idx);
        memcpy(new_defaults, circuit->param_defaults, sizeof(MQ_Expr *) * idx);
    }
    
    new_names[idx]    = name;
    new_defaults[idx] = default_val;
    
    circuit->param_names    = new_names;
    circuit->param_defaults = new_defaults;
    
    return idx;
}

// ============================================================
// Program
// ============================================================

MQ_Program *mq_program(M_Arena *arena, string name, MQ_SourceLang lang) {
    MQ_Program *p    = arena_alloc_zero(arena, sizeof(MQ_Program));
    p->name          = name;
    p->source_lang   = lang;
    p->mq_ir_version[0] = MQ_IR_VERSION_MAJOR;
    p->mq_ir_version[1] = MQ_IR_VERSION_MINOR;
    p->mq_ir_version[2] = MQ_IR_VERSION_PATCH;
    return p;
}

void mq_program_add_routine(M_Arena *arena, MQ_Program *prog, MQ_Routine *routine) {
    u32 n = prog->routine_count++;
    MQ_Routine **new_arr = arena_alloc_array(arena, MQ_Routine *, prog->routine_count);
    if (n > 0)
        memcpy(new_arr, prog->routines, sizeof(MQ_Routine *) * n);
    new_arr[n]    = routine;
    prog->routines = new_arr;
}

void mq_program_add_circuit(M_Arena *arena, MQ_Program *prog, MQ_Circuit *circuit) {
    u32 n = prog->circuit_count++;
    MQ_Circuit **new_arr = arena_alloc_array(arena, MQ_Circuit *, prog->circuit_count);
    if (n > 0)
        memcpy(new_arr, prog->circuits, sizeof(MQ_Circuit *) * n);
    new_arr[n]    = circuit;
    prog->circuits = new_arr;
}



//- Testing stuff

#include <stdio.h>

static void wr_indent(FILE *f, int d) {
    for (int i = 0; i < d; i++) fprintf(f, "  ");
}

static void wr_str(FILE *f, string s) {
    if (str_is_null(s)) fprintf(f, "(null)");
    else                fprintf(f, "%.*s", str_expand(s));
}

static void wr_type(FILE *f, MQ_Type *t) {
    if (!t) { fprintf(f, "void"); return; }
    switch (t->kind) {
        case MQ_Type_Void:     fprintf(f, "void");           break;
        case MQ_Type_Bool:     fprintf(f, "bool");           break;
        case MQ_Type_Int:      fprintf(f, "int%u", t->width); break;
        case MQ_Type_Float:    fprintf(f, "float");          break;
        case MQ_Type_Angle:    fprintf(f, "angle");          break;
        case MQ_Type_Bit:      fprintf(f, "bit");            break;
        case MQ_Type_Qubit:    fprintf(f, "qubit");          break;
        case MQ_Type_BitReg:   fprintf(f, "bitreg[%u]",   t->width); break;
        case MQ_Type_QubitReg: fprintf(f, "qubitreg[%u]", t->width); break;
        case MQ_Type_Array:
        fprintf(f, "array[%u]<", t->width);
        wr_type(f, t->element_type);
        fprintf(f, ">");
        break;
    }
}

static void wr_expr(FILE *f, MQ_Expr *e) {
    if (!e) { fprintf(f, "(null)"); return; }
    
    static const char *binop_sym[] = {
        "+", "-", "*", "/", "%", "**",
        "&", "|", "^", "<<", ">>",
        "==", "!=", "<", "<=", ">", ">=",
        "&&", "||"
    };
    static const char *unop_sym[] = {
        "-", "!", "~",
        "sin", "cos", "tan", "asin", "acos", "atan",
        "sqrt", "exp", "log", "abs"
    };
    
    switch (e->kind) {
        case MQ_Expr_BoolLit:  fprintf(f, "%s", e->lit.bool_val ? "true" : "false"); break;
        case MQ_Expr_IntLit:   fprintf(f, "%lld", (long long)e->lit.int_val);        break;
        case MQ_Expr_FloatLit: fprintf(f, "%g",   e->lit.float_val);                 break;
        case MQ_Expr_Symbol:
        case MQ_Expr_Var:      wr_str(f, e->name);                                   break;
        case MQ_Expr_QubitRef: fprintf(f, "q[%u]", e->qubit_id);                     break;
        
        case MQ_Expr_RegIndex: {
            wr_str(f, e->reg.name);
            fprintf(f, "[");
            wr_expr(f, e->reg.index_expr);
            fprintf(f, "]");
        } break;
        
        case MQ_Expr_BinOp: {
            fprintf(f, "(");
            wr_expr(f, e->bin.lhs);
            fprintf(f, " %s ", binop_sym[e->bin.op]);
            wr_expr(f, e->bin.rhs);
            fprintf(f, ")");
        } break;
        
        case MQ_Expr_UnOp: {
            if (e->un.op >= MQ_UnOp_Sin) {
                fprintf(f, "%s(", unop_sym[e->un.op]);
                wr_expr(f, e->un.operand);
                fprintf(f, ")");
            } else {
                fprintf(f, "%s", unop_sym[e->un.op]);
                wr_expr(f, e->un.operand);
            }
        } break;
        
        case MQ_Expr_Array:
        case MQ_Expr_Call: {
            if (!str_is_null(e->call.name)) wr_str(f, e->call.name);
            else                            fprintf(f, "array");
            fprintf(f, "(");
            for (u32 i = 0; i < e->call.arg_count; i++) {
                if (i) fprintf(f, ", ");
                wr_expr(f, e->call.args[i]);
            }
            fprintf(f, ")");
        } break;
        
        case MQ_Expr_BitRead: {
            wr_str(f, e->bit.reg_name);
            fprintf(f, "[%u]", e->bit.index);
            break;
        }
    }
}

static const char *gate_name_table[] = {
    "I", "H", "X", "Y", "Z", "S", "Sdg", "T", "Tdg",
    "P", "RX", "RY", "RZ", "U",
    "SWAP", "ISWAP", "RZZ", "RXX", "RYY",
    "CCX", "CSWAP",
    "Custom", "Unitary"
};

static void wr_instr(FILE *f, MQ_Instruction *in, int d)
{
    wr_indent(f, d);
    switch (in->type) {
        
        case MQ_Instr_Gate: {
            if (in->gate.gate == MQ_Gate_Custom)
                wr_str(f, in->gate.custom_name);
            else
                fprintf(f, "%s", gate_name_table[in->gate.gate]);
            
            if (in->gate.param_count > 0) {
                fprintf(f, "(");
                for (u8 i = 0; i < in->gate.param_count; i++) {
                    if (i) fprintf(f, ", ");
                    if (in->gate.params_symbolic) wr_expr(f, in->gate.param_exprs[i]);
                    else                          fprintf(f, "%g", in->gate.params[i]);
                }
                fprintf(f, ")");
            }
            
            for (u8 i = 0; i < in->qubit_count; i++)
                fprintf(f, " [%u]", in->qubits[i]);
            
            if (in->gate.control_count > 0) {
                fprintf(f, " ctrl=[");
                for (u8 i = 0; i < in->gate.control_count; i++) {
                    if (i) fprintf(f, ", ");
                    fprintf(f, "%u:|%u>", in->gate.controls[i], in->gate.control_states[i]);
                }
                fprintf(f, "]");
            }
            
            if (in->gate.has_classical_cond)
                fprintf(f, " if(c[%u] == %u)", in->gate.classical_bit, in->gate.classical_val);
        } break;
        
        case MQ_Instr_Measure: {
            fprintf(f, "MEASURE [%u]", in->qubits[0]);
            if (in->measure.has_target) fprintf(f, " -> c[%u]", in->measure.clbit);
            else                        fprintf(f, " (discard)");
        } break;
        
        case MQ_Instr_Reset: {
            fprintf(f, "RESET [%u]", in->qubits[0]);
        } break;
        
        case MQ_Instr_Barrier: {
            fprintf(f, "BARRIER");
            for (u8 i = 0; i < in->qubit_count; i++) fprintf(f, " [%u]", in->qubits[i]);
        } break;
        
        case MQ_Instr_Delay: {
            static const char *unit_str[] = { "dt", "ns", "us", "ms", "s" };
            fprintf(f, "DELAY %g%s", in->delay.duration, unit_str[in->delay.unit]);
            for (u8 i = 0; i < in->qubit_count; i++) fprintf(f, " [%u]", in->qubits[i]);
        } break;
    }
    fprintf(f, "\n");
}

static void wr_stmt(FILE *f, MQ_Stmt *s, int d) {
    if (!s) return;
    
    static const char *binop_sym[] = {
        "+", "-", "*", "/", "%", "**",
        "&", "|", "^", "<<", ">>",
        "==", "!=", "<", "<=", ">", ">=",
        "&&", "||"
    };
    
    switch (s->kind) {
        case MQ_Stmt_Block: {
            for (u32 i = 0; i < s->block.count; i++)
                wr_stmt(f, s->block.stmts[i], d);
        } break;
        
        case MQ_Stmt_Instr: {
            wr_instr(f, &s->instr, d);
        } break;
        
        case MQ_Stmt_Adjoint: {
            wr_indent(f, d); fprintf(f, "ADJOINT {\n");
            wr_stmt(f, s->adjoint_body, d + 1);
            wr_indent(f, d); fprintf(f, "}\n");
        } break;
        
        case MQ_Stmt_DeclQubit: {
            wr_indent(f, d);
            fprintf(f, "USE ");
            wr_str(f, s->decl_qubit.name);
            fprintf(f, "[%u] base_id=%u\n", s->decl_qubit.count, s->decl_qubit.base_id);
        } break;
        
        case MQ_Stmt_DeclClassical: {
            wr_indent(f, d);
            fprintf(f, "LET ");
            wr_str(f, s->decl_classical.name);
            fprintf(f, " : ");
            wr_type(f, s->decl_classical.type);
            if (s->decl_classical.init) {
                fprintf(f, " = ");
                wr_expr(f, s->decl_classical.init);
            }
            fprintf(f, "\n");
        } break;
        
        case MQ_Stmt_DeclParam: {
            wr_indent(f, d);
            fprintf(f, "PARAM ");
            wr_str(f, s->decl_param.name);
            if (s->decl_param.default_val) {
                fprintf(f, " = ");
                wr_expr(f, s->decl_param.default_val);
            }
            fprintf(f, "\n");
        } break;
        
        case MQ_Stmt_Set: {
            wr_indent(f, d);
            wr_expr(f, s->set.lhs);
            if (s->set.augmented) fprintf(f, " %s= ", binop_sym[s->set.aug_op]);
            else                  fprintf(f, " = ");
            wr_expr(f, s->set.rhs);
            fprintf(f, "\n");
        } break;
        
        case MQ_Stmt_If: {
            for (u32 i = 0; i < s->if_stmt.count; i++) {
                MQ_IfBranch *br = &s->if_stmt.branches[i];
                wr_indent(f, d);
                if (!br->cond) {
                    fprintf(f, "ELSE {\n");
                    wr_stmt(f, br->body, d + 1);
                    wr_indent(f, d); fprintf(f, "}\n");
                    continue;
                }
                fprintf(f, i == 0 ? "IF (" : "ELIF (");
                wr_expr(f, br->cond);
                fprintf(f, ") {\n");
                wr_stmt(f, br->body, d + 1);
                wr_indent(f, d); fprintf(f, "}\n");
            }
        } break;
        
        case MQ_Stmt_For: {
            wr_indent(f, d);
            fprintf(f, "FOR ");
            wr_str(f, s->for_loop.var_name);
            fprintf(f, " IN ");
            wr_expr(f, s->for_loop.iterable);
            fprintf(f, " {\n");
            wr_stmt(f, s->for_loop.body, d + 1);
            wr_indent(f, d); fprintf(f, "}\n");
        } break;
        
        case MQ_Stmt_While: {
            wr_indent(f, d);
            fprintf(f, "WHILE (");
            wr_expr(f, s->while_loop.cond);
            fprintf(f, ") {\n");
            wr_stmt(f, s->while_loop.body, d + 1);
            wr_indent(f, d); fprintf(f, "}\n");
        } break;
        
        case MQ_Stmt_Break:    wr_indent(f, d); fprintf(f, "BREAK\n");    break;
        case MQ_Stmt_Continue: wr_indent(f, d); fprintf(f, "CONTINUE\n"); break;
        
        case MQ_Stmt_Return: {
            wr_indent(f, d);
            fprintf(f, "RETURN");
            if (s->return_val) { fprintf(f, " "); wr_expr(f, s->return_val); }
            fprintf(f, "\n");
        } break;
        
        case MQ_Stmt_Call: {
            wr_indent(f, d);
            wr_str(f, s->call.callee);
            fprintf(f, "(");
            for (u32 i = 0; i < s->call.arg_count; i++) {
                if (i) fprintf(f, ", ");
                wr_expr(f, s->call.args[i]);
            }
            fprintf(f, ")\n");
        } break;
        
        case MQ_Stmt_Comment: {
            wr_indent(f, d);
            fprintf(f, "// ");
            wr_str(f, s->comment_text);
            fprintf(f, "\n");
        } break;
        
        case MQ_Stmt_Pragma: {
            wr_indent(f, d);
            fprintf(f, "#pragma ");
            wr_str(f, s->pragma.key);
            fprintf(f, " ");
            wr_str(f, s->pragma.value);
            fprintf(f, "\n");
            break;
        }
    }
}

static void wr_routine(FILE *f, MQ_Routine *r) {
    fprintf(f, "  ROUTINE \"");
    wr_str(f, r->name);
    fprintf(f, "\" %s", r->kind == MQ_Routine_Gate ? "Gate" : "Operation");
    
    if (r->is_intrinsic) {
        fprintf(f, " [intrinsic: ");
        wr_str(f, r->intrinsic_name);
        fprintf(f, "]\n");
        return;
    }
    
    if (!str_is_null(r->doc_comment)) {
        fprintf(f, "\n    // ");
        wr_str(f, r->doc_comment);
    }
    fprintf(f, "\n");
    
    if (r->param_count > 0) {
        fprintf(f, "    params:");
        for (u32 i = 0; i < r->param_count; i++) {
            fprintf(f, " ");
            wr_str(f, r->params[i].name);
            fprintf(f, ":");
            wr_type(f, r->params[i].type);
        }
        fprintf(f, "\n");
    }
    
    fprintf(f, "    returns: ");
    wr_type(f, r->return_type);
    fprintf(f, "\n");
    
    fprintf(f, "    BODY\n");
    wr_stmt(f, r->body, 3);
}

static void wr_circuit(FILE *f, MQ_Circuit *c) {
    fprintf(f, "  CIRCUIT \"");
    wr_str(f, c->name);
    fprintf(f, "\" qubits=%u cbits=%u global_phase=%g\n",
            c->total_qubits, c->total_cbits, c->global_phase);
    
    // Registers
    fprintf(f, "    registers:\n");
    for (u32 i = 0; i < c->register_count; i++) {
        MQ_Register *r = &c->registers[i];
        fprintf(f, "      %s \"", r->kind == MQ_Reg_Quantum ? "Q" : "C");
        wr_str(f, r->name);
        fprintf(f, "\" size=%u base=%u\n", r->size, r->base_id);
    }
    
    // Symbolic params
    if (c->param_count > 0) {
        fprintf(f, "    params:");
        for (u32 i = 0; i < c->param_count; i++) {
            fprintf(f, " ");
            wr_str(f, c->param_names[i]);
            if (c->param_defaults[i]) {
                fprintf(f, "=");
                wr_expr(f, c->param_defaults[i]);
            }
        }
        fprintf(f, "\n");
    }
    
    // Backend hint
    if (c->backend_hint) {
        fprintf(f, "    backend: ");
        wr_str(f, c->backend_hint->backend_name);
        fprintf(f, " basis=[");
        wr_str(f, c->backend_hint->basis_gates);
        fprintf(f, "]\n");
    }
    
    fprintf(f, "    BODY\n");
    wr_stmt(f, c->body, 3);
}

static const char *lang_name_table[] = {
    "Unknown", "Qiskit", "Q#", "Cirq", "OpenQASM2", "OpenQASM3", "MQ"
};

void mq_program_write(FILE *f, MQ_Program *prog) {
    fprintf(f, "PROGRAM \"");
    wr_str(f, prog->name);
    fprintf(f, "\" [%s] version=%u.%u.%u\n",
            lang_name_table[prog->source_lang],
            prog->mq_ir_version[0],
            prog->mq_ir_version[1],
            prog->mq_ir_version[2]);
    
    if (!str_is_null(prog->source_file)) {
        fprintf(f, "  source: ");
        wr_str(f, prog->source_file);
        fprintf(f, "\n");
    }
    
    fprintf(f, "\n");
    
    for (u32 i = 0; i < prog->routine_count; i++) {
        wr_routine(f, prog->routines[i]);
        fprintf(f, "\n");
    }
    
    for (u32 i = 0; i < prog->circuit_count; i++) {
        wr_circuit(f, prog->circuits[i]);
        fprintf(f, "\n");
    }
}



void mq_ir_test(M_Arena *arena, string filename) {
    MQ_Program *prog = mq_program(arena, str_lit("mq_test"), MQ_Lang_MQ);
    
    // --------------------------------------------------------
    // Routine: "h_rz" Gate
    //   params: angle:Angle
    //   body:   H [0], RZ(angle) [0]
    // --------------------------------------------------------
    {
        MQ_Type        *angle_type = mq_type_scalar(arena, MQ_Type_Angle);
        MQ_FormalParam  params[1]  = { mq_formal_param(str_lit("angle"), angle_type, 0) };
        
        u32 q0 = 0;
        
        // H [0]
        MQ_Instruction h_instr = mq_instr_gate(MQ_Gate_H, &q0, 1);
        
        // RZ(angle) [0]  -- symbolic
        MQ_Expr       *sym_angle   = mq_expr_symbol(arena, str_lit("angle"));
        MQ_Expr       *param_exprs[1] = { sym_angle };
        MQ_Instruction rz_instr = mq_instr_gate_sym(MQ_Gate_RZ, &q0, 1, param_exprs, 1);
        
        MQ_Stmt *stmts[2] = {
            mq_stmt_instr(arena, h_instr),
            mq_stmt_instr(arena, rz_instr),
        };
        MQ_Stmt *body = mq_stmt_block(arena, stmts, 2);
        
        MQ_Routine *r = mq_routine(arena, str_lit("h_rz"), MQ_Routine_Gate,
                                   params, 1, 0, body);
        r->doc_comment = str_lit("Apply H then RZ(angle) to qubit 0");
        mq_program_add_routine(arena, prog, r);
    }
    
    // --------------------------------------------------------
    // Routine: "cx_intrinsic" (intrinsic -- maps to native CNOT)
    // --------------------------------------------------------
    {
        MQ_Routine *r = mq_routine_intrinsic(arena, str_lit("cx_intrinsic"),
                                             MQ_Routine_Gate, str_lit("CX"));
        mq_program_add_routine(arena, prog, r);
    }
    
    // --------------------------------------------------------
    // Circuit 1: "bell"
    //   Q "q" [0,1]   C "c" [0,1]
    //   H [0]
    //   X [1]  ctrl=[0:|1>]        (CNOT)
    //   MEASURE [0] -> c[0]
    //   MEASURE [1] -> c[1]
    //   IF (c[0] == 1): X [0]
    //   ELSE:           I [0]
    // --------------------------------------------------------
    {
        MQ_QubitMeta meta[2] = {
            { .id=0, .style=MQ_Qubit_Flat, .register_name=str_lit("q"), .register_index=0 },
            { .id=1, .style=MQ_Qubit_Flat, .register_name=str_lit("q"), .register_index=1 },
        };
        MQ_Register regs[2] = {
            mq_register_quantum  (str_lit("q"), 2, 0, meta),
            mq_register_classical(str_lit("c"), 2, 0),
        };
        MQ_Circuit *bell = mq_circuit(arena, str_lit("bell"), regs, 2, 2, 2);
        
        // H [0]
        u32 q0 = 0, q1 = 1;
        MQ_Instruction h0 = mq_instr_gate(MQ_Gate_H, &q0, 1);
        
        // CNOT: X [1] ctrl=[0:|1>]
        MQ_Instruction cnot = mq_instr_gate(MQ_Gate_X, &q1, 1);
        mq_instr_add_control(&cnot, 0, 1);
        
        // MEASURE
        MQ_Instruction m0 = mq_instr_measure(0, 0);
        MQ_Instruction m1 = mq_instr_measure(1, 1);
        
        // IF c[0]==1: X [0]  ELSE: I [0]
        MQ_Expr *bit0 = mq_expr_bit_read(arena, str_lit("c"), 0);
        MQ_Expr *one  = mq_expr_int(arena, 1);
        MQ_Expr *cond = mq_expr_binop(arena, MQ_BinOp_Eq, bit0, one);
        
        MQ_Instruction x0 = mq_instr_gate(MQ_Gate_X, &q0, 1);
        MQ_Instruction i0 = mq_instr_gate(MQ_Gate_I, &q0, 1);
        
        MQ_IfBranch branches[2] = {
            mq_if_branch  (cond, mq_stmt_instr(arena, x0)),
            mq_else_branch(      mq_stmt_instr(arena, i0)),
        };
        
        MQ_Stmt *body_stmts[] = {
            mq_stmt_instr(arena, h0),
            mq_stmt_instr(arena, cnot),
            mq_stmt_instr(arena, m0),
            mq_stmt_instr(arena, m1),
            mq_stmt_if(arena, branches, 2),
        };
        bell->body = mq_stmt_block(arena, body_stmts, 5);
        bell->measure_map[0] = 0;
        bell->measure_map[1] = 1;
        
        mq_program_add_circuit(arena, prog, bell);
    }
    
    // --------------------------------------------------------
    // Circuit 2: "advanced"
    //   Q "q" [0,1,2]   C "c" [0]
    //
    //   Exercises:
    //     comment, pragma
    //     decl_param (unbound "theta")
    //     decl_classical
    //     symbolic gate (RZ(theta))
    //     classically conditioned gate
    //     barrier
    //     adjoint block
    //     delay
    //     reset
    //     for loop (range)
    //     while loop
    //     set / augmented set
    //     call to h_rz routine
    //     return
    //     custom gate
    //     CCX (three-qubit)
    //     measure discard
    // --------------------------------------------------------
    {
        MQ_QubitMeta meta[3] = {
            { .id=0, .style=MQ_Qubit_Flat, .register_name=str_lit("q"), .register_index=0 },
            { .id=1, .style=MQ_Qubit_Flat, .register_name=str_lit("q"), .register_index=1 },
            { .id=2, .style=MQ_Qubit_Flat, .register_name=str_lit("q"), .register_index=2 },
        };
        MQ_Register regs[2] = {
            mq_register_quantum  (str_lit("q"), 3, 0, meta),
            mq_register_classical(str_lit("c"), 1, 0),
        };
        MQ_Circuit *adv = mq_circuit(arena, str_lit("advanced"), regs, 2, 3, 1);
        mq_circuit_add_param(arena, adv, str_lit("theta"), 0);
        
        MQ_Type *int_type   = mq_type_int(arena, 64);
        MQ_Type *angle_type = mq_type_scalar(arena, MQ_Type_Angle);
        
        u32 q0 = 0, q1 = 1, q2 = 2;
        u32 q01[2] = {0, 1};
        u32 q012[3] = {0, 1, 2};
        
        // -- comment
        MQ_Stmt *s_comment = mq_stmt_comment(arena, str_lit("advanced circuit test"));
        
        // -- pragma
        MQ_Stmt *s_pragma = mq_stmt_pragma(arena, str_lit("optimization_level"), str_lit("2"));
        
        // -- PARAM theta (unbound)
        MQ_Stmt *s_param = mq_stmt_decl_param(arena, str_lit("theta"), 0);
        
        // -- LET i : int64 = 0
        MQ_Stmt *s_decl_i = mq_stmt_decl_classical(arena, str_lit("i"), int_type,
                                                   mq_expr_int(arena, 0));
        
        // -- LET phi : angle = sin(theta)
        MQ_Expr *sym_theta = mq_expr_symbol(arena, str_lit("theta"));
        MQ_Expr *sin_theta = mq_expr_unop(arena, MQ_UnOp_Sin, sym_theta);
        MQ_Stmt *s_decl_phi = mq_stmt_decl_classical(arena, str_lit("phi"), angle_type,
                                                     sin_theta);
        
        // -- RZ(theta) [0]  symbolic
        MQ_Expr *param_exprs[1] = { mq_expr_symbol(arena, str_lit("theta")) };
        MQ_Instruction rz_sym = mq_instr_gate_sym(MQ_Gate_RZ, &q0, 1, param_exprs, 1);
        MQ_Stmt *s_rz = mq_stmt_instr(arena, rz_sym);
        
        // -- U(theta, phi, 0.0) [1]  mixed concrete+symbolic
        MQ_Expr *u_params[3] = {
            mq_expr_symbol(arena, str_lit("theta")),
            mq_expr_symbol(arena, str_lit("phi")),
            mq_expr_float(arena, 0.0),
        };
        MQ_Instruction u_instr = mq_instr_gate_sym(MQ_Gate_U, &q1, 1, u_params, 3);
        MQ_Stmt *s_u = mq_stmt_instr(arena, u_instr);
        
        // -- CCX [0,1,2]  (Toffoli)
        MQ_Instruction ccx = mq_instr_gate(MQ_Gate_CCX, q012, 3);
        MQ_Stmt *s_ccx = mq_stmt_instr(arena, ccx);
        
        // -- custom gate "my_gate" [0]
        MQ_Instruction cust = mq_instr_gate_custom(str_lit("my_gate"), &q0, 1, 0, 0);
        MQ_Stmt *s_custom = mq_stmt_instr(arena, cust);
        
        // -- MEASURE [2] -> c[0] then classically conditioned S on [0]
        MQ_Instruction meas2 = mq_instr_measure(2, 0);
        MQ_Stmt *s_meas2 = mq_stmt_instr(arena, meas2);
        
        MQ_Instruction s_cond_instr = mq_instr_gate(MQ_Gate_S, &q0, 1);
        mq_instr_set_classical_cond(&s_cond_instr, 0, 1);
        MQ_Stmt *s_classical_cond = mq_stmt_instr(arena, s_cond_instr);
        
        // -- BARRIER [0,1]
        MQ_Instruction barrier = mq_instr_barrier(q01, 2);
        MQ_Stmt *s_barrier = mq_stmt_instr(arena, barrier);
        
        // -- ADJOINT { H[0], T[1] }
        {
            MQ_Instruction h_adj = mq_instr_gate(MQ_Gate_H, &q0, 1);
            MQ_Instruction t_adj = mq_instr_gate(MQ_Gate_T, &q1, 1);
            MQ_Stmt *adj_inner[2] = {
                mq_stmt_instr(arena, h_adj),
                mq_stmt_instr(arena, t_adj),
            };
            MQ_Stmt *adj_block = mq_stmt_block(arena, adj_inner, 2);
            MQ_Stmt *s_adjoint = mq_stmt_adjoint(arena, adj_block);
            
            // -- DELAY 100ns [0]
            MQ_Instruction delay = mq_instr_delay(&q0, 1, 100.0, MQ_Time_ns);
            MQ_Stmt *s_delay = mq_stmt_instr(arena, delay);
            
            // -- RESET [1]
            MQ_Instruction reset = mq_instr_reset(q1);
            MQ_Stmt *s_reset = mq_stmt_instr(arena, reset);
            
            // -- MEASURE [0] discard
            MQ_Instruction m_disc = mq_instr_measure_discard(q0);
            MQ_Stmt *s_disc = mq_stmt_instr(arena, m_disc);
            
            // -- FOR i IN range(3) { H [0] }
            MQ_Expr *range_arg[1] = { mq_expr_int(arena, 3) };
            MQ_Expr *range_call   = mq_expr_call(arena, str_lit("range"), range_arg, 1);
            MQ_Instruction h_for  = mq_instr_gate(MQ_Gate_H, &q0, 1);
            MQ_Stmt *s_for = mq_stmt_for(arena, str_lit("i"), int_type,
                                         range_call, mq_stmt_instr(arena, h_for));
            
            // -- i += 1
            MQ_Stmt *s_set_aug = mq_stmt_set_aug(arena,
                                                 mq_expr_var(arena, str_lit("i")),
                                                 MQ_BinOp_Add,
                                                 mq_expr_int(arena, 1));
            
            // -- WHILE (i < 3) { H[0]; BREAK }
            MQ_Expr *i_var    = mq_expr_var(arena, str_lit("i"));
            MQ_Expr *three    = mq_expr_int(arena, 3);
            MQ_Expr *while_c  = mq_expr_binop(arena, MQ_BinOp_Lt, i_var, three);
            
            MQ_Instruction h_while = mq_instr_gate(MQ_Gate_H, &q0, 1);
            MQ_Stmt *while_stmts[2] = {
                mq_stmt_instr(arena, h_while),
                mq_stmt_break(arena),
            };
            MQ_Stmt *s_while = mq_stmt_while(arena, while_c,
                                             mq_stmt_block(arena, while_stmts, 2));
            
            // -- call h_rz(theta)
            MQ_Expr *call_args[1] = { mq_expr_symbol(arena, str_lit("theta")) };
            MQ_Stmt *s_call = mq_stmt_call(arena, str_lit("h_rz"), call_args, 1);
            
            // -- RETURN (void)
            MQ_Stmt *s_ret = mq_stmt_return(arena, 0);
            
            MQ_Stmt *all[] = {
                s_comment, s_pragma, s_param, s_decl_i, s_decl_phi,
                s_rz, s_u, s_ccx, s_custom,
                s_meas2, s_classical_cond,
                s_barrier, s_adjoint, s_delay, s_reset, s_disc,
                s_for, s_set_aug, s_while, s_call, s_ret,
            };
            adv->body = mq_stmt_block(arena, all, 21);
        }
        
        mq_program_add_circuit(arena, prog, adv);
    }
    
    FILE* f = fopen((char*)filename.str, "w");
    mq_program_write(f, prog);
    fclose(f);
}
