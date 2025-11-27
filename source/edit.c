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

    //Header
    string_list_push(&scratch.arena, &output, str_lit("from qiskit import QuantumCircuit\n"));

    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "qc = QuantumCircuit(%d)\n", circuit->qubit_count));
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "# Comment line %d\n", 2));
    
    //body

    for(u32 i=0; i<circuit->len; i++){
        OperatorSlice* slice = &circuit->slices[i];
        u32 control_qubit = -1;
        u32 target_qubit  = -1;
        GateType target_gate;

        for(u32 q=0; q<slice->len; q++){
            Operator* op=&slice->ops[q];
            switch(op->type){

                case OpType_ControlOn:{
                    control_qubit=q;
                    break;
                }

                case OpType_Gate1:{
                    target_qubit=q;
                    target_gate=op->gate;
                    // string gates[]={str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z")};
                    // string qgate=gates[op->gate];
                    // string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.%s(%u)", qgate.str, q));
                    break;
                }

                case OpType_ControlOff:{
                    //TODO
                    break;
                }

                case OpType_Inspect:{
                    //TODO
                    break;
                }

                case OpType_Identity:{
                    string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.id(%u)\n", q));
                    break;
                }

                default:{string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "unsupported operator on qubit %u\n", q));}
            }
        }
        if(control_qubit!=-1 && target_qubit!=-1){
            string gates[]={str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z")};
            string qgate=gates[target_gate];
            string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.c%s(%u, %u)\n", qgate.str, control_qubit, target_qubit));
        }
        else if(target_qubit!=-1){
            string gates[]={str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z")};
            string qgate=gates[target_gate];
            string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.%s(%u)\n", qgate.str, target_qubit));
        }
    }
    
    
    
    
    OS_FileCreateWrite(filename, string_list_flatten(&scratch.arena, &output));
    scratch_return(&scratch);
}
