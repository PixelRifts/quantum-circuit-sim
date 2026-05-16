/*
 * MQ_IR.h -- MetaQuantum High-Level Intermediate Representation
 *
 * A language-agnostic IR for quantum programs that:
 *   - Q#, Qiskit, and Cirq can compile into
 *   - can be lowered back out to any of the three
 *
 * Layered architecture (low to high):
 *
 *   MQ_Instruction   single quantum/classical primitive op
 *   MQ_Stmt          control-flow nodes wrapping instructions
 *   MQ_Routine       named, reusable gate / operation definition
 *   MQ_Circuit       one complete quantum circuit
 *   MQ_Program       full compilation unit (routines + circuits)
 *
 * Memory model
 *   All allocation goes through the arena / pool API from mem.h.
 *   Nothing in this header calls malloc.  An M_Arena* is threaded
 *   through every construction helper; the arena owns all node
 *   memory for the lifetime of the program tree.
 *
 * String model
 *   Every name / label is a `string` (string_const from str.h): a
 *   non-owning {u8*, u64} slice into arena memory.  A zero-initialised
 *   string ({0}) represents an absent name; test with str_is_null().
 *
 * Feature scope
 *   Minimal intersection of Qiskit, Cirq, and Q#:
 *     - standard gate set present in all three
 *     - symbolic / parametric rotation angles
 *     - mid-circuit measurement and classical conditioning
 *     - qubit allocation (dynamic circuits, Q# use-scope)
 *     - classical control flow: if / for / while
 *     - user-defined gate and operation routines
 *     - circuit inversion (circuit.inverse / cirq.inverse / Adjoint)
 *     - backend metadata for round-trip fidelity
 *
 * Version: 0.3.0
 */

#ifndef IR_H
#define IR_H

#include "defines.h"
#include "base/str.h"
#include "base/mem.h"



/* ======================================================================
 * Constants
 * ====================================================================== */

#define MQ_MAX_GATE_QUBITS  3
#define MQ_MAX_CONTROLS     8
#define MQ_MAX_GATE_PARAMS  3

#define MQ_IR_VERSION_MAJOR 0
#define MQ_IR_VERSION_MINOR 3
#define MQ_IR_VERSION_PATCH 0


/* ======================================================================
 * Forward declarations
 * ====================================================================== */

typedef struct MQ_Type      MQ_Type;
typedef struct MQ_Expr      MQ_Expr;
typedef struct MQ_Stmt      MQ_Stmt;
typedef struct MQ_Routine   MQ_Routine;
typedef struct MQ_Circuit   MQ_Circuit;
typedef struct MQ_Program   MQ_Program;
typedef struct MQ_Register  MQ_Register;


/* ======================================================================
 * Type system
 *
 * Used for routine parameter / return types, classical variable
 * declarations, and type annotations on symbolic expressions.
 * ====================================================================== */

typedef enum MQ_TypeKind {
    MQ_Type_Void,
    MQ_Type_Bool,
    MQ_Type_Int,
    MQ_Type_Float,
    MQ_Type_Angle,
    MQ_Type_Bit,
    MQ_Type_Qubit,
    MQ_Type_BitReg,
    MQ_Type_QubitReg,
    MQ_Type_Array,
} MQ_TypeKind;

/*
 * MQ_Type_Int   : width = storage bit-width (e.g. 64)
 * MQ_Type_Array : width = element count, element_type = inner type
 * MQ_Type_BitReg / MQ_Type_QubitReg : width = register length
 */
struct MQ_Type {
    MQ_TypeKind  kind;
    u32          width;
    MQ_Type     *element_type;
};


/* ======================================================================
 * Expressions
 *
 * MQ_Expr nodes appear wherever a classical value is needed:
 *   - gate rotation angles (possibly symbolic)
 *   - if / while conditions
 *   - loop bounds
 *   - routine call arguments
 *
 * Symbolic parameters map to:
 *   Qiskit  -- Parameter / ParameterExpression
 *   Cirq    -- sympy.Symbol
 *   Q#      -- unbound Double / Angle function argument
 * ====================================================================== */

