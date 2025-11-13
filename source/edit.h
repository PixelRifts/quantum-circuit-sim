/* date = November 13th 2025 0:41 pm */

#ifndef EDIT_H
#define EDIT_H

#include "base/base.h"

#include "client/ui.h"

typedef struct EditContext {
    
} EditContext;

EditContext* EditorCreate(M_Arena* arena);
void EditorFree();

#endif //EDIT_H
