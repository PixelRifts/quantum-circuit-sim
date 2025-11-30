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
    
    
    string_list_push(&scratch.arena, &output, str_lit("\n"));
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "qc = QuantumCircuit(%d)\n", circuit->qubit_count));
    
    //body
    
    for(u32 i=0; i<circuit->len; i++){
        OperatorSlice* slice = &circuit->slices[i];
        u32 control_qubit = -1;
        u32 target_qubit  = -1;
        GateType target_gate;
        
        for(u32 q=0; q<slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            switch(op->type) {
                case OpType_ControlOn: {
                    control_qubit = q;
                    break;
                }
                
                case OpType_Gate1: {
                    target_qubit = q;
                    target_gate = op->gate;
                    // string gates[]={str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z")};
                    // string qgate=gates[op->gate];
                    // string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.%s(%u)", qgate.str, q));
                    break;
                }
                
                case OpType_ControlOff: {
                    //TODO
                    break;
                }
                
                case OpType_Inspect: {
                    //TODO
                    break;
                }
                
                case OpType_Identity: {
                    string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.id(%u)\n", q));
                    break;
                }
                
                default:{string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "unsupported operator on qubit %u\n", q));}
            }
        }
        
        if (control_qubit != -1 && target_qubit != -1) {
            string gates[] = {str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z")};
            string qgate = gates[target_gate];
            string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.c%s(%u, %u)\n", qgate.str, control_qubit, target_qubit));
        }
        else if (target_qubit != -1) {
            string gates[] = { str_lit("h"), str_lit("x"), str_lit("y"), str_lit("z") };
            string qgate = gates[target_gate];
            string_list_push(&scratch.arena, &output, str_from_format(&scratch.arena, "qc.%s(%u)\n", qgate.str, target_qubit));
        }
    }
    
    
    
    
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



static Rift_UIBoxStyle g_panel_style = (Rift_UIBoxStyle) {
    .color        = (vec4){0.10f, 0.10f, 0.12f, 1.0f},
    .hot_color    = (vec4){0.15f, 0.15f, 0.17f, 1.0f},
    .active_color = (vec4){0.06f, 0.06f, 0.12f, 1.0f},
    
    .border_color        = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_hot_color    = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_active_color = (vec4){0.06f, 0.06f, 0.15f, 1.0f},
    
    .text_color = (vec4){0.8f, 0.8f, 0.94f, 1.0f},
    .font = 0,
    .font_size = 20,
    
    .rounding = 0,
    .softness = 0.1,
    .edge_size = 0,
};

static Rift_UIBoxStyle g_block_style = (Rift_UIBoxStyle) {
    .color        = (vec4){0.10f, 0.10f, 0.12f, 1.0f},
    .hot_color    = (vec4){0.15f, 0.15f, 0.17f, 1.0f},
    .active_color = (vec4){0.06f, 0.06f, 0.12f, 1.0f},
    
    .border_color        = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_hot_color    = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_active_color = (vec4){0.06f, 0.06f, 0.15f, 1.0f},
    
    .text_color = (vec4){0.8f, 0.8f, 0.94f, 1.0f},
    .font = 0,
    .font_size = 20,
    
    .rounding = 2,
    .softness = 0.1,
    .edge_size = 1,
};

static Rift_UIBoxStyle g_line_style = (Rift_UIBoxStyle) {
    .color        = (vec4){0.80f, 0.80f, 0.82f, 1.0f},
    .hot_color    = (vec4){0.15f, 0.15f, 0.17f, 1.0f},
    .active_color = (vec4){0.06f, 0.06f, 0.12f, 1.0f},
    
    .border_color        = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_hot_color    = (vec4){0.20f, 0.40f, 0.52f, 1.0f},
    .border_active_color = (vec4){0.06f, 0.06f, 0.15f, 1.0f},
    
    .text_color = (vec4){0.8f, 0.8f, 0.94f, 1.0f},
    .font = 0,
    .font_size = 20,
    
    .rounding = 0,
    .softness = 0.1,
    .edge_size = 0,
};

