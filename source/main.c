#include "defines.h"
#include "os/os.h"
#include "os/input.h"
#include "base/base.h"
#include "client/window.h"

#include "client/ui.h"
#include "client/simple_ui_render.h"
#include "client/tri_render.h"

#include "edit.h"
#include "quantum.h"

#include <glad/glad.h>
#include <stdlib.h>
#include <time.h>

#include "ir.h"

int main() {
    OS_Init();
	
    ThreadContext context = {0};
    tctx_init(&context);
    U_FrameArenaInit();
    
    M_Arena systems_arena = {0};
    arena_init(&systems_arena);
    
    srand(time(0));
    example_ir_dump(&systems_arena);
    
    Rift_Window window = {0};
    Rift_WindowCreate(&window, (Rift_WindowProps) {
                          .width = 1080,
                          .height = 720,
                          .name = str_lit("Composer"),
                          .custom_titlebar = true,
                      });
    
    glClearColor(0.2, 0.2, 0.28, 1);
    glViewport(0, 0, 1080, 720);
    
    Rift_UIContext* ctx = Rift_UIContextCreate(&systems_arena, 1080, 720);
    
    Rift_TriRenderer* trirenderer = Rift_TriRendererInit(&systems_arena, 1080, 720);
    Rift_UISimpleRenderer* renderer = Rift_UIRendererInit(&systems_arena, 1080, 720);
    Rift_UIFontLoad(renderer, "CascadiaCode");
    
    Rift_UIBox* content = Rift_WindowCustomTitlebar(&window, ctx, renderer, trirenderer);
    
    
    EditContext* editor = EditorCreate(&systems_arena, ctx, content);
    
    float start = 0.0f;
    float end = 0.016f;
    float delta = 0.016f;
    
    vec4 col = color_code_to_vec4(0x1F1F24FF);
    glClearColor(col.x, col.y, col.z, col.w);
    while (Rift_WindowIsOpen(&window)) {
        delta = end - start;
        start = glfwGetTime();
        
        OS_InputReset();
        Rift_PollEvents();
        
        glClear(GL_COLOR_BUFFER_BIT);
        
        Rift_WindowUpdateTitlebar(&window, ctx);
        EditorUpdate(editor, delta);
        Rift_UIContextUpdate(ctx, delta);
        
        Rift_TriRendererBegin(trirenderer);
        Rift_UIRendererBegin(renderer);
        
        Rift_UIContextDraw(ctx, renderer, trirenderer);
        
        Rift_TriRendererEnd(trirenderer);
        Rift_UIRendererEnd(renderer);
        
        Rift_WindowSwapBuffers(&window);
        end = glfwGetTime();
    }
    
    
    EditorFree(editor);
    
    
    Rift_UIRendererFree(renderer);
    Rift_TriRendererFree(trirenderer);
    Rift_UIContextDestroy(ctx);
    
    Rift_WindowDestroy(&window);
    
    
    arena_free(&systems_arena);
    U_FrameArenaFree();
	tctx_free(&context);
}
