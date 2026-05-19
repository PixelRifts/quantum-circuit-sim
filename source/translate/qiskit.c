#include "qiskit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PY_SNIPPET_MAX 64

/* ────────────────────────────────────────────────────────────── */
/* Read entire file into memory                                 */
/* ────────────────────────────────────────────────────────────── */

static char *read_file(const char *path, u32 *out_len)
{
    FILE *f = fopen(path, "rb");

    if (!f) {
        fprintf(stderr, "[python] cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);

    long size = ftell(f);

    rewind(f);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);

    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);

    fclose(f);

    buf[read] = '\0';

    *out_len = (u32)read;

    return buf;
}

/* ────────────────────────────────────────────────────────────── */
/* Copy readable source snippet                                 */
/* ────────────────────────────────────────────────────────────── */

static void copy_snippet(char *dst,
                         size_t dst_size,
                         const char *src,
                         u32 src_len,
                         u32 max_chars)
{
    while (src_len > 0 &&
          (*src == ' '  ||
           *src == '\t' ||
           *src == '\n' ||
           *src == '\r'))
    {
        src++;
        src_len--;
    }

    b8 truncated = (src_len > max_chars);

    u32 copy_len = truncated ? max_chars : src_len;

    if (copy_len >= (u32)dst_size - 4) {
        copy_len = (u32)dst_size - 4;
    }

    memcpy(dst, src, copy_len);

    if (truncated) {

        memcpy(dst + copy_len, "...", 3);

        dst[copy_len + 3] = '\0';

    } else {

        dst[copy_len] = '\0';
    }

    for (char *p = dst; *p; p++) {

        if (*p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
}

/* ────────────────────────────────────────────────────────────── */
/* Recursive tree printer                                       */
/* ────────────────────────────────────────────────────────────── */

static void print_node(TSNode node,
                       const char *source,
                       int depth)
{
    const char *type = ts_node_type(node);

    b8 is_named  = ts_node_is_named(node);
    b8 has_error = ts_node_has_error(node);

    u32 start_byte = ts_node_start_byte(node);
    u32 end_byte   = ts_node_end_byte(node);

    u32 span = end_byte - start_byte;

    TSPoint sp = ts_node_start_point(node);
    TSPoint ep = ts_node_end_point(node);

    /* indentation */

    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    /* node type */

    if (is_named) {

        printf("\033[1;36m%s\033[0m", type);

    } else {

        printf("\033[0;33m\"%s\"\033[0m", type);
    }

    /* source position */

    printf(" \033[0;90m[%u:%u - %u:%u]\033[0m",
           sp.row + 1,
           sp.column,
           ep.row + 1,
           ep.column);

    /* parse error */

    if (has_error) {
        printf(" \033[1;31m<ERROR>\033[0m");
    }

    /* source snippet */

    if (span > 0 && span < 200) {

        char snippet[PY_SNIPPET_MAX + 4];

        copy_snippet(
            snippet,
            sizeof(snippet),
            source + start_byte,
            span,
            PY_SNIPPET_MAX
        );

        printf("  \033[0;32m`%s`\033[0m", snippet);
    }

    printf("\n");

    /* recurse */

    u32 child_count = ts_node_child_count(node);

    for (u32 i = 0; i < child_count; i++) {

        TSNode child = ts_node_child(node, i);

        print_node(child, source, depth + 1);
    }
}

/* ────────────────────────────────────────────────────────────── */
/* Public API                                                    */
/* ────────────────────────────────────────────────────────────── */

Qiskit_ParseResult qiskit_parse_file(const char *path)
{
    Qiskit_ParseResult result;

    memset(&result, 0, sizeof(result));

    result.ok = false;

    /* 1. Read source */

    result.source = read_file(path, &result.source_len);

    if (!result.source) {
        return result;
    }

    /* 2. Create parser */

    result.parser = ts_parser_new();

    if (!result.parser) {

        fprintf(stderr,
                "[python] ts_parser_new() failed\n");

        free(result.source);

        result.source = NULL;

        return result;
    }

    /* 3. Attach Python grammar */

    if (!ts_parser_set_language(
            result.parser,
            tree_sitter_python()))
    {
        fprintf(stderr,
                "[python] ts_parser_set_language() failed\n");

        ts_parser_delete(result.parser);

        free(result.source);

        result.parser = NULL;
        result.source = NULL;

        return result;
    }

    /* 4. Parse */

    result.tree = ts_parser_parse_string(
        result.parser,
        NULL,
        result.source,
        result.source_len
    );

    if (!result.tree) {

        fprintf(stderr,
                "[python] parsing failed\n");

        ts_parser_delete(result.parser);

        free(result.source);

        result.parser = NULL;
        result.source = NULL;

        return result;
    }

    /* 5. Check parse errors */

    TSNode root = ts_tree_root_node(result.tree);

    if (ts_node_has_error(root)) {

        fprintf(stderr,
                "[python] warning: parse errors detected\n");
    }

    result.ok = true;

    return result;
}

void qiskit_print_tree(const Qiskit_ParseResult *result)
{
    if (!result || !result->ok) {

        fprintf(stderr,
                "[python] py_print_tree: invalid result\n");

        return;
    }

    TSNode root = ts_tree_root_node(result->tree);

    printf("\n=== Python Syntax Tree ===\n\n");

    print_node(root, result->source, 0);

    printf("\n");
}

void qiskit_parse_result_free(Qiskit_ParseResult *result)
{
    if (!result) {
        return;
    }

    if (result->tree) {

        ts_tree_delete(result->tree);

        result->tree = NULL;
    }

    if (result->parser) {

        ts_parser_delete(result->parser);

        result->parser = NULL;
    }

    if (result->source) {

        free(result->source);

        result->source = NULL;
    }

    result->ok = false;
}