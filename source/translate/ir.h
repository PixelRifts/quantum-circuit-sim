/* date = March 11th 2026 11:33 pm */

#ifndef IR_H
#define IR_H

#include "defines.h"
#include "base/str.h"

//- IR Types
typedef struct MQ_Type      MQ_Type;
typedef struct MQ_Expr      MQ_Expr;
typedef struct MQ_Stmt      MQ_Stmt;
typedef struct MQ_Routine   MQ_Routine;
typedef struct MQ_Circuit   MQ_Circuit;
typedef struct MQ_Program   MQ_Program;
typedef struct MQ_Register  MQ_Register;

//- Types

typedef enum MQ_TypeKind {
    MQ_Type_Void,
    
    // Scalars
    MQ_Type_Bool,
    MQ_Type_Int,
    MQ_Type_Float,
    MQ_Type_Complex,
    MQ_Type_Angle,
    MQ_Type_Result,
    
    // Parameters
    MQ_Type_Param,
    
    // Quantum handles
    MQ_Type_Qubit,
    MQ_Type_BitReg,
    MQ_Type_QubitReg,
    
    // Compound (classical)
    MQ_Type_Bit,
    MQ_Type_Array,
    MQ_Type_Tuple,
    MQ_Type_Range,
    MQ_Type_Callable,
} MQ_TypeKind;


struct MQ_Type {
    MQ_TypeKind  kind;
    u32          width;
    MQ_Type     *element_type;
    
    // Tuple
    MQ_Type    **members;
    u32          member_count;
    
    // Callable
    MQ_Type     *callable_input;
    MQ_Type     *callable_output;
};

//- Expression

typedef enum MQ_BinOp {
    // Arithmetic
    MQ_BinOp_Add,  MQ_BinOp_Sub, MQ_BinOp_Mul,
    MQ_BinOp_Div,  MQ_BinOp_Mod, MQ_BinOp_Pow,
    // Bitwise
    MQ_BinOp_And,  MQ_BinOp_Or,  MQ_BinOp_Xor,
    MQ_BinOp_Shl,  MQ_BinOp_Shr,
    // Comparison
    MQ_BinOp_Eq,   MQ_BinOp_Ne,
    MQ_BinOp_Lt,   MQ_BinOp_Le,
    MQ_BinOp_Gt,   MQ_BinOp_Ge,
    // Logical
    MQ_BinOp_LogAnd, MQ_BinOp_LogOr,
} MQ_BinOp;

typedef enum MQ_UnOp {
    MQ_UnOp_Neg,
    MQ_UnOp_Not,
    MQ_UnOp_BitNot,
    
    // @check may not want these
    MQ_UnOp_Sin,   MQ_UnOp_Cos,   MQ_UnOp_Tan,
    MQ_UnOp_Asin,  MQ_UnOp_Acos,  MQ_UnOp_Atan,
    MQ_UnOp_Sqrt,  MQ_UnOp_Exp,   MQ_UnOp_Log,
    MQ_UnOp_Abs,
} MQ_UnOp;

typedef enum MQ_ExprKind {
    // Literals
    MQ_Expr_BoolLit,
    MQ_Expr_IntLit,
    MQ_Expr_FloatLit,
    MQ_Expr_ComplexLit,
    MQ_Expr_Symbol,
    
    // References
    MQ_Expr_Var,
    MQ_Expr_QubitRef,
    MQ_Expr_RegIndex,
    MQ_Expr_TupleIndex,
    
    // Compound
    MQ_Expr_BinOp,
    MQ_Expr_UnOp,
    MQ_Expr_Ternary,
    MQ_Expr_Call,
    MQ_Expr_Cast,
    MQ_Expr_Tuple,
    MQ_Expr_Array,
    
    // @check Q#-specific
    // MQ_Expr_Range,
    // MQ_Expr_ResultEq,
    
    // Quantum-result reads
    MQ_Expr_BitRead,
} MQ_ExprKind;

struct MQ_Expr {
    MQ_ExprKind  kind;
    MQ_Type     *type;
    
