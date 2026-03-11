#include "ir.h"

static const char* mq_instr_type_str(MQ_InstrType t) {
    switch (t) {
        case MQ_Instr_Gate:       return "Gate";
        case MQ_Instr_ControlOn:  return "ControlOn";
        case MQ_Instr_ControlOff: return "ControlOff";
        case MQ_Instr_Measure:    return "Measure";
        case MQ_Instr_Reset:      return "Reset";
        case MQ_Instr_Barrier:    return "Barrier";
        case MQ_Instr_Delay:      return "Delay";
        case MQ_Instr_Call:       return "Call";
    }
    return "Unknown";
}

static const char* mq_gate_type_str(MQ_GateType g) {
    switch (g) {
        case MQ_Gate_I:     return "I";
        case MQ_Gate_H:     return "H";
        case MQ_Gate_X:     return "X";
        case MQ_Gate_Y:     return "Y";
        case MQ_Gate_Z:     return "Z";
        case MQ_Gate_S:     return "S";
        case MQ_Gate_T:     return "T";
        case MQ_Gate_RX:    return "RX";
        case MQ_Gate_RY:    return "RY";
        case MQ_Gate_RZ:    return "RZ";
        case MQ_Gate_SWAP:  return "SWAP";
        case MQ_Gate_ISWAP: return "ISWAP";
        case MQ_Gate_RZZ:   return "RZZ";
        case MQ_Gate_U:     return "U";
    }
    return "Unknown";
}

string mq_ir_to_string(M_Arena* arena, MQ_Circuit* c) {
    string_list list = {0};
    
    string_list_push(arena, &list,
                     str_from_format(arena,
                                     "Circuit(qubits=%u, classical=%u)\n",
                                     c->qubit_count, c->classical_count));
    
    for (u32 i = 0; i < c->len; i++) {
        MQ_Instruction* ins = &c->instructions[i];
        
        string_list_push(arena, &list,
                         str_from_format(arena, "%u: (%s, ",
                                         i, mq_instr_type_str(ins->type)));
        
        if (ins->type == MQ_Instr_Gate) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%s, ",
                                             mq_gate_type_str(ins->gate)));
        } else if (ins->type == MQ_Instr_Call) {
            string_list_push(arena, &list,
                             str_from_format(arena, "call:%u, ",
                                             ins->circuit_ref));
        } else {
            string_list_push(arena, &list, str_lit("-, "));
        }
        
        string_list_push(arena, &list, str_lit("q=["));
        for (u32 q = 0; q < ins->qubit_count; q++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%u", ins->qubits[q]));
            if (q + 1 < ins->qubit_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("], "));
        
        string_list_push(arena, &list, str_lit("p=["));
        for (u32 p = 0; p < ins->param_count; p++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%f", ins->params[p]));
            if (p + 1 < ins->param_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("], "));
        
        string_list_push(arena, &list, str_lit("c=["));
        for (u32 b = 0; b < ins->classical_count; b++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%u", ins->classical_bits[b]));
            if (b + 1 < ins->classical_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("])\n"));
    }
    
    return string_list_flatten(arena, &list);
}

void example_ir_dump(M_Arena* arena) {
    MQ_Circuit c = {0};
    
    c.qubit_count = 2;
    c.classical_count = 1;
    c.len = 4;
    c.cap = 4;
    c.instructions = arena_alloc(arena, sizeof(MQ_Instruction) * c.cap);
    
    // H(q0)
    c.instructions[0] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_H,
        .qubits = {0},
        .qubit_count = 1,
    };
    
    // ControlOn(q0)
    c.instructions[1] = (MQ_Instruction){
        .type = MQ_Instr_ControlOn,
        .qubits = {0},
        .qubit_count = 1,
    };
    
    // X(q1)  -> controlled by q0
    c.instructions[2] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_X,
        .qubits = {1},
        .qubit_count = 1,
    };
    
    // Measure(q1 -> c0)
    c.instructions[3] = (MQ_Instruction){
        .type = MQ_Instr_Measure,
        .qubits = {1},
        .qubit_count = 1,
        .classical_bits = {0},
        .classical_count = 1,
    };
    
    string dump = mq_ir_to_string(arena, &c);
    
    printf("%.*s\n", str_expand(dump));
}