typedef enum MQ_BinOp {
    MQ_BinOp_Add, MQ_BinOp_Sub, MQ_BinOp_Mul,
    MQ_BinOp_Div, MQ_BinOp_Mod, MQ_BinOp_Pow,
    MQ_BinOp_And, MQ_BinOp_Or,  MQ_BinOp_Xor,
    MQ_BinOp_Shl, MQ_BinOp_Shr,
    MQ_BinOp_Eq,  MQ_BinOp_Ne,
    MQ_BinOp_Lt,  MQ_BinOp_Le,
    MQ_BinOp_Gt,  MQ_BinOp_Ge,
    MQ_BinOp_LogAnd, MQ_BinOp_LogOr,
} MQ_BinOp;

typedef enum MQ_UnOp {
    MQ_UnOp_Neg,
    MQ_UnOp_Not,
    MQ_UnOp_BitNot,
    // Transcendentals needed for symbolic rotation expressions
    MQ_UnOp_Sin,  MQ_UnOp_Cos,  MQ_UnOp_Tan,
    MQ_UnOp_Asin, MQ_UnOp_Acos, MQ_UnOp_Atan,
    MQ_UnOp_Sqrt, MQ_UnOp_Exp,  MQ_UnOp_Log,
    MQ_UnOp_Abs,
} MQ_UnOp;

typedef enum MQ_ExprKind {
    // Literals
    MQ_Expr_BoolLit,
    MQ_Expr_IntLit,
    MQ_Expr_FloatLit,
    
    // Unbound symbolic parameter (Qiskit Parameter, Cirq sympy.Symbol)
    MQ_Expr_Symbol,
    
    // Variable and register references
    MQ_Expr_Var,
    MQ_Expr_QubitRef,
    MQ_Expr_RegIndex,
    
    // Compound
    MQ_Expr_BinOp,
    MQ_Expr_UnOp,
    MQ_Expr_Call,
    MQ_Expr_Array,
    
    // Read a classical bit; used to form mid-circuit feedback conditions
    MQ_Expr_BitRead,
} MQ_ExprKind;

/*
 * MQ_Expr_Symbol / MQ_Expr_Var : .name
 * MQ_Expr_QubitRef              : .qubit_id
 * MQ_Expr_RegIndex              : .reg.{name, index_expr}
 * MQ_Expr_BinOp                 : .bin.{op, lhs, rhs}
 * MQ_Expr_UnOp                  : .un.{op, operand}
 * MQ_Expr_Call                  : .call.{name, args, arg_count}
 * MQ_Expr_Array                 : .call.args, .call.arg_count (name == {0})
 * MQ_Expr_BitRead               : .bit.{reg_name, index}
 */
struct MQ_Expr {
    MQ_ExprKind  kind;
    MQ_Type     *type;
    
    union {
        struct {
            b8  bool_val;
            i64 int_val;
            f64 float_val;
        } lit;
        
        string name;
        
        u32 qubit_id;
        
        struct {
            string   name;
            MQ_Expr *index_expr;
        } reg;
        
        struct {
            MQ_BinOp  op;
            MQ_Expr  *lhs;
            MQ_Expr  *rhs;
        } bin;
        
        struct {
            MQ_UnOp  op;
            MQ_Expr *operand;
        } un;
        
        struct {
            string    name;
            MQ_Expr **args;
            u32       arg_count;
        } call;
        
        struct {
            string reg_name;
            u32    index;
        } bit;
    };
};


/* ======================================================================
 * Qubit addressing
 *
 * A flat u32 qubit_id is the canonical handle everywhere in the IR.
 * MQ_QubitMeta preserves the original framework-specific addressing
 * so back-end emitters can reconstruct idiomatic output.
 *
 *   Qiskit -- flat index into a QuantumRegister
 *   Q#     -- named Qubit variable  (MQ_Qubit_Named)
 *   Cirq   -- LineQubit(x) / GridQubit(row, col) / NamedQubit("n")
 * ====================================================================== */

typedef enum MQ_QubitStyle {
    MQ_Qubit_Flat,
    MQ_Qubit_Named,
    MQ_Qubit_Line,
    MQ_Qubit_Grid,
} MQ_QubitStyle;

