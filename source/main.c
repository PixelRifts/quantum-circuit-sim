#include "defines.h"
#include "os/os.h"
#include "os/input.h"
#include "base/base.h"
#include "client/window.h"

#include "client/ui.h"
#include "client/simple_ui_render.h"
#include "client/tri_render.h"

//#include "netsim.h"
#include "quantum.h"

#include <glad/glad.h>
#include <stdlib.h>
#include <time.h>



int main() {
    Complex a = { 3.0, 4.0 };
    Complex b = { 1.0, 2.0 };
    printf("a = "); ComplexPrint(a); printf("\n");
    printf("b = "); ComplexPrint(b); printf("\n");
    
    // Testing!!
    Complex c = ComplexAdd(a, b);
    printf("a+b = "); ComplexPrint(c); printf("\n");

    Complex d = ComplexSub(a, b);
    printf("a-b = "); ComplexPrint(d); printf("\n");

    Complex e = ComplexMul(a, b);
    printf("a*b = "); ComplexPrint(e); printf("\n");

    Complex conjugate = ComplexConjugate(a);
    printf("a* = "); ComplexPrint(conjugate); printf("\n");

    Complex div = ComplexDiv(a,b);
    printf("a/b = "); ComplexPrint(div); printf("\n");

    f64 mag = ComplexMag(a);
    printf("mag(a) = %lf\n",mag);

    f64 angle = ComplexAngle(a);
    printf("angle(deg) = %lf\n",angle);

    Complex scale = ComplexScale(a,4);
    printf("a*s = "); ComplexPrint(scale); printf("\n");

    Complex theta = ComplexExp(0);
    printf("theta = "); ComplexPrint(theta); printf("\n");
    // Add all the function tests here
}

//- Bringing this back later
/*
int main() {
	OS_Init();
	
    ThreadContext context = {0};
	tctx_init(&context);
    U_FrameArenaInit();
    
    M_Arena systems_arena = {0};
    arena_init(&systems_arena);
    
    srand(time(0));
    
    
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
    
    
    float start = 0.0f;
    float end = 0.016f;
    float delta = 0.016f;
    
    while (Rift_WindowIsOpen(&window)) {
        delta = end - start;
        start = glfwGetTime();
        
        OS_InputReset();
        Rift_PollEvents();
        
        glClear(GL_COLOR_BUFFER_BIT);
        
        Rift_WindowUpdateTitlebar(&window, ctx);
        Rift_UIContextUpdate(ctx, delta);
        
        
        Rift_UIRendererBegin(renderer);
        Rift_TriRendererBegin(trirenderer);
        
        Rift_UIContextDraw(ctx, renderer, trirenderer);
        
        Rift_TriRendererEnd(trirenderer);
        Rift_UIRendererEnd(renderer);
        
        Rift_WindowSwapBuffers(&window);
        end = glfwGetTime();
    }
    
    SimulationDestroy(netsim);
    
    Rift_UIRendererFree(renderer);
    Rift_TriRendererFree(trirenderer);
    Rift_UIContextDestroy(ctx);
    
    
    Rift_WindowDestroy(&window);
    
    
    arena_free(&systems_arena);
    U_FrameArenaFree();
	tctx_free(&context);
}
*/