#define BLOCK_SIZE       40.0f
#define QUBIT_SEPARATION (BLOCK_SIZE*1.5f)
EditContext* EditorCreate(M_Arena* arena, Rift_UIContext* ui, Rift_UIBox* content) {
    EditContext* ctx = arena_alloc_zero(arena, sizeof(EditContext));
    arena_init(&ctx->arena);
    
    CircuitSetQubitCount(&ctx->circuit, 4);
    CircuitPushTimeslice(&ctx->circuit);
    CircuitPushTimeslice(&ctx->circuit);
    
    ctx->ui = ui;
    ctx->content = content;
    ctx->content->layout_axis = UIAxis_Y;
    
    //- Top bar
    
    ctx->top_bar = Rift_UIBoxCreate(ctx->ui, ctx->content,
                                    Rift_UISizePct(1.0f), Rift_UISizePixels(BLOCK_SIZE),
                                    &g_panel_style,
                                    UIBoxFlag_DrawBack);
    ctx->top_bar->layout_axis = UIAxis_X;
    
    for (int i = 0; i < BlockType_Max; i++) {
        ctx->pick_gates[i] = Rift_UIBoxCreate(ctx->ui, ctx->top_bar,
                                              Rift_UISizePixels(BLOCK_SIZE), Rift_UISizePixels(BLOCK_SIZE),
                                              &g_block_style,
                                              UIBoxFlag_DrawBack | UIBoxFlag_DrawBorder |
                                              UIBoxFlag_DrawName | UIBoxFlag_Interactive);
        ctx->pick_gates[i]->text = block_type_names[i];
    }
    
    Rift_UIBox* the_rest = Rift_UIBoxCreate(ctx->ui, ctx->top_bar,
                                            Rift_UISizePixels(-(BLOCK_SIZE*BlockType_Max)), Rift_UISizePct(1.0f),
                                            &g_block_style,
                                            0);
    the_rest->layout_axis = UIAxis_X;
    the_rest->alignment = UIAlign_End;
    
    Rift_UIBox* qubit_control = Rift_UIBoxCreate(ctx->ui, the_rest,
                                                 Rift_UISizePixels(BLOCK_SIZE * 0.5f), Rift_UISizePct(1.0f),
                                                 &g_block_style,
                                                 0);
    qubit_control->layout_axis = UIAxis_Y;
    
    ctx->qubit_add = Rift_UIBoxCreate(ctx->ui, qubit_control,
                                      Rift_UISizePct(1.0f), Rift_UISizePixels(BLOCK_SIZE * 0.5f),
                                      &g_block_style,
                                      UIBoxFlag_DrawBack | UIBoxFlag_DrawBorder |
                                      UIBoxFlag_DrawName | UIBoxFlag_Interactive);
    ctx->qubit_add->text = str_lit("+");
    ctx->qubit_remove = Rift_UIBoxCreate(ctx->ui, qubit_control,
                                         Rift_UISizePct(1.0f), Rift_UISizePixels(BLOCK_SIZE * 0.5f),
                                         &g_block_style,
                                         UIBoxFlag_DrawBack | UIBoxFlag_DrawBorder |
                                         UIBoxFlag_DrawName | UIBoxFlag_Interactive);
    ctx->qubit_remove->text = str_lit("_");
    
    //- Qubits
    
    ctx->qubit_area = Rift_UIBoxCreate(ctx->ui, ctx->content,
                                       Rift_UISizePct(1.0f), Rift_UISizePixels(-BLOCK_SIZE),
                                       &g_panel_style,
                                       0);
    ctx->qubit_area->layout_axis = UIAxis_X;
    
    ctx->qubit_labels_area = Rift_UIBoxCreate(ctx->ui, ctx->qubit_area,
                                              Rift_UISizePixels(40.0f), Rift_UISizePct(1.0f),
                                              &g_panel_style,
                                              0);
    ctx->qubit_labels_area->layout_axis = UIAxis_Y;
    
    ctx->qubit_lines_area = Rift_UIBoxCreate(ctx->ui, ctx->qubit_area,
                                             Rift_UISizePixels(-45.0f), Rift_UISizePct(1.0f),
                                             &g_panel_style,
                                             0);
    ctx->qubit_lines_area->layout_axis = UIAxis_Y;
    
    
    
    
    Rift_UIBoxCreate(ctx->ui, ctx->qubit_labels_area,
                     Rift_UISizePct(1.0f), Rift_UISizePixels(QUBIT_SEPARATION * 0.5f),
                     &g_panel_style,
                     0);
    for (int i = 0; i < 4; i++) {
        ctx->label_paddings[i] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_lines_area,
                                                  Rift_UISizePct(1.0f), Rift_UISizePixels(QUBIT_SEPARATION),
                                                  &g_panel_style,
                                                  0);
        ctx->lines[i] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_lines_area,
                                         Rift_UISizePct(1.0f), Rift_UISizePixels(2.0f),
                                         &g_line_style,
                                         UIBoxFlag_DrawBack);
        
        
        ctx->labels[i] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_labels_area,
                                          Rift_UISizePct(1.0f), Rift_UISizePixels(QUBIT_SEPARATION+2.0f),
                                          &g_line_style,
                                          UIBoxFlag_DrawName);
        ctx->labels[i]->text = str_from_format(&ctx->arena, "q%d", i);
    }
    
    return ctx;
}

void EditorUpdate(EditContext* ctx, f32 delta) {
    if (Rift_UIBoxSignal(ctx->ui, ctx->qubit_add).pressed) {
        u32 new_count = Min(ctx->circuit.qubit_count + 1, 8);
        if (new_count != ctx->circuit.qubit_count) {
            ctx->label_paddings[new_count-1] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_lines_area,
                                                                Rift_UISizePct(1.0f), Rift_UISizePixels(QUBIT_SEPARATION),
                                                                &g_panel_style,
                                                                0);
            ctx->lines[new_count-1] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_lines_area,
                                                       Rift_UISizePct(1.0f), Rift_UISizePixels(2.0f),
                                                       &g_line_style,
                                                       UIBoxFlag_DrawBack);
            
            
            ctx->labels[new_count-1] = Rift_UIBoxCreate(ctx->ui, ctx->qubit_labels_area,
                                                        Rift_UISizePct(1.0f), Rift_UISizePixels(QUBIT_SEPARATION+2.0f),
                                                        &g_line_style,
                                                        UIBoxFlag_DrawName);
            ctx->labels[new_count-1]->text = str_from_format(&ctx->arena, "q%d", new_count-1);
        }
        CircuitSetQubitCount(&ctx->circuit, new_count);
    }
    
    if (Rift_UIBoxSignal(ctx->ui, ctx->qubit_remove).pressed) {
        u32 new_count = Max(ctx->circuit.qubit_count - 1, 1);
        if (new_count != ctx->circuit.qubit_count) {
            Rift_UIBoxDestroy(ctx->ui, ctx->label_paddings[new_count]);
            Rift_UIBoxDestroy(ctx->ui, ctx->labels[new_count]);
            Rift_UIBoxDestroy(ctx->ui, ctx->lines[new_count]);
        }
        CircuitSetQubitCount(&ctx->circuit, new_count);
    }
    
}

void EditorFree(EditContext* ctx) {
    CircuitFree(&ctx->circuit);
    arena_free(&ctx->arena);
}