typedef struct MQ_QubitMeta {
    u32           id;
    MQ_QubitStyle style;
    
    // MQ_Qubit_Named
    string name;
    
    // MQ_Qubit_Line
    i32 line_x;
    
    // MQ_Qubit_Grid
    i32 grid_row;
    i32 grid_col;
    
    // Owning register; {0} if none
    string register_name;
    u32    register_index;
} MQ_QubitMeta;


/* ======================================================================
 * Gate types
 *
 * Only gates natively present in all three frameworks are listed.
 * Framework-specific gates belong in MQ_Gate_Custom.
 * ====================================================================== */

typedef enum MQ_GateType {
    // Single-qubit, no parameters
    MQ_Gate_I,
    MQ_Gate_H,
    MQ_Gate_X,
    MQ_Gate_Y,
    MQ_Gate_Z,
    MQ_Gate_S,
    MQ_Gate_Sdg,
    MQ_Gate_T,
    MQ_Gate_Tdg,
    
    // Single-qubit, parametric
    MQ_Gate_P,
    MQ_Gate_RX,
    MQ_Gate_RY,
    MQ_Gate_RZ,
    MQ_Gate_U,
    
    // Two-qubit
    MQ_Gate_SWAP,
    MQ_Gate_ISWAP,
    MQ_Gate_RZZ,
    MQ_Gate_RXX,
    MQ_Gate_RYY,
    
    // Three-qubit
    MQ_Gate_CCX,
    MQ_Gate_CSWAP,
    
    // Escape hatches
    MQ_Gate_Custom,
    MQ_Gate_Unitary,
} MQ_GateType;


/* ======================================================================
 * Quantum instructions
 *
 * The lowest-level node.  Encodes one quantum primitive.
 *
 * Unitary controls
 *   controls[] / control_states[] model coherent control on any gate.
 *   control_states[i] == 0 means control on |0>; == 1 means on |1>.
 *   This covers CX, CCX, and arbitrary controlled-U uniformly.
 *
 * Classical conditioning
 *   has_classical_cond / classical_bit / classical_val encode a
 *   hardware-level "if (cbit == val)" as in OpenQASM 2 / Qiskit c_if.
 *   For high-level feedback use MQ_Stmt_If wrapping this instruction;
 *   this field is for backends that emit it natively.
 *
 * Parameters
 *   params_symbolic == 0 : use params[] (concrete f64 values)
 *   params_symbolic == 1 : use param_exprs[] (may contain MQ_Expr_Symbol)
 *
 * Gate payload union
 *   MQ_Gate_Custom  -- gate.custom.name
 *   MQ_Gate_Unitary -- gate.unitary.matrix  (2^n x 2^n, row-major,
 *                      arena-allocated, n == qubit_count)
 *   all others      -- no extra payload
 *
 * Measure
 *   measure.has_target == 0 : discard result (trace out)
 *   measure.has_target == 1 : store in measure.clbit
 *
 * Barrier and Reset have no extra fields beyond the common qubit targets.
 * ====================================================================== */

typedef enum MQ_InstrType {
    MQ_Instr_Gate,
    MQ_Instr_Measure,
    MQ_Instr_Reset,
    MQ_Instr_Barrier,
    MQ_Instr_Delay,
} MQ_InstrType;

typedef enum MQ_TimeUnit {
    MQ_Time_Dt,
    MQ_Time_ns,
    MQ_Time_us,
    MQ_Time_ms,
    MQ_Time_s,
} MQ_TimeUnit;

