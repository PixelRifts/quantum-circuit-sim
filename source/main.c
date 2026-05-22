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

#include "translate/mql.h"
#include "translate/ir.h"
#include "translate/qsharp.h"
#include "translate/qiskit.h"
#include "translate/cirq.h"

static string read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[qsharp] cannot open '%s'\n", path);
        return (string){0};
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    
    if (size < 0) {
        fclose(f);
        return (string){0};
    }
    
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return (string){0};
    }
    
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    
    buf[read] = '\0';
    return (string) {.str = (u8*)buf, .size = read};
}

void run_editor(M_Arena* systems_arena) {
    Rift_Window window = {0};
    Rift_WindowCreate(&window, (Rift_WindowProps) {
                          .width = 1080,
                          .height = 720,
                          .name = str_lit("Composer"),
                          .custom_titlebar = true,
                      });
    
    glClearColor(0.2, 0.2, 0.28, 1);
    glViewport(0, 0, 1080, 720);
    
    Rift_UIContext* ctx = Rift_UIContextCreate(systems_arena, 1080, 720);
    
    Rift_TriRenderer* trirenderer = Rift_TriRendererInit(systems_arena, 1080, 720);
    Rift_UISimpleRenderer* renderer = Rift_UIRendererInit(systems_arena, 1080, 720);
    Rift_UIFontLoad(renderer, "CascadiaCode");
    
    Rift_UIBox* content = Rift_WindowCustomTitlebar(&window, ctx, renderer, trirenderer);
    
    
    EditContext* editor = EditorCreate(systems_arena, ctx, content);
    
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
    
}


//-

typedef enum FileFormat {
    Fmt_None,
    Fmt_MQ,
    Fmt_MQL,
    Fmt_Qiskit,
    Fmt_Cirq,
    Fmt_QSharp,
} FileFormat;


static FileFormat fmt_from_name(const char *name) {
    if (strcmp(name, "mql")    == 0) return Fmt_MQL;
    if (strcmp(name, "mq")     == 0) return Fmt_MQ;
    if (strcmp(name, "qiskit") == 0) return Fmt_Qiskit;
    if (strcmp(name, "cirq")   == 0) return Fmt_Cirq;
    if (strcmp(name, "qsharp") == 0) return Fmt_QSharp;
    return Fmt_None;
}

static const char *fmt_name(FileFormat f) {
    switch (f) {
        case Fmt_MQL:    return "mql";
        case Fmt_MQ:     return "mq";
        case Fmt_Qiskit: return "qiskit";
        case Fmt_Cirq:   return "cirq";
        case Fmt_QSharp: return "qsharp";
        default:         return "unknown";
    }
}

static FileFormat fmt_from_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return Fmt_None;
    if (strcmp(dot, ".mql") == 0) return Fmt_MQL;
    if (strcmp(dot, ".mq")  == 0) return Fmt_MQ;
    if (strcmp(dot, ".qs")  == 0) return Fmt_QSharp;
    // .py is intentionally left as Fmt_None -- caller must require --from/--to
    return Fmt_None;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [options] <input>\n"
            "\n"
            "  --from <fmt>   force input format\n"
            "  --to   <fmt>   force output format\n"
            "  -o <file>      output file (stdout if omitted)\n"
            "  --editor       open editor UI\n"
            "\n"
            "formats: mql  qiskit  cirq  qsharp  mq\n"
            "\n"
            "  mql    MetaQ source  (.mql)  -- input only\n"
            "  mq     MQ IR dump   (.mq)   -- output only\n"
            "  qiskit Qiskit        (.py)\n"
            "  cirq   Cirq          (.py)\n"
            "  qsharp Q#            (.qs)\n"
            "\n"
            "note: .py is ambiguous; --from / --to is required when using Python files\n"
            "\n"
            "examples:\n"
            "  %s prog.mql -o out.qs\n"
            "  %s --from qiskit bell.py -o bell.qs\n"
            "  %s --from cirq sim.py --to qiskit -o out.py\n",
            argv0, argv0, argv0, argv0);
}


