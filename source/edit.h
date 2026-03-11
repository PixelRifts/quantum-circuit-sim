/* date = November 13th 2025 0:41 pm */

#ifndef EDIT_H
#define EDIT_H

#include "base/base.h"

#include "client/ui.h"
#include "quantum.h"

#define MAX_QUBITS 8

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
    Gate_S,
    Gate_T,
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
    
    QState state;
} Circuit;


void CircuitEmitAsPython(Circuit* circuit, string filename);
void CircuitSetQubitCount(Circuit* circuit, int new_count);
OperatorSlice* CircuitInsertTimesliceAt(Circuit* circuit, u32 index);
OperatorSlice* CircuitPushTimeslice(Circuit* circuit);



typedef enum BlockType {
    BlockType_H,
    BlockType_X,
    BlockType_Y,
    BlockType_Z,
    BlockType_S,
    BlockType_T,
    
    BlockType_ControlOn,
    BlockType_ControlOff,
    
    BlockType_Prob,
    BlockType_Bloch,
    BlockType_Amp,
    
    BlockType_Max,
} BlockType;

static string block_type_names[BlockType_Max] = {
    [BlockType_H] = str_lit("H"),
    [BlockType_X] = str_lit("PX"),
    [BlockType_Y] = str_lit("PY"),
    [BlockType_Z] = str_lit("PZ"),
    [BlockType_S] = str_lit("S"),
    [BlockType_T] = str_lit("T"),
    [BlockType_ControlOn]  = str_lit("On"),
    [BlockType_ControlOff] = str_lit("Off"),
    [BlockType_Prob]  = str_lit("Prb"),
    [BlockType_Bloch] = str_lit("Blch"),
    [BlockType_Amp] = str_lit("Amp"),
};

typedef struct EditableCircuitBlock EditableCircuitBlock;
struct EditableCircuitBlock {
    EditableCircuitBlock* next;
    EditableCircuitBlock* prev;
    
    BlockType type;
    Rift_UIBox* box;
    
    EditableCircuitBlock* linked_conditional;
    
    u32 line;
    u32 timeslice;
    u32 image;
};

typedef struct EditContext {
    M_Arena arena;
    M_Pool circuit_block_pool;
    Circuit circuit;
    
    Rift_UIContext* ui;
    Rift_UIBox* content;
    Rift_UIBox* laidout_content;
    Rift_UIBox* top_bar;
    Rift_UIBox* qubit_area;
    Rift_UIBox* qubit_labels_area;
    Rift_UIBox* qubit_lines_area;
    
    Rift_UIBox* qubit_add;
    Rift_UIBox* qubit_remove;
    
    Rift_UIBox* pick_gates[BlockType_Max];
    Rift_UIBox* label_paddings[MAX_QUBITS];
    Rift_UIBox* labels[MAX_QUBITS];
    Rift_UIBox* lines[MAX_QUBITS];
    
    b8 dragging;
    EditableCircuitBlock* dragging_block;
    
    EditableCircuitBlock* first_block;
    EditableCircuitBlock* last_block;
    u32 block_count;
    
    u32 texture_px;
    u32 texture_con_on;
    u32 texture_con_off;
} EditContext;

EditContext* EditorCreate(M_Arena* arena, Rift_UIContext* ui, Rift_UIBox* content);
void EditorUpdate(EditContext* ctx, f32 delta);
void EditorFree(EditContext* ctx);

#endif //EDIT_H