typedef struct MQ_Instruction {
    MQ_InstrType type;
    
    // Qubit targets (all types except Delay)
    u32 qubits[MQ_MAX_GATE_QUBITS];
    u8  qubit_count;
    
    // Source provenance
    u32 source_line;
    u32 source_col;
    
    union {
        struct {
            // Unitary control qubits; control_states[i]: 0 = |0>, 1 = |1>
            u32 controls[MQ_MAX_CONTROLS];
            u8  control_states[MQ_MAX_CONTROLS];
            u8  control_count;
            
            // Hardware-level classical conditioning
            u32 classical_bit;
            u32 classical_val;
            b8  has_classical_cond;
            
            MQ_GateType gate;
            
            // Rotation parameters; select with params_symbolic
            u8 param_count;
            b8 params_symbolic;
            union {
                f64      params[MQ_MAX_GATE_PARAMS];
                MQ_Expr *param_exprs[MQ_MAX_GATE_PARAMS];
            };
            
            // Custom or unitary gate payload; select with gate
            union {
                string  custom_name;
                f64    *unitary_matrix;
            };
        } gate;
        
        struct {
            u32 clbit;
            b8  has_target;
        } measure;
        
        struct {
            f64         duration;
            MQ_TimeUnit unit;
        } delay;
    };
} MQ_Instruction;


/* ======================================================================
 * Registers
 *
 *   Qiskit -- QuantumRegister(n,"q") / ClassicalRegister(n,"c")
 *   Q#     -- named Qubit[] declarations
 *   Cirq   -- named LineQubit / GridQubit ranges
 * ====================================================================== */

typedef enum MQ_RegKind {
    MQ_Reg_Quantum,
    MQ_Reg_Classical,
} MQ_RegKind;

/*
 * base_id    : flat id of the first element in this register
 * qubit_meta : arena-allocated, length == size; NULL for classical
 */
struct MQ_Register {
    MQ_RegKind    kind;
    string        name;
    u32           size;
    u32           base_id;
    MQ_QubitMeta *qubit_meta;
};


/* ======================================================================
 * Statements
 *
 * Statements compose quantum instructions with classical control flow.
 * All pointer arrays (stmts, args, branches) are arena-allocated.
 *
 * Language mapping examples:
 *
 *   Qiskit  qc.measure(0,0); qc.x(0).c_if(cr,1)
 *     ->  MQ_Stmt_Instr(Measure, measure.clbit=0)
 *         MQ_Stmt_If { cond = BitRead(cr,0)==1,
 *                      branch -> MQ_Stmt_Instr(X, has_classical_cond=1) }
 *
 *   Cirq    cirq.inverse(sub_circuit)
 *     ->  MQ_Stmt_Adjoint { body = sub_circuit stmts }
 *
 *   Q#      Adjoint Op(q)
 *     ->  MQ_Stmt_Adjoint { body = MQ_Stmt_Call("Op",[q]) }
 *
 *   Q#      use q = Qubit() { ... }
 *     ->  MQ_Stmt_DeclQubit { name="q", count=1, base_id=N }
 *         ... body stmts ...
 *         (scope end is the enclosing MQ_Stmt_Block boundary)
 * ====================================================================== */

typedef enum MQ_StmtKind {
    MQ_Stmt_Block,
    MQ_Stmt_Instr,
    
    // Circuit inversion; supported by all three frameworks structurally
    MQ_Stmt_Adjoint,
    
    // Qubit allocation: Qiskit dynamic circuits / OpenQASM3 / Q# use
    MQ_Stmt_DeclQubit,
    
    // Classical declarations
    MQ_Stmt_DeclClassical,
    MQ_Stmt_DeclParam,
    
    // Classical mutation
    MQ_Stmt_Set,
    
    // Classical control flow
    MQ_Stmt_If,
    MQ_Stmt_For,
    MQ_Stmt_While,
    MQ_Stmt_Break,
    MQ_Stmt_Continue,
    MQ_Stmt_Return,
    
    // Void subroutine call; use MQ_Expr_Call for valued calls
    MQ_Stmt_Call,
    
    MQ_Stmt_Comment,
    MQ_Stmt_Pragma,
} MQ_StmtKind;

// One branch of an if / elif / else chain; cond == NULL => else branch
typedef struct MQ_IfBranch {
    MQ_Expr *cond;
    MQ_Stmt *body;
} MQ_IfBranch;

struct MQ_Stmt {
    MQ_StmtKind kind;
    
    union {
        struct {
            MQ_Stmt **stmts;
            u32       count;
        } block;
        
        MQ_Instruction instr;
        
        // MQ_Stmt_Adjoint: MQ_Stmt_Block or single instr
        MQ_Stmt *adjoint_body;
        
