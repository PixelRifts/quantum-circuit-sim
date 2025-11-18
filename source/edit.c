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
    string_list_push(&scratch.arena, &output, str_lit("from quiskit import QuantumCircuit\n"));
    
    // 
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "qc = QuantumCircuit(%d)\n", circuit->qubit_count));
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "# Comment line %d\n", 2));
    
    
    
    
    OS_FileCreateWrite(filename, string_list_flatten(&scratch.arena, &output));
    scratch_return(&scratch);
}
