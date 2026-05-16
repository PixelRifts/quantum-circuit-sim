
#ifndef QSHARP_H
#define QSHARP_H

#include "../defines.h"
#include "../base/str.h"
#include <tree_sitter/api.h>

/* ── Opaque handle returned by the Q# parse pass ─────────────────────────── */
typedef struct QS_ParseResult QS_ParseResult;

struct QS_ParseResult {
    TSTree   *tree;        /* owned Tree-sitter tree                          */
    TSParser *parser;      /* owned parser (keep alive while tree is used)    */
    char     *source;      /* null-terminated copy of the source text         */
    u32       source_len;
    b8        ok;          /* false if the file could not be read / parsed    */
};

/* ── Public API ───────────────────────────────────────────────────────────── */

/*
 * Parse a Q# source file located at `path`.
 * Returns a QS_ParseResult; check `.ok` before using the tree.
 * Call qs_parse_result_free() when done.
 */
QS_ParseResult qs_parse_file(const char *path);

/*
 * Print a human-readable dump of the entire syntax tree to stdout.
 * `result` must have `.ok == true`.
 */
void qs_print_tree(const QS_ParseResult *result);

/*
 * Print every named node together with its source text (useful for
 * quickly verifying what Tree-sitter recognised in a Q# file).
 */
void qs_print_named_nodes(const QS_ParseResult *result);

/*
 * Release all resources held by a QS_ParseResult.
 */
void qs_parse_result_free(QS_ParseResult *result);

/* ── Tree-sitter language binding ────────────────────────────────────────── */
/*
 * Declared here so translation units that need the raw TSLanguage*
 * (e.g. future IR-lowering code) can reach it without going through
 * the Tree-sitter DLL header directly.
 */
extern const TSLanguage *tree_sitter_qsharp(void);

#endif /* QSHARP_H */