        struct {
            string name;
            u32    count;
            u32    base_id;
        } decl_qubit;
        
        struct {
            string   name;
            MQ_Type *type;
            MQ_Expr *init;
        } decl_classical;
        
        // decl_param.default_val == NULL => fully unbound
        struct {
            string   name;
            MQ_Expr *default_val;
        } decl_param;
        
        // set.augmented == 0 => plain assign; == 1 => lhs aug_op= rhs
        struct {
            MQ_Expr *lhs;
            MQ_Expr *rhs;
            MQ_BinOp aug_op;
            b8       augmented;
        } set;
        
        struct {
            MQ_IfBranch *branches;
            u32          count;
        } if_stmt;
        
        // for_loop.var_type == NULL => infer from iterable
        struct {
            string   var_name;
            MQ_Type *var_type;
            MQ_Expr *iterable;
            MQ_Stmt *body;
        } for_loop;
        
        struct {
            MQ_Expr *cond;
            MQ_Stmt *body;
        } while_loop;
        
        // return_val == NULL => void
        MQ_Expr *return_val;
        
        struct {
            string    callee;
            MQ_Expr **args;
            u32       arg_count;
        } call;
        
        string comment_text;
        
        struct {
            string key;
            string value;
        } pragma;
    };
    
    u32 source_line;
    u32 source_col;
};


/* ======================================================================
 *  Routines
*
 *    Qiskit - custom Gate subclass / parametrised QuantumCircuit
 *    Q#     - operation / gate definition
 *    Cirq   - Gate subclass or named sub-circuit
 *
 *    MQ_Routine_Gate      : purely unitary; no measurements or resets
 *    MQ_Routine_Operation : may measure, reset, or allocate qubits
 *
 *  Intrinsic routines (is_intrinsic == 1) have no body; back-end
 *  emitters map intrinsic_name to the appropriate framework primitive.
 *  ====================================================================== */

typedef enum MQ_RoutineKind {
    MQ_Routine_Gate,
    MQ_Routine_Operation,
} MQ_RoutineKind;

// default_val == NULL => required parameter
typedef struct MQ_FormalParam {
    string   name;
    MQ_Type *type;
    MQ_Expr *default_val;
} MQ_FormalParam;

struct MQ_Routine {
    string         name;
    MQ_RoutineKind kind;
    
    // Arena-allocated array
    MQ_FormalParam *params;
    u32             param_count;
    
    // NULL => void
    MQ_Type *return_type;
    MQ_Stmt *body;
    
    b8     is_intrinsic;
    string intrinsic_name;
    
    string doc_comment;
    u32    source_line;
};


/* ======================================================================
 * Circuit
 * ====================================================================== */

/*
 * Backend / hardware metadata preserved for accurate round-tripping.
 * All string fields are arena-allocated slices.
 * dt == 0.0 means not available.
 * coupling_map is a flat array of (ctrl, tgt) u32 pairs; length == coupling_map_len * 2.
 */
typedef struct MQ_BackendHint {
    string  backend_name;
    u32     coupling_map_len;
    u32    *coupling_map;
    string  basis_gates;
    f64     dt;
} MQ_BackendHint;

/*
 * param_names / param_defaults are parallel arrays (length == param_count).
 * param_defaults[i] == NULL => parameter i is fully unbound.
 *
 * measure_map[qubit_id] = cbit_id, or -1 if unmeasured.
 * measure_map is arena-allocated, length == total_qubits.
 *
 * global_phase is in radians; 0.0 means not set.
 */
struct MQ_Circuit {
    string name;
    
    // Register table
    MQ_Register *registers;
    u32          register_count;
    u32          total_qubits;
    u32          total_cbits;
    
    // Symbolic parameter declarations
    string  *param_names;
    MQ_Expr **param_defaults;
    u32       param_count;
    
    MQ_Stmt *body;
    
    i32 *measure_map;
    
    f64 global_phase;
    
    // NULL if absent
    MQ_BackendHint *backend_hint;
};


/* ======================================================================
 *  Program (full compilation unit)
 *  ====================================================================== */

