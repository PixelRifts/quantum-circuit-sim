/* qiskit.h */

#ifndef QISKIT_H
#define QISKIT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../defines.h"
#include "../base/str.h"
#include "tree_sitter/api.h"
#include "ir.h"

typedef uint32_t u32;

/* Provided by tree-sitter-python grammar */
extern const TSLanguage *tree_sitter_python();

typedef struct {
    b8        ok;

    char     *source;
    u32       source_len;

    TSParser *parser;
    TSTree   *tree;
} Qiskit_ParseResult;

Qiskit_ParseResult qiskit_parse_file(const char *path);

void qiskit_print_tree(const Qiskit_ParseResult *result);

void qiskit_parse_result_free(Qiskit_ParseResult *result);

MQ_Program *qiskit_parse(M_Arena *arena, string src);

MQ_Program *qiskit_tree_to_ir(const Qiskit_ParseResult *result, M_Arena *arena);

void mq_ir_to_code(FILE *out, MQ_Program *prog);

#endif