int main(int argc, char **argv) {
    OS_Init();
    
    ThreadContext context = {0};
    tctx_init(&context);
    U_FrameArenaInit();
    
    M_Arena systems_arena = {0};
    arena_init(&systems_arena);
    srand(time(0));
    
    // ==== Argument Parsing ====
    const char *input_path  = 0;
    const char *output_path = 0;
    FileFormat  forced_in   = Fmt_None;
    FileFormat  forced_out  = Fmt_None;
    int         open_editor = 0;
    
    // Sorry for what i have done
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--editor") == 0) {
            open_editor = 1;
            
        } else if (strcmp(argv[i], "--from") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --from requires an argument\n");
                usage(argv[0]);
                return 1;
            }
            forced_in = fmt_from_name(argv[++i]);
            if (forced_in == Fmt_None) {
                fprintf(stderr, "error: unknown format '%s'\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
            if (forced_in == Fmt_MQ) {
                fprintf(stderr, "error: 'mq' is an IR dump and cannot be used as input\n");
                return 1;
            }
            
        } else if (strcmp(argv[i], "--to") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --to requires an argument\n");
                usage(argv[0]);
                return 1;
            }
            forced_out = fmt_from_name(argv[++i]);
            if (forced_out == Fmt_None) {
                fprintf(stderr, "error: unknown format '%s'\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
            if (forced_out == Fmt_MQL) {
                fprintf(stderr, "error: 'mql' is a source format and cannot be used as output\n");
                return 1;
            }
            
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                usage(argv[0]);
                return 1;
            }
            output_path = argv[++i];
            
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
            
        } else {
            if (input_path) {
                fprintf(stderr, "error: multiple input files specified\n");
                usage(argv[0]);
                return 1;
            }
            input_path = argv[i];
        }
    }
    
    
    // ==== editor mode ====
    if (open_editor) {
        run_editor(&systems_arena);
        goto cleanup;
    }
    
    // ==== all other modes ====
    if (!input_path) {
        fprintf(stderr, "error: no input file specified\n");
        usage(argv[0]);
        return 1;
    }
    
    // ==== resolve input format ====
    FileFormat in_fmt = forced_in;
    
    if (in_fmt == Fmt_None) {
        in_fmt = fmt_from_ext(input_path);
        if (in_fmt == Fmt_None) {
            // Check specifically for .py to give a targeted error
            const char *dot = strrchr(input_path, '.');
            if (dot && strcmp(dot, ".py") == 0) {
                fprintf(stderr,
                        "error: cannot infer input format from '.py' "
                        "(both Qiskit and Cirq use Python)\n"
                        "       use --from qiskit  or  --from cirq\n");
            } else {
                fprintf(stderr,
                        "error: cannot infer input format from '%s'\n"
                        "       use --from <fmt>\n", input_path);
            }
            return 1;
        }
    }
    
    // ==== resolve output format ====
    FileFormat out_fmt = forced_out;
    
    if (out_fmt == Fmt_None && output_path) {
        out_fmt = fmt_from_ext(output_path);
        if (out_fmt == Fmt_None) {
            const char *dot = strrchr(output_path, '.');
            if (dot && strcmp(dot, ".py") == 0) {
                fprintf(stderr,
                        "error: cannot infer output format from '.py' "
                        "(both Qiskit and Cirq use Python)\n"
                        "       use --to qiskit  or  --to cirq\n");
            } else {
                fprintf(stderr,
                        "error: cannot infer output format from '%s'\n"
                        "       use --to <fmt>\n", output_path);
            }
            return 1;
        }
    }
    
    if (out_fmt == Fmt_None)
        out_fmt = Fmt_MQ;
    
    // ==== parser ====
    string src = read_file(input_path);
    MQ_Program *prog  = 0;
    
    switch (in_fmt) {
        case Fmt_MQL:
        case Fmt_None:  {
            MQL_Parser parser;
            mql_parser_init(&parser, &systems_arena, src);
            prog = mql_parse_program(&parser);
            if (parser.errors) {
                for (MQL_Error *e = parser.errors; e; e = e->next)
                    fprintf(stderr, "%s:%d:%d: error: %.*s\n",
                            input_path, e->line, e->col, str_expand(e->message));
                return 1;
            }
        } break;
        
       
            case Fmt_Qiskit: {
    Qiskit_ParseResult qk = qiskit_parse_file(input_path);
    if (qk.ok) {
        prog = qiskit_tree_to_ir(&qk, &systems_arena);
        qiskit_parse_result_free(&qk);
    } else {
        fprintf(stderr, "%s: error: Qiskit parse failed\n", input_path);
        return 1;
    }
    if (!prog) {
        fprintf(stderr, "%s: error: Qiskit IR lowering failed\n", input_path);
        return 1;
    }
} break;
        
        case Fmt_Cirq: {
            // prog = @call cirq_parse
            fprintf(stderr, "Cirq input is currently unimplemented\n");
            if (!prog) {
                fprintf(stderr, "%s: error: Cirq parse failed\n", input_path);
                return 1;
            }
        } break;
        
        case Fmt_QSharp: {
            // 1. Run your frontend Tree-sitter parser pass
            QS_ParseResult qs = qs_parse_file(input_path);
            if (qs.ok) {
                // 2. Lower the concrete syntax tree straight into your universal MQ_Program IR
                prog = qsharp_tree_to_ir(&qs, &systems_arena);
                
                // 3. Free frontend configurations cleanly
                qs_parse_result_free(&qs);
            } else {
                fprintf(stderr, "%s: error: Q# frontend parse failed\n", input_path);
                return 1;
            }
        } break;
        
        case Fmt_MQ: {
            fprintf(stderr, "%s: error: .mq is an IR dump and cannot be used as input\n",
                    input_path);
        } return 1;
    }
    
    
    // Output
    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "w");
        if (!out) {
            fprintf(stderr, "error: could not open output file '%s'\n", output_path);
            return 1;
        }
    }
    
    switch (out_fmt) {
        case Fmt_MQ:
        case Fmt_None: {
            mq_program_write(out, prog);
        } break;
        
       case Fmt_Qiskit: {
    mq_ir_to_code(out, prog);
} break;
        
        case Fmt_Cirq: {
            // @call cirq_emit(out, prog);
            fprintf(stderr, "Cirq output is currently unimplemented\n");
        } break;
        
        case Fmt_QSharp: {
            // @call qsharp_emit(out, prog);
            // fprintf(stderr, "Qsharp output is currently unimplemented\n");
                qsharp_emit(out, prog);
        } break;
        
        case Fmt_MQL: {
            mql_emit(out, prog);
            if (output_path) fclose(out);
        } return 1;
    }
    
    if (output_path) fclose(out);
    
    
    cleanup:
    arena_free(&systems_arena);
    U_FrameArenaFree();
    tctx_free(&context);
    return 0;
}