typedef enum MQ_SourceLang {
    MQ_Lang_Unknown,
    MQ_Lang_Qiskit,
    MQ_Lang_QSharp,
    MQ_Lang_Cirq,
    MQ_Lang_OpenQASM2,
    MQ_Lang_OpenQASM3,
    MQ_Lang_MQ,
} MQ_SourceLang;

/*
 * entry_circuit : index of the entry-point circuit; default 0.
 * source_file   : {0} if absent.
 * mq_ir_version : { major, minor, patch }
 */
struct MQ_Program {
    string name;
    
    MQ_Routine **routines;
    u32          routine_count;
    
    MQ_Circuit **circuits;
    u32          circuit_count;
    u32          entry_circuit;
    
    MQ_SourceLang source_lang;
    string        source_file;
    u32           mq_ir_version[3];
};


/* ======================================================================
 *  Convenience macros
 *  ====================================================================== */

#define MQ_INSTR_IS_PARAMETRIC(i)        ((i).gate.params_symbolic)
#define MQ_INSTR_IS_CONTROLLED(i)        ((i).gate.control_count > 0)
#define MQ_INSTR_IS_CLASSICALLY_COND(i)  ((i).gate.has_classical_cond)

#define MQ_GATE_IS_SELF_ADJOINT(g) (  \
(g) == MQ_Gate_I    ||            \
(g) == MQ_Gate_H    ||            \
(g) == MQ_Gate_X    ||            \
(g) == MQ_Gate_Y    ||            \
(g) == MQ_Gate_Z    ||            \
(g) == MQ_Gate_SWAP ||            \
(g) == MQ_Gate_CCX  ||            \
(g) == MQ_Gate_CSWAP)



/* ======================================================================
 *  Construction Helpers
 *  ====================================================================== */

// Type construction
MQ_Type *mq_type_scalar(M_Arena *arena, MQ_TypeKind kind);
MQ_Type *mq_type_int(M_Arena *arena, u32 width);
MQ_Type *mq_type_array(M_Arena *arena, MQ_Type *element_type, u32 count);
MQ_Type *mq_type_reg(M_Arena *arena, MQ_TypeKind kind, u32 width);

// Expr construction
MQ_Expr *mq_expr_bool(M_Arena *arena, b8 val);
MQ_Expr *mq_expr_int(M_Arena *arena, i64 val);
MQ_Expr *mq_expr_float(M_Arena *arena, f64 val);
MQ_Expr *mq_expr_symbol(M_Arena *arena, string name);
MQ_Expr *mq_expr_var(M_Arena *arena, string name);
MQ_Expr *mq_expr_qubit_ref(M_Arena *arena, u32 qubit_id);
MQ_Expr *mq_expr_reg_index(M_Arena *arena, string reg_name, MQ_Expr *index_expr);
MQ_Expr *mq_expr_binop(M_Arena *arena, MQ_BinOp op, MQ_Expr *lhs, MQ_Expr *rhs);
MQ_Expr *mq_expr_unop(M_Arena *arena, MQ_UnOp op, MQ_Expr *operand);
MQ_Expr *mq_expr_call(M_Arena *arena, string name, MQ_Expr **args, u32 arg_count);
MQ_Expr *mq_expr_array(M_Arena *arena, MQ_Expr **elems, u32 count);
MQ_Expr *mq_expr_bit_read(M_Arena *arena, string reg_name, u32 index);

// Instruction construction
MQ_Instruction mq_instr_gate(MQ_GateType gate, u32 *qubits, u8 qubit_count);
MQ_Instruction mq_instr_gate_p(MQ_GateType gate, u32 *qubits, u8 qubit_count, f64 *params, u8 param_count);
MQ_Instruction mq_instr_gate_sym(MQ_GateType gate, u32 *qubits, u8 qubit_count, MQ_Expr **param_exprs, u8 param_count);
MQ_Instruction mq_instr_gate_custom(string name, u32 *qubits, u8 qubit_count, MQ_Expr **param_exprs, u8 param_count);
MQ_Instruction mq_instr_measure(u32 qubit, u32 clbit);
MQ_Instruction mq_instr_measure_discard(u32 qubit);
MQ_Instruction mq_instr_reset(u32 qubit);
MQ_Instruction mq_instr_barrier(u32 *qubits, u8 qubit_count);
MQ_Instruction mq_instr_delay(u32 *qubits, u8 qubit_count, f64 duration, MQ_TimeUnit unit);

