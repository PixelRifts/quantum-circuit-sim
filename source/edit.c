#include "edit.h"

void CircuitEmitAsPython(Circuit* circuit, string filename) {
    M_Scratch scratch = scratch_get();
    // List of lines to output
    string_list output = {0};
    
    
    // Add lines like this.
    // Check str.h to see how to work with strings. try not to use char*s.
    
    // Any function that wants an arena, use &scratch.arena
    // two functions you'll probably need:
    //   str_lit()         : see below, makes a "string" from a string literal
    //   str_from_format() : like printf, but makes a "string" instead of outputting to console
    //                       needs to allocate space so will require an arena parameter
    // Maybe output a few comments too
    string_list_push(&scratch.arena, &output, str_lit("from qiskit import QuantumCircuit\n"));
    
    // 
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "qc = QuantumCircuit(%d)\n", circuit->qubit_count));
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "# Comment line %d\n", 2));
    
    
    
    
    OS_FileCreateWrite(filename, string_list_flatten(&scratch.arena, &output));
    scratch_return(&scratch);
}


//- Helpers

void CircuitSetQubitCount(Circuit* circuit, int new_count) {
    u32 old_count = circuit->qubit_count;
    
    circuit->qubit_count = new_count;
    for (int i = 0; i < circuit->len; i++) {
        OperatorSlice* sl = &circuit->slices[i];
        
        sl->cap = Max(sl->cap, new_count);
        sl->len = new_count;
        sl->ops = realloc(sl->ops, new_count * sizeof(Operator));
        
        if (new_count > old_count) {
            for (int k = old_count; k < new_count; k++) {
                sl->ops[k].type = OpType_Identity;
            }
        }
    }
}

OperatorSlice* CircuitInsertTimesliceAt(Circuit* circuit, u32 index) {
    if (circuit->len + 1 > circuit->cap) {
        u32 new_cap = circuit->cap == 0 ? 8 : circuit->cap * 2;
        circuit->slices = realloc(circuit->slices, new_cap * sizeof(OperatorSlice));
        circuit->cap = new_cap;
    }
    
    u32 count = circuit->len - index;
    memmove(&circuit->slices[index+1], &circuit->slices[index],
            count * sizeof(OperatorSlice));
    circuit->slices[index].len = circuit->qubit_count;
    circuit->slices[index].cap = circuit->qubit_count;
    circuit->slices[index].ops = calloc(circuit->qubit_count, sizeof(Operator));
    
    circuit->len += 1;
    return &circuit->slices[index];
}

OperatorSlice* CircuitPushTimeslice(Circuit* circuit) {
    return CircuitInsertTimesliceAt(circuit, circuit->len);
}

void CircuitFree(Circuit* circuit) {
    for (int i = 0; i < circuit->len; i++) {
        circuit->slices[i].len = 0;
        circuit->slices[i].cap = 0;
        free(circuit->slices[i].ops);
    }
    circuit->qubit_count = 0;
    circuit->len = 0;
    circuit->cap = 0;
    free(circuit->slices);
}

//- Editor

EditContext* EditorCreate(M_Arena* arena) {
    EditContext* ctx = arena_alloc_zero(arena, sizeof(EditContext));
    
    CircuitSetQubitCount(&ctx->circuit, 4);
    CircuitPushTimeslice(&ctx->circuit);
    CircuitPushTimeslice(&ctx->circuit);
    
    
    
    return ctx;
}

void EditorFree(EditContext* ctx) {
    CircuitFree(&ctx->circuit);
}
