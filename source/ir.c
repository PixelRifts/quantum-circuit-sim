#include "ir.h"
#include "os/os.h"

static const char* mq_instr_type_str(MQ_InstrType t) {
    switch (t) {
        case MQ_Instr_Gate:       return "Gate";
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
                             str_from_format(arena, "call: %u, ",
                                             ins->circuit_ref));
        } else {
            string_list_push(arena, &list, str_lit("-, "));
        }
        
        /* targets */
        string_list_push(arena, &list, str_lit("q=["));
        for (u32 q = 0; q < ins->qubit_count; q++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%u", ins->qubits[q]));
            if (q + 1 < ins->qubit_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("], "));
        
        /* controls */
        string_list_push(arena, &list, str_lit("ctrl=["));
        for (u32 c = 0; c < ins->control_count; c++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%u:%u",
                                             ins->controls[c], ins->control_states[c]));
            if (c + 1 < ins->control_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("], "));
        
        /* params */
        string_list_push(arena, &list, str_lit("p=["));
        for (u32 p = 0; p < ins->param_count; p++) {
            string_list_push(arena, &list,
                             str_from_format(arena, "%f", ins->params[p]));
            if (p + 1 < ins->param_count)
                string_list_push(arena, &list, str_lit(","));
        }
        string_list_push(arena, &list, str_lit("], "));
        
        /* classical bits */
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
    
    /* ---------- Subcircuit (id = 1) ---------- */
    MQ_Circuit bell = {0};
    bell.qubit_count = 2;
    bell.classical_count = 0;
    bell.len = 2;
    bell.cap = 2;
    bell.instructions = arena_alloc(arena, sizeof(MQ_Instruction) * bell.cap);
    
    // H(q0)
    bell.instructions[0] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_H,
        .qubits = {0},
        .qubit_count = 1
    };
    
    // CX(q0 -> q1)
    bell.instructions[1] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_X,
        .qubits = {1},
        .qubit_count = 1,
        .controls = {0},
        .control_states = {1},
        .control_count = 1
    };
    
    
    /* ---------- Main Circuit (id = 0) ---------- */
    MQ_Circuit c = {0};
    
    c.qubit_count = 4;
    c.classical_count = 3;
    c.len = 10;
    c.cap = 10;
    c.instructions = arena_alloc(arena, sizeof(MQ_Instruction) * c.cap);
    
    // H(q0)
    c.instructions[0] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_H,
        .qubits = {0},
        .qubit_count = 1
    };
    
    // RX(0.5) q1
    c.instructions[1] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_RX,
        .qubits = {1},
        .qubit_count = 1,
        .params = {0.5},
        .param_count = 1
    };
    
    // Controlled Z (q0 -> q1)
    c.instructions[2] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_Z,
        .qubits = {1},
        .qubit_count = 1,
        .controls = {0},
        .control_states = {1},
        .control_count = 1
    };
    
    // SWAP(q2,q3)
    c.instructions[3] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_SWAP,
        .qubits = {2,3},
        .qubit_count = 2
    };
    
    // ISWAP(q1,q2)
    c.instructions[4] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_ISWAP,
        .qubits = {1,2},
        .qubit_count = 2
    };
    
    // RZZ(1.2) between q0 and q3
    c.instructions[5] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_RZZ,
        .qubits = {0,3},
        .qubit_count = 2,
        .params = {1.2},
        .param_count = 1
    };
    
    // X(q2) with negative control q1==0
    c.instructions[6] = (MQ_Instruction){
        .type = MQ_Instr_Gate,
        .gate = MQ_Gate_X,
        .qubits = {2},
        .qubit_count = 1,
        .controls = {1},
        .control_states = {0},
        .control_count = 1
    };
    
    // Call subcircuit (Bell pair generator)
    c.instructions[7] = (MQ_Instruction){
        .type = MQ_Instr_Call,
        .circuit_ref = 1
    };
    
    // Measure q2 -> c0
    c.instructions[8] = (MQ_Instruction){
        .type = MQ_Instr_Measure,
        .qubits = {2},
        .qubit_count = 1,
        .classical_bits = {0},
        .classical_count = 1
    };
    
    // Measure q3 -> c1
    c.instructions[9] = (MQ_Instruction){
        .type = MQ_Instr_Measure,
        .qubits = {3},
        .qubit_count = 1,
        .classical_bits = {1},
        .classical_count = 1
    };
    
    
    /* ---------- Dump ---------- */
    string_list t = {0};
    string_list_push(arena, &t, str_lit("MAIN CIRCUIT\n"));
    string dump_main = mq_ir_to_string(arena, &c);
    string_list_push(arena, &t, dump_main);
    string_list_push(arena, &t, str_lit("\n"));
    string dump_sub  = mq_ir_to_string(arena, &bell);
    string_list_push(arena, &t, dump_sub);
    string_list_push(arena, &t, str_lit("\n"));
    
    printf("MAIN CIRCUIT\n%.*s\n", str_expand(dump_main));
    printf("SUBCIRCUIT 1 (Bell)\n%.*s\n", str_expand(dump_sub));
    
    OS_FileCreateWrite(str_lit("test.mq"), string_list_flatten(arena, &t));
}