    union {
        struct {
            b8    bool_val;
            i64   int_val;
            f64   float_val;
            f64   re, im;
        } lit;
        
        // @name change to Symbol reference rather than
        const char *name;
        u32 qubit_id;
        
        // Register index
        struct {
            // @name
            const char *name;
            MQ_Expr    *index_expr;
        } reg;
        
        // Tuple index
        struct {
            MQ_Expr *expr;
            u32      pos;
        } tuple;
        
        // Binary operation
        struct {
            MQ_BinOp  op;
            MQ_Expr  *lhs;
            MQ_Expr  *rhs;
        } bin;
        
        // Unary operation
        struct {
            MQ_UnOp  op;
            MQ_Expr *operand;
        } un;
        
        // Ternary
        struct {
            MQ_Expr *cond;
            MQ_Expr *then_expr;
            MQ_Expr *else_expr;
        } tern;
        
        // Function call / array literal / tuple construction
        struct {
            // @name
            const char  *name;
            MQ_Expr    **args;
            u32          arg_count;
        } call;
        
        // Cast
        struct {
            MQ_Type *target_type;
            MQ_Expr *operand;
        } cast;
        
        /*
         * Q# Range
        struct {
            MQ_Expr *start;
            MQ_Expr *step;
            MQ_Expr *end;
        } range;
         */
        
        // Q# result equality  (r == Zero / r == One)
        /*
struct {
            MQ_Expr *expr;
            b8     zero;
        } result;
*/
        
        // Classical bit read
        struct {
            // @name
            const char *reg_name;
            u32         bit_index;
        } bit;
    };
};

//- Gates

typedef enum MQ_QubitStyle {
    MQ_Qubit_Flat,      /* sequential id (default / Qiskit)                 */
    MQ_Qubit_Named,     /* string name (Q# variable / Cirq NamedQubit)      */
    MQ_Qubit_Line,      /* Cirq LineQubit(x)                                */
    MQ_Qubit_Grid,      /* Cirq GridQubit(row, col)                         */
} MQ_QubitStyle;

typedef struct MQ_QubitMeta {
    u32           id;               /* canonical flat index                  */
    MQ_QubitStyle style;
    // @name
    const char   *name;             /* MQ_Qubit_Named                        */
    i32           line_x;           /* MQ_Qubit_Line                         */
    i32           grid_row;         /* MQ_Qubit_Grid                         */
    i32           grid_col;         /* MQ_Qubit_Grid                         */
    // @name
    const char   *register_name;    /* owning register name                  */
    u32           register_index;   /* index within that register            */
} MQ_QubitMeta;

typedef enum MQ_GateType {
    // Single-qubit, no parameters 
    MQ_Gate_I,
    MQ_Gate_H,
    MQ_Gate_X,
    MQ_Gate_Y,
    MQ_Gate_Z,
    MQ_Gate_S,
    MQ_Gate_T,
    
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
    MQ_Gate_XX_Plus_YY,
    
    MQ_Gate_Custom,
    MQ_Gate_Unitary,
} MQ_GateType;

typedef enum MQ_GateMod {
    MQ_GateMod_None    = 0,
    MQ_GateMod_Adjoint = 1 << 0,
    MQ_GateMod_Pow     = 1 << 1,
} MQ_GateMod;

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
    
    u32  qubits[3];
    u8   qubit_count;
    
    u32  controls[8];
    u8   control_states[8];
    u8   control_count;
    
    u32  classical_bits[2];
    u8   classical_count;
    u32  classical_val;
    
    MQ_Expr *params[3];
    u8       param_count;
    
    union {
        struct {
            MQ_GateType  gate;
            MQ_GateMod   gate_mods;
            const char*  custom_name;
            f64*         unitary_matrix;
        } gate;
        
        struct {
            u32  meas_clbit;
            b8   meas_has_target;
        } measure;
        
        struct {
            f64         delay_duration;
            MQ_TimeUnit delay_unit;
        } delay;
    };
    
    // Source locs
    u32 source_line;
    u32 source_col;
} MQ_Instruction;



#endif //IR_H
