#include "edit.h"

#include <stdlib.h>

#include "os/input.h"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#include "client/tri_render.h"

#include "ir.h"

static const char* get_qiskit_gate_class(GateType gate) {
    switch (gate) {
        case Gate_H:  return "HGate";
        case Gate_PX: return "XGate";
        case Gate_PY: return "YGate";
        case Gate_PZ: return "ZGate";
        case Gate_S:  return "SGate";
        case Gate_T:  return "TGate";
        default:      return "XGate";
    }
}

// Helper to map Enum to simple function calls (for non-controlled ops)
static const char* get_qiskit_func_name(GateType gate) {
    switch (gate) {
        case Gate_H:  return "h";
        case Gate_PX: return "x";
        case Gate_PY: return "y";
        case Gate_PZ: return "z";
        case Gate_S:  return "s";
        case Gate_T:  return "t";
        default:      return "x";
    }
}

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
    string_list_push(&scratch.arena, &output, str_lit("import pprint\n"));
    string_list_push(&scratch.arena, &output, str_lit("from qiskit import QuantumCircuit, transpile\n"));
    string_list_push(&scratch.arena, &output, str_lit("from qiskit_aer import AerSimulator\n"));
    string_list_push(&scratch.arena, &output, str_lit("from qiskit.circuit.library import XGate, YGate, ZGate, HGate, SGate, TGate\n"));
    
    
    string_list_push(&scratch.arena, &output, str_lit("\n"));
    string_list_push(&scratch.arena, &output,
                     str_from_format(&scratch.arena, "qc = QuantumCircuit(%d)\n", circuit->qubit_count));
    
    for(u32 i = 0; i < circuit->len; i++) {
        OperatorSlice* slice = &circuit->slices[i];
        
        // 1. Analyze the slice to see if it contains controls
        b8 is_controlled_slice = false;
        u32 target_qubit_index = -1;
        GateType target_gate_type = 0;
        
        // We need buffers to store control indices and states ('0' or '1')
        // Assuming a max reasonable number of controls, or allocate based on qubit_count
        u32 control_indices[64]; 
        char control_states[65]; // +1 for null terminator
        u32 control_count = 0;
        
        for(u32 q = 0; q < slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            if (op->type == OpType_ControlOn || op->type == OpType_ControlOff) {
                is_controlled_slice = true;
                if (control_count < 64) {
                    control_indices[control_count] = q;
                    // OpType_ControlOn -> '1', OpType_ControlOff -> '0'
                    control_states[control_count] = (op->type == OpType_ControlOn) ? '1' : '0';
                    control_count++;
                }
            } else if (op->type == OpType_Gate1) {
                target_qubit_index = q;
                target_gate_type = op->gate;
            }
        }
        control_states[control_count] = '\0'; // Null terminate the state string
        
        // 2. Emission Logic
        if (is_controlled_slice) {
            // --- CONTROLLED OPERATION ---
            if (target_qubit_index != -1 && control_count > 0) {
                // We use the general .control() syntax in Qiskit to handle 
                // arbitrary numbers of controls and "0" states.
                // Syntax: qc.append(HGate().control(num_ctrl, ctrl_state="01"), [c1, c2, target])
                
                const char* gate_class = get_qiskit_gate_class(target_gate_type);
                
                // Start the formatting
                // Note: Depending on your string library, you might need to build the list string dynamically
                // Here I assume standard format strings can handle it or we build it step by step.
                
                string_list_push(&scratch.arena, &output, 
                                 str_from_format(&scratch.arena, "qc.append(%s().control(%u, ctrl_state='%s'), [", 
                                                 gate_class, control_count, control_states));
                
                // Add control indices
                for(u32 c = 0; c < control_count; c++) {
                    string_list_push(&scratch.arena, &output, 
                                     str_from_format(&scratch.arena, "%u, ", control_indices[c]));
                }
                
                // Add target index and close
                string_list_push(&scratch.arena, &output, 
                                 str_from_format(&scratch.arena, "%u])\n", target_qubit_index));
            }
            else {
                // Handle error: Controlled slice with no target or lost controls
                string_list_push(&scratch.arena, &output, 
                                 str_lit("# Error: Malformed controlled slice\n"));
            }
        } 
        else {
            // --- PARALLEL SINGLE GATES ---
            // If no controls exist, we might have multiple gates in one slice (e.g., H on q0, X on q1)
            for(u32 q = 0; q < slice->len; q++) {
                Operator* op = &slice->ops[q];
                
                switch(op->type) {
                    case OpType_Gate1: {
                        const char* func = get_qiskit_func_name(op->gate);
                        string_list_push(&scratch.arena, &output, 
                                         str_from_format(&scratch.arena, "qc.%s(%u)\n", func, q));
                    } break;
                    
                    case OpType_Inspect: {
                        // Mapping Inspect_Chance or BlochSphere to a comment or save_state
                        // For aer, save_state_vector is common, or just measure
                        if (op->inspect == Inspect_Chance) {
                            string_list_push(&scratch.arena, &output, 
                                             str_from_format(&scratch.arena, "qc.measure(%u, %u)\n", q, q)); // Simple measure mapping
                        }
                    } break;
                    
                    case OpType_Identity:
                    default:
                    break;
                }
            }
        }
    }
    
    string_list_push(&scratch.arena, &output,
                     str_lit("\nqc.measure_all()\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("simulator = AerSimulator()\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("qc = transpile(qc, simulator)\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("compiled_circuit = simulator.run(qc, shots=16384)\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("result = compiled_circuit.result()\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("counts = result.get_counts(qc)\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("probs = {state: count / 16384 for state, count in counts.items()}\n"));
    string_list_push(&scratch.arena, &output,
                     str_lit("pprint.pprint(probs)\n"));
    
    OS_FileCreateWrite(filename, string_list_flatten(&scratch.arena, &output));
    scratch_return(&scratch);
}

static MQ_GateType gate_map(GateType g) {
    switch (g) {
        case Gate_H:  return MQ_Gate_H;
        case Gate_PX: return MQ_Gate_X;
        case Gate_PY: return MQ_Gate_Y;
        case Gate_PZ: return MQ_Gate_Z;
        case Gate_S:  return MQ_Gate_S;
        case Gate_T:  return MQ_Gate_T;
    }
    return MQ_Gate_X;
}

static void mq_instr_push(MQ_Circuit* c, MQ_Instruction instr) {
    if (c->len >= c->cap) {
        c->cap = c->cap ? c->cap * 2 : 32;
        c->instructions = realloc(c->instructions,
                                  sizeof(MQ_Instruction) * c->cap);
    }
    c->instructions[c->len++] = instr;
}

void CircuitEmitAsIR(Circuit* circuit, string filename) {
    MQ_Circuit ir = {0};
    ir.qubit_count = circuit->qubit_count;
    ir.classical_count = 0;
    ir.cap = 64;
    ir.instructions = malloc(sizeof(MQ_Instruction) * ir.cap);
    
    for (u32 s = 0; s < circuit->len; s++) {
        OperatorSlice* slice = &circuit->slices[s];
        
        u32 controls[64];
        u8 control_states[64];
        u32 control_count = 0;
        
        /* collect controls in this slice */
        for (u32 q = 0; q < slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            if (op->type == OpType_ControlOn) {
                controls[control_count] = q;
                control_states[control_count] = 1;
                control_count++;
            }
            else if (op->type == OpType_ControlOff) {
                controls[control_count] = q;
                control_states[control_count] = 0;
                control_count++;
            }
        }
        
        /* emit instructions */
        for (u32 q = 0; q < slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            if (op->type == OpType_Gate1) {
                
                MQ_Instruction instr = {0};
                instr.type = MQ_Instr_Gate;
                instr.gate = gate_map(op->gate);
                
                instr.qubits[0] = q;
                instr.qubit_count = 1;
                
                for (u32 c = 0; c < control_count; c++) {
                    instr.controls[c] = controls[c];
                    instr.control_states[c] = control_states[c];
                }
                instr.control_count = control_count;
                
                mq_instr_push(&ir, instr);
            }
            
            if (op->type == OpType_Inspect) {
                mq_instr_push(&ir, (MQ_Instruction){
                                  .type = MQ_Instr_Measure,
                                  .qubits = {q},
                                  .qubit_count = 1,
                                  .classical_bits = {q},
                                  .classical_count = 1
                              });
            }
        }
    }
    
    M_Scratch s = scratch_get();
    string ir_text = mq_ir_to_string(&s.arena, &ir);
    OS_FileCreateWrite(filename, ir_text);
    scratch_return(&s);
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

QGate QGate_FromOperator(GateType t) {
    QGate g;
    g.size = QGate_1;
    
    switch (t) {
        case Gate_H: {
            double s = 0.7071067811865475244;
            g.m1[0][0] = (Complex){ s, 0 };
            g.m1[0][1] = (Complex){ s, 0 };
            g.m1[1][0] = (Complex){ s, 0 };
            g.m1[1][1] = (Complex){ -s, 0 };
        } break;
        
        case Gate_PX: {
            g.m1[0][0] = (Complex){ 0, 0 };
            g.m1[0][1] = (Complex){ 1, 0 };
            g.m1[1][0] = (Complex){ 1, 0 };
            g.m1[1][1] = (Complex){ 0, 0 };
        } break;
        
        case Gate_PY: {
            g.m1[0][0] = (Complex){ 0, 0 };
            g.m1[0][1] = (Complex){ 0, -1 };
            g.m1[1][0] = (Complex){ 0, 1 };
            g.m1[1][1] = (Complex){ 0, 0 };
        } break;
        
        case Gate_PZ: {
            g.m1[0][0] = (Complex){ 1, 0 };
            g.m1[0][1] = (Complex){ 0, 0 };
            g.m1[1][0] = (Complex){ 0, 0 };
            g.m1[1][1] = (Complex){ -1, 0 };
        } break;
        
        case Gate_S: {
            g.m1[0][0] = (Complex){ 1, 0 };
            g.m1[0][1] = (Complex){ 0, 0 };
            g.m1[1][0] = (Complex){ 0, 0 };
            g.m1[1][1] = (Complex){ -1, 0 };
        } break;
        
        case Gate_T: {
            g.m1[0][0] = (Complex){ 1, 0 };
            g.m1[0][1] = (Complex){ 0, 0 };
            g.m1[1][0] = (Complex){ 0, 0 };
            g.m1[1][1] = (Complex){ -1, 0 };
        } break;
    }
    
    return g;
}

void CircuitEvaluate(Circuit* c) {
    QStateResize(&c->state, c->qubit_count);
    
    QState* state = &c->state;
    
    for (u32 s = 0; s < c->len; s++) {
        OperatorSlice* slice = &c->slices[s];
        
        // Temporary arrays for gating
        i32 gate_control = 0;
        
        // Prepass gather controls
        for (u32 q = 0; q < slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            switch (op->type) {
                case OpType_Identity:
                case OpType_Inspect:
                case OpType_Gate1:
                break;
                
                case OpType_ControlOn:
                gate_control = q+1;
                break;
                
                case OpType_ControlOff:
                gate_control = -(q+1);
                break;
            }
        }
        
        // Prepass gather controls
        for (u32 q = 0; q < slice->len; q++) {
            Operator* op = &slice->ops[q];
            
            switch (op->type) {
                case OpType_Gate1: {
                    QGate g = QGate_FromOperator(op->gate);
                    QGateApply(&g, &q, state);
                } break;
                
                default: break;
            }
        }
    }
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

//- Editable Blocks


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

#define BLOCK_SIZE       50.0f
#define QUBIT_SEPARATION (BLOCK_SIZE*1.5f)

void DrawBlock(Rift_UIBox* box, Rift_UISimpleRenderer* boxrenderer, Rift_TriRenderer* trirenderer) {
    EditableCircuitBlock* block = box->custom_context;
    vec2 half_size = v2(box->render_width * 0.5f, box->render_height * 0.5f);
    
    if (block->image) {
        Rift_TriRendererDrawImage(trirenderer, vec2_add(box->render_pos, half_size),
                                  v2(box->render_width, box->render_height), block->image);
    }
    
    if (block->linked_conditional) {
        Rift_TriRendererDrawLine(trirenderer, vec2_add(box->render_pos, half_size),
                                 vec2_add(block->linked_conditional->box->render_pos, half_size),
                                 Color_White, 4);
    }
}

EditableCircuitBlock* EditableBlockCreate(EditContext* ctx, BlockType type) {
    EditableCircuitBlock* block = pool_alloc(&ctx->circuit_block_pool);
    MemoryZeroStruct(block, EditableCircuitBlock);
    
    Rift_UIBoxFlags extra = 0;
    if (type == BlockType_X ||
        type == BlockType_ControlOn ||
        type == BlockType_ControlOff)
        extra |= UIBoxFlag_NoStandardDraw;
    
    block->box = Rift_UIBoxCreate(ctx->ui, ctx->content,
                                  Rift_UISizePixels(BLOCK_SIZE), Rift_UISizePixels(BLOCK_SIZE),
                                  &g_block_style,
                                  UIBoxFlag_DrawBack | UIBoxFlag_DrawBorder |
                                  UIBoxFlag_DrawName | UIBoxFlag_Interactive |
                                  UIBoxFlag_CustomDraw | UIBoxFlag_DrawOnTop |
                                  extra);
    block->box->text = block_type_names[type];
    block->box->custom_context = block;
    block->box->render_fn = DrawBlock;
    
    block->type = type;
    block->line = 100000;
    block->timeslice = 100000;
    
    switch (type) {
        case BlockType_X: block->image = ctx->texture_px; break;
        case BlockType_ControlOn:  block->image = ctx->texture_con_on; break;
        case BlockType_ControlOff: block->image = ctx->texture_con_off; break;
        default: break;
    }
    
    if (ctx->first_block && ctx->last_block) {
        ctx->last_block->next = block;
        block->prev = ctx->last_block;
        ctx->last_block = block;
    } else {
        ctx->first_block = block;
        ctx->last_block  = block;
    }
    ctx->block_count += 1;
    
    return block;
}

void EditableBlockFree(EditContext* ctx, EditableCircuitBlock* block) {
    if (ctx->first_block == block) ctx->first_block = block->next;
    if (ctx->last_block  == block) ctx->last_block  = block->prev;
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    
    Rift_UIBoxDestroy(ctx->ui, block->box);
    pool_dealloc(&ctx->circuit_block_pool, block);
    
    ctx->block_count -= 1;
}

static void ClearOperator(EditContext* ctx, u32 timeslice, u32 line) {
    Operator* ref = &ctx->circuit.slices[timeslice].ops[line];
    ref->type = OpType_Identity;
}

static void SetOperator(EditContext* ctx, u32 timeslice, u32 line, BlockType type) {
    Operator* ref = &ctx->circuit.slices[timeslice].ops[line];
    
    switch (type) {
        case BlockType_H: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_H;
        } break;
        
        case BlockType_X: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_PX;
        } break;
        
        case BlockType_Y: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_PY;
        } break;
        
        case BlockType_Z: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_PZ;
        } break;
        
        case BlockType_S: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_S;
        } break;
        
        case BlockType_T: {
            ref->type = OpType_Gate1;
            ref->gate = Gate_T;
        } break;
        
        case BlockType_ControlOn: {
            ref->type = OpType_ControlOn;
        } break;
        
        case BlockType_ControlOff: {
            ref->type = OpType_ControlOff;
        } break;
        
        case BlockType_Prob: {
            ref->type = OpType_ControlOff;
        } break;
        
        case BlockType_Bloch: {
            ref->type = OpType_ControlOff;
        } break;
        
        case BlockType_Amp: {
            ref->type = OpType_ControlOff;
        } break;
        
        case BlockType_Max: break;
    }
}

