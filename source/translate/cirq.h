/* python.h */

#ifndef CIRQ_H
#define CIRQ_H

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
} Cirq_ParseResult;

Cirq_ParseResult cirq_parse_file(const char *path);

void cirq_print_tree(const Cirq_ParseResult *result);

void cirq_parse_result_free(Cirq_ParseResult *result);

MQ_Program *cirq_tree_to_ir(const Cirq_ParseResult *result, M_Arena *arena);

void cirq_emit(FILE *out, MQ_Program *prog);

#endif