// Edit Instructions IDK IF NECESSARY
void mq_instr_add_control(MQ_Instruction *instr, u32 qubit, u8 control_state);
void mq_instr_set_classical_cond(MQ_Instruction *instr, u32 classical_bit, u32 classical_val);

// Stmt construction
MQ_Stmt *mq_stmt_block(M_Arena *arena, MQ_Stmt **stmts, u32 count);
MQ_Stmt *mq_stmt_instr(M_Arena *arena, MQ_Instruction instr);
MQ_Stmt *mq_stmt_adjoint(M_Arena *arena, MQ_Stmt *body);
MQ_Stmt *mq_stmt_decl_qubit(M_Arena *arena, string name, u32 count, u32 base_id);
MQ_Stmt *mq_stmt_decl_classical(M_Arena *arena, string name, MQ_Type *type, MQ_Expr *init);
MQ_Stmt *mq_stmt_decl_param(M_Arena *arena, string name, MQ_Expr *default_val);
MQ_Stmt *mq_stmt_set(M_Arena *arena, MQ_Expr *lhs, MQ_Expr *rhs);
MQ_Stmt *mq_stmt_set_aug(M_Arena *arena, MQ_Expr *lhs, MQ_BinOp op, MQ_Expr *rhs);
MQ_Stmt *mq_stmt_if(M_Arena *arena, MQ_IfBranch *branches, u32 count);
MQ_Stmt *mq_stmt_for(M_Arena *arena, string var_name, MQ_Type *var_type, MQ_Expr *iterable, MQ_Stmt *body);
MQ_Stmt *mq_stmt_while(M_Arena *arena, MQ_Expr *cond, MQ_Stmt *body);
MQ_Stmt *mq_stmt_break(M_Arena *arena);
MQ_Stmt *mq_stmt_continue(M_Arena *arena);
MQ_Stmt *mq_stmt_return(M_Arena *arena, MQ_Expr *val);
MQ_Stmt *mq_stmt_call(M_Arena *arena, string callee, MQ_Expr **args, u32 arg_count);
MQ_Stmt *mq_stmt_comment(M_Arena *arena, string text);
MQ_Stmt *mq_stmt_pragma(M_Arena *arena, string key, string value);

// Helpers for building if branches
MQ_IfBranch mq_if_branch(MQ_Expr *cond, MQ_Stmt *body);
MQ_IfBranch mq_else_branch(MQ_Stmt *body);

// Reg construction
MQ_Register mq_register_quantum(string name, u32 size, u32 base_id, MQ_QubitMeta *meta);
MQ_Register mq_register_classical(string name, u32 size, u32 base_id);

// Routines construction
MQ_Routine *mq_routine(M_Arena *arena, string name, MQ_RoutineKind kind,
                       MQ_FormalParam *params, u32 param_count, MQ_Type *return_type, MQ_Stmt *body);
MQ_Routine *mq_routine_intrinsic(M_Arena *arena, string name, MQ_RoutineKind kind, string intrinsic_name);

MQ_FormalParam mq_formal_param(string name, MQ_Type *type, MQ_Expr *default_val);

// Circuit construction
MQ_Circuit *mq_circuit(M_Arena *arena, string name, MQ_Register *registers, u32 register_count,
                       u32 total_qubits, u32 total_cbits);
u32 mq_circuit_add_param(M_Arena *arena, MQ_Circuit *circuit, string name, MQ_Expr *default_val);

// Program construction
MQ_Program *mq_program(M_Arena *arena, string name, MQ_SourceLang lang);
void        mq_program_add_routine(M_Arena *arena, MQ_Program *prog, MQ_Routine *routine);
void        mq_program_add_circuit(M_Arena *arena, MQ_Program *prog, MQ_Circuit *circuit);




// Random stuff

void mq_program_dump(FILE *filename, MQ_Program *prog);
void mq_ir_test(M_Arena *arena, string filename);

#endif //IR_H