//- Editor

EditContext* EditorCreate(M_Arena* arena, Rift_UIContext* ui, Rift_UIBox* content) {
    EditContext* ctx = arena_alloc_zero(arena, sizeof(EditContext));
    arena_init(&ctx->arena);
    pool_init(&ctx->circuit_block_pool, sizeof(EditableCircuitBlock));
    
    ctx->texture_px = Rift_LoadTexture(str_lit("assets/PX_gate.png"));
    ctx->texture_con_on = Rift_LoadTexture(str_lit("assets/On_condition.png"));
    ctx->texture_con_off = Rift_LoadTexture(str_lit("assets/Off_condition.png"));
    
    CircuitSetQubitCount(&ctx->circuit, 4);
    CircuitPushTimeslice(&ctx->circuit);
    
    ctx->ui = ui;
    ctx->content = content;
    
    ctx->laidout_content = Rift_UIBoxCreate(ctx->ui, ctx->content,
                                            Rift_UISizePct(1.0f), Rift_UISizePct(1.0f),
                                            &g_panel_style,
                                            0);
    ctx->laidout_content->layout_axis = UIAxis_Y;
    ctx->laidout_content->style.color = v4(0.12f, 0.12f, 0.14f, 1.0f);
    
    //- Top bar
    
    ctx->top_bar = Rift_UIBoxCreate(ctx->ui, ctx->laidout_content,
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
    
    ctx->qubit_area = Rift_UIBoxCreate(ctx->ui, ctx->laidout_content,
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
        u32 new_count = Min(ctx->circuit.qubit_count + 1, MAX_QUBITS);
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
        
        // Remove excess items
        for (EditableCircuitBlock* curr = ctx->first_block; curr; ) {
            EditableCircuitBlock* next = curr->next;
            if (curr->line == new_count) {
                EditableBlockFree(ctx, curr);
            }
            curr = next;
        }
        CircuitSetQubitCount(&ctx->circuit, new_count);
    }
    
    // Select dragging target
    for (int i = 0; i < BlockType_Max; i++) {
        Rift_UISignal sig = Rift_UIBoxSignal(ctx->ui, ctx->pick_gates[i]);
        
        if ((sig.dragX || sig.dragY) && !ctx->dragging) {
            ctx->dragging = true;
            
            ctx->dragging_block = EditableBlockCreate(ctx, i);
            ctx->dragging_block->box->pos.x = OS_InputMouseX() - BLOCK_SIZE * 0.5f;
            ctx->dragging_block->box->pos.y = OS_InputMouseY() - 30.0f - BLOCK_SIZE * 0.5f;
            
            ctx->dragging_block->line = 100000;
            ctx->dragging_block->timeslice = 100000;
        }
    }
    
    for (EditableCircuitBlock* curr = ctx->first_block; curr; curr = curr->next) {
        Rift_UISignal sig = Rift_UIBoxSignal(ctx->ui, curr->box);
        
        if ((sig.dragX || sig.dragY) && !ctx->dragging) {
            ctx->dragging = true;
            
            curr->linked_conditional = nullptr;
            for (EditableCircuitBlock* other = ctx->first_block; other; other = other->next) {
                if (curr != other && curr->timeslice == other->timeslice) {
                    if (curr->type == BlockType_ControlOn ||
                        curr->type == BlockType_ControlOff) {
                        other->linked_conditional = nullptr;
                    }
                }
            }
            
            ctx->dragging_block = curr;
            ctx->dragging_block->box->pos.x = OS_InputMouseX() - BLOCK_SIZE * 0.5f;
            ctx->dragging_block->box->pos.y = OS_InputMouseY() - 30.0f - BLOCK_SIZE * 0.5f;
            
            ClearOperator(ctx, ctx->dragging_block->timeslice, ctx->dragging_block->line);
            
            ctx->dragging_block->line = 100000;
            ctx->dragging_block->timeslice = 100000;
        }
    }
    
    // Process dragging
    if (ctx->dragging) {
        ctx->dragging_block->box->pos.x = OS_InputMouseX() - BLOCK_SIZE * 0.5f;
        ctx->dragging_block->box->pos.y = OS_InputMouseY() - 30.0f - BLOCK_SIZE * 0.5f;
    }
    
    if (OS_InputMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT) && ctx->dragging) {
        ctx->dragging = false;
        
        // Place in right place
        Rift_UISignal dropoff = Rift_UIBoxSignal(ctx->ui, ctx->top_bar);
        if (dropoff.hovering) {
            EditableBlockFree(ctx, ctx->dragging_block);
        } else {
            i32 nearest_line = -1;
            f32 min_dist = FLOAT_MAX;
            for (int i = 0; i < ctx->circuit.qubit_count; i++) {
                f32 dist = dist_to_line(ctx->lines[i]->render_pos,
                                        vec2_add(ctx->lines[i]->render_pos, v2(ctx->lines[i]->render_width, 0.0f)),
                                        OS_InputMousePos());
                if (dist < min_dist) {
                    min_dist = dist;
                    nearest_line = i;
                }
            }
            
            i32 nearest_timeslice = -1;
            min_dist = FLOAT_MAX;
            for (int i = 0; i < ctx->circuit.len; i++) {
                f32 dist = dist_to_line(v2(ctx->qubit_lines_area->render_pos.x + i * BLOCK_SIZE + 40.0f, ctx->qubit_lines_area->render_pos.y),
                                        v2(ctx->qubit_lines_area->render_pos.x + i * BLOCK_SIZE + 40.0f, ctx->qubit_lines_area->render_pos.y + 1000),
                                        OS_InputMousePos());
                if (dist < min_dist) {
                    min_dist = dist;
                    nearest_timeslice = i;
                }
            }
            if (nearest_timeslice == ctx->circuit.len-1) {
                CircuitPushTimeslice(&ctx->circuit);
            } else {
                // TODO(voxel): Reduce timeslices
            }
            
            
            b8 to_shift = false;
            b8 cancel_placement = false;
            for (EditableCircuitBlock* curr = ctx->first_block; curr; curr = curr->next) {
                if (curr->line == nearest_line && curr->timeslice == nearest_timeslice) {
                    if (OS_InputKey(GLFW_KEY_LEFT_SHIFT)) {
                        to_shift = true;
                    } else {
                        cancel_placement = true;
                    }
                    break;
                }
            }
            
            if (to_shift) {
                for (EditableCircuitBlock* curr = ctx->first_block; curr; curr = curr->next) {
                    if (curr->timeslice >= nearest_timeslice) {
                        curr->timeslice += 1;
                        curr->box->pos.x += BLOCK_SIZE;
                    }
                }
                CircuitInsertTimesliceAt(&ctx->circuit, nearest_timeslice);
            }
            
            if (!cancel_placement) {
                vec2 found_pos = v2(ctx->qubit_lines_area->render_pos.x + nearest_timeslice * BLOCK_SIZE,
                                    ctx->lines[nearest_line]->render_pos.y - 30.0f - BLOCK_SIZE * 0.5f);
                
                ctx->dragging_block->line = nearest_line;
                ctx->dragging_block->timeslice = nearest_timeslice;
                
                SetOperator(ctx, ctx->dragging_block->timeslice, ctx->dragging_block->line, ctx->dragging_block->type);
                
                // Update linked conditional statuses
                for (EditableCircuitBlock* other = ctx->first_block; other; other = other->next) {
                    if (ctx->dragging_block->type == BlockType_ControlOn ||
                        ctx->dragging_block->type == BlockType_ControlOff) {
                        if (nearest_timeslice == other->timeslice) {
                            other->linked_conditional = ctx->dragging_block;
                        }
                    } else if (other->type == BlockType_ControlOn ||
                               other->type == BlockType_ControlOff) {
                        if (nearest_timeslice == other->timeslice) {
                            ctx->dragging_block->linked_conditional = other;
                        }
                    }
                }
                
                
                ctx->dragging_block->box->pos = found_pos;
            } else {
                EditableBlockFree(ctx, ctx->dragging_block);
            }
        }
        
        ctx->dragging_block = nullptr;
    }
    
    if (OS_InputKey(GLFW_KEY_LEFT_CONTROL) && OS_InputKeyPressed(GLFW_KEY_S)) {
        if (OS_InputKey(GLFW_KEY_LEFT_SHIFT)) {
            printf("Saved Circuit to circuit.mq\n"); flush;
            CircuitEmitAsIR(&ctx->circuit, str_lit("circuit.mq"));
        } else {
            printf("Saved Circuit to circuit.py\n"); flush;
            CircuitEmitAsPython(&ctx->circuit, str_lit("circuit.py"));
        }
    }
    
    if (OS_InputKey(GLFW_KEY_LEFT_CONTROL) && OS_InputKeyPressed(GLFW_KEY_P)) {
        CircuitEvaluate(&ctx->circuit);
        
        for (u32 i = 0; i < ctx->circuit.state.size; i++) {
            printf("|");
            for (int b = ctx->circuit.state.bitcount - 1; b >= 0; b--) {
                printf("%u", (i >> b) & 1u);
            }
            printf("> = ");
            ComplexPrint(ctx->circuit.state.state[i]);
            printf("\n");
            flush;
        }
    }
}

void EditorFree(EditContext* ctx) {
    CircuitFree(&ctx->circuit);
    pool_free(&ctx->circuit_block_pool);
    arena_free(&ctx->arena);
}
