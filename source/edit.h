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



typedef struct EditContext {
    Circuit circuit;
    
    Rift_UIContext* ctx;
    Rift_UIBox* lines;
} EditContext;

EditContext* EditorCreate(M_Arena* arena);
void EditorFree(EditContext* ctx);

#endif //EDIT_H
