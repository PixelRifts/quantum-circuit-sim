/* date = November 13th 2025 0:41 pm */

#ifndef EDIT_H
#define EDIT_H

#include "base/base.h"

#include "client/ui.h"




typedef enum OperatorType {
    OpType_Identity,
    OpType_ControlOn,
    OpType_ControlOff,
    OpType_Gate1,
    OpType_Inspect,
} OperatorType;

typedef enum GateType {
    Gate_H,
    Gate_PX,
    Gate_PY,
    Gate_PZ,
} GateType;

typedef enum InspectType {
    Inspect_Chance,
    Inspect_BlochSphere,
} InspectType;


// Tagged / Discriminated Union
typedef struct Operator {
    OperatorType type;
    
    union {
        GateType gate;
        InspectType inspect;
    };
} Operator;


// A VERTICAL SLICE NOT HORIZONTAL
// len always equal to qubit_count
typedef struct OperatorSlice {
    u32 len;
    u32 cap;
    Operator* ops;
} OperatorSlice;

// A HORIZONTAL SET OF VERTICAL SLICES
typedef struct Circuit {
    u32 qubit_count;
    u32 len;
    u32 cap;
    OperatorSlice* slices;
} Circuit;


void CircuitEmitAsPython(Circuit* circuit, string filename);
void CircuitSetQubitCount(Circuit* circuit, int new_count);
OperatorSlice* CircuitInsertTimesliceAt(Circuit* circuit, u32 index);
OperatorSlice* CircuitPushTimeslice(Circuit* circuit);



typedef enum BlockType {
    BlockType_H,
    BlockType_X,
    BlockType_Y,
    BlockType_Max,
} BlockType;

static string block_type_names[BlockType_Max] = {
    [BlockType_H] = str_lit("H"),
    [BlockType_X] = str_lit("X"),
    [BlockType_Y] = str_lit("Y"),
};

typedef struct EditContext {
    M_Arena arena;
    Circuit circuit;
    
    Rift_UIContext* ui;
    Rift_UIBox* content;
    Rift_UIBox* top_bar;
    Rift_UIBox* qubit_area;
    Rift_UIBox* qubit_labels_area;
    Rift_UIBox* qubit_lines_area;
    
    Rift_UIBox* qubit_add;
    Rift_UIBox* qubit_remove;
    
    Rift_UIBox* pick_gates[BlockType_Max];
    Rift_UIBox* label_paddings[8];
    Rift_UIBox* labels[8];
    Rift_UIBox* lines[8];
} EditContext;

EditContext* EditorCreate(M_Arena* arena, Rift_UIContext* ui, Rift_UIBox* content);
void EditorUpdate(EditContext* ctx, f32 delta);
void EditorFree(EditContext* ctx);

#endif //EDIT_H
