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
} GateType;

typedef struct Operator {
    OperatorType type;
    
    union {
        GateType gate;
        InspectType inspect;
    };
} Operator;

typedef struct OperatorSlice {
    u32 len;
    u32 cap;
    Operator* ops;
} OperatorSlice;

typedef struct Circuit {
    u32 len;
    u32 cap;
    OperatorSlice* slices;
} Circuit;





typedef struct EditContext {
    OperatorSlice* ;
} EditContext;

EditContext* EditorCreate(M_Arena* arena);
void EditorFree();

#endif //EDIT_H
