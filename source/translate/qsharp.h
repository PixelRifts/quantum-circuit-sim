#ifndef QSHARP_H
#define QSHARP_H

#include "../defines.h"
#include "../base/str.h"
#include "../base/mem.h"
#include <tree_sitter/api.h>

/* Forward declaration of the revamped IR structure */
typedef struct MQ_Program MQ_Program;

typedef struct QS_ParseResult {
    TSTree   *tree;
    TSParser *parser;
    char     *source;
    u32       source_len;
    b8        ok;
} QS_ParseResult;

QS_ParseResult qs_parse_file(const char *path);
void           qs_print_tree(const QS_ParseResult *result);
void           qs_print_named_nodes(const QS_ParseResult *result);
void           qs_parse_result_free(QS_ParseResult *result);

/* Your new Tree-to-IR translation signature pass */
MQ_Program* qsharp_tree_to_ir(const QS_ParseResult *result, M_Arena *arena);
void           qsharp_emit(FILE *out, MQ_Program *prog);

extern const TSLanguage *tree_sitter_qsharp(void);

#endif /* QSHARP_H */