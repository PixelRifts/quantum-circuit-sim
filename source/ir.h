/* date = March 11th 2026 11:33 pm */

#ifndef IR_H
#define IR_H

#include "defines.h"
#include "base/str.h"

typedef enum MQ_InstrType {
    MQ_Instr_Gate,
    MQ_Instr_ControlOn,
    MQ_Instr_ControlOff,
    MQ_Instr_Measure,
    MQ_Instr_Reset,
    MQ_Instr_Barrier,
    MQ_Instr_Delay,
    MQ_Instr_Call,
} MQ_InstrType;

typedef enum MQ_GateType {
    MQ_Gate_I,
    MQ_Gate_H,
    MQ_Gate_X,
    MQ_Gate_Y,
    MQ_Gate_Z,
    MQ_Gate_S,
    MQ_Gate_T,
    MQ_Gate_RX,
    MQ_Gate_RY,
    MQ_Gate_RZ,
    MQ_Gate_SWAP,
    MQ_Gate_ISWAP,
    MQ_Gate_RZZ,
    MQ_Gate_U
} MQ_GateType;

typedef struct MQ_Instruction MQ_Instruction;
struct MQ_Instruction {
    MQ_InstrType type;
    
    u32 qubits[3];
    u8 qubit_count;
    
    u32 classical_bits[2];
    u8 classical_count;
    
    f64 params[3];
    u8 param_count;
    
    union {
        MQ_GateType gate;
        u32 circuit_ref;
    };
};

typedef struct MQ_Circuit MQ_Circuit;
struct MQ_Circuit {
    u32 qubit_count;
    u32 classical_count;
    
    u32 len;
    u32 cap;
    MQ_Instruction* instructions;
};

typedef enum FlowType {
    MQ_Flow_If,
    MQ_Flow_For,
    MQ_Flow_While
} MQ_FlowType;

typedef struct MQ_FlowOp MQ_FlowOp;
struct MQ_FlowOp {
    MQ_FlowType type;
    u32 classical_bit;
    MQ_Circuit body;
};

string mq_ir_to_string(M_Arena* arena, MQ_Circuit* c);
void example_ir_dump(M_Arena* arena);

#endif //IR_H
