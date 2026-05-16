
#include "qsharp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Maximum source snippet shown next to a node when printing. */
#define QS_SNIPPET_MAX 64

/*
 * Read an entire file into a heap-allocated, null-terminated buffer.
 * Returns NULL on failure.  *out_len receives the byte count (excl. '\0').
 */
static char *read_file(const char *path, u32 *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[qsharp] cannot open '%s'\n", path);
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
    *out_len  = (u32)read;
    return buf;
}

/*
 * Copy at most `max_chars` UTF-8 bytes from `src` into `dst`,
 * appending "…" when the text was truncated, and always null-terminating.
 */
static void copy_snippet(char *dst, size_t dst_size,
                         const char *src, u32 src_len, u32 max_chars)
{
    /* strip leading whitespace for readability */
    while (src_len > 0 && (*src == ' ' || *src == '\t' ||
                           *src == '\r' || *src == '\n')) {
        src++;
        src_len--;
    }

    b8 truncated = (src_len > max_chars);
    u32 copy_len = truncated ? max_chars : src_len;

    if (copy_len >= (u32)dst_size - 4)
        copy_len = (u32)dst_size - 4;

    memcpy(dst, src, copy_len);
    if (truncated) {
        memcpy(dst + copy_len, "...", 3);
        dst[copy_len + 3] = '\0';
    } else {
        dst[copy_len] = '\0';
    }

    /* replace newlines so the snippet stays on one line */
    for (char *p = dst; *p; p++) {
        if (*p == '\r' || *p == '\n') *p = ' ';
    }
}

/* ── recursive tree printer ──────────────────────────────────────────────── */

static void print_node(TSNode node, const char *source, int depth)
{
    const char *type      = ts_node_type(node);
    b8          is_named  = ts_node_is_named(node);
    b8          has_error = ts_node_has_error(node);

    u32 start_byte = ts_node_start_byte(node);
    u32 end_byte   = ts_node_end_byte(node);
    u32 span       = end_byte - start_byte;

    TSPoint sp = ts_node_start_point(node);
    TSPoint ep = ts_node_end_point(node);

    /* indentation */
    for (int i = 0; i < depth; i++) printf("  ");

    /* node type  (named nodes shown without quotes, anonymous with) */
    if (is_named) {
        printf("\033[1;36m%s\033[0m", type);          /* cyan  */
    } else {
        printf("\033[0;33m\"%s\"\033[0m", type);       /* yellow */
    }

    /* position */
    printf(" \033[0;90m[%u:%u – %u:%u]\033[0m",
           sp.row + 1, sp.column, ep.row + 1, ep.column);

    /* error marker */
    if (has_error) printf(" \033[1;31m<ERROR>\033[0m");

    /* source snippet for leaf nodes */
    u32 child_count = ts_node_child_count(node);
    if (child_count == 0 && span > 0 && span < 200) {
        char snippet[QS_SNIPPET_MAX + 4];
        copy_snippet(snippet, sizeof(snippet),
                     source + start_byte, span, QS_SNIPPET_MAX);
        printf("  \033[0;32m`%s`\033[0m", snippet);
    }

    printf("\n");

    for (u32 i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        print_node(child, source, depth + 1);
    }
}

/* ── public API ──────────────────────────────────────────────────────────── */

QS_ParseResult qs_parse_file(const char *path)
{
    QS_ParseResult result;
    memset(&result, 0, sizeof(result));
    result.ok = false;

    /* ── 1. read source ─────────────────────────────────────────────────── */
    result.source = read_file(path, &result.source_len);
    if (!result.source) return result;

    /* ── 2. create parser ───────────────────────────────────────────────── */
    result.parser = ts_parser_new();
    if (!result.parser) {
        fprintf(stderr, "[qsharp] ts_parser_new() failed\n");
        free(result.source);
        result.source = NULL;
        return result;
    }

    /* Link the Q# Tree-sitter grammar.
     * tree_sitter_qsharp() is provided by the grammar DLL that build.bat
     * copies into bin/.  The declaration is in qsharp.h. */
    if (!ts_parser_set_language(result.parser, tree_sitter_qsharp())) {
        fprintf(stderr, "[qsharp] ts_parser_set_language() failed – "
                        "grammar version mismatch?\n");
        ts_parser_delete(result.parser);
        free(result.source);
        result.source = NULL;
        result.parser = NULL;
        return result;
    }

    /* ── 3. parse ───────────────────────────────────────────────────────── */
    result.tree = ts_parser_parse_string(result.parser, NULL,
                                         result.source, result.source_len);
    if (!result.tree) {
        fprintf(stderr, "[qsharp] ts_parser_parse_string() returned NULL\n");
        ts_parser_delete(result.parser);
        free(result.source);
        result.source = NULL;
        result.parser = NULL;
        return result;
    }

    /* ── 4. report any parse errors ─────────────────────────────────────── */
    TSNode root = ts_tree_root_node(result.tree);
    if (ts_node_has_error(root)) {
        fprintf(stderr, "[qsharp] warning: parse errors detected in '%s'\n",
                path);
        /* We still return ok=true; the caller can inspect the tree */
    }

    result.ok = true;
    return result;
}

void qs_print_tree(const QS_ParseResult *result)
{
    if (!result || !result->ok) {
        fprintf(stderr, "[qsharp] qs_print_tree: invalid result\n");
        return;
    }

    TSNode root = ts_tree_root_node(result->tree);
    printf("\n=== Q# Syntax Tree ===\n\n");
    print_node(root, result->source, 0);
    printf("\n");
}

void qs_print_named_nodes(const QS_ParseResult *result)
{
    if (!result || !result->ok) {
        fprintf(stderr, "[qsharp] qs_print_named_nodes: invalid result\n");
        return;
    }

    printf("\n=== Q# Named Nodes ===\n");
    printf("%-40s  %-8s  %s\n", "Node Type", "Line", "Source");
    printf("%-40s  %-8s  %s\n",
           "----------------------------------------",
           "--------",
           "----------------------------------------------");

    /* Depth-first walk via Tree-sitter cursor */
    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(result->tree));

    b8 visited_children = false;
    while (true) {
        TSNode node = ts_tree_cursor_current_node(&cursor);

        if (!visited_children && ts_node_is_named(node)) {
            const char *type      = ts_node_type(node);
            TSPoint     sp        = ts_node_start_point(node);
            u32         start     = ts_node_start_byte(node);
            u32         end       = ts_node_end_byte(node);
            u32         span      = end - start;

            char snippet[QS_SNIPPET_MAX + 4] = "";
            /* only show a snippet if this is a leaf or a small node */
            if (span < 120) {
                copy_snippet(snippet, sizeof(snippet),
                             result->source + start, span, QS_SNIPPET_MAX);
            }

            b8 has_error = ts_node_has_error(node);
            printf("%-40s  %4u:%-3u  %s%s\n",
                   type,
                   sp.row + 1, sp.column,
                   snippet,
                   has_error ? "  <ERROR>" : "");
        }

        /* advance cursor */
        if (!visited_children && ts_tree_cursor_goto_first_child(&cursor)) {
            visited_children = false;
        } else if (ts_tree_cursor_goto_next_sibling(&cursor)) {
            visited_children = false;
        } else if (ts_tree_cursor_goto_parent(&cursor)) {
            visited_children = true;
        } else {
            break;
        }
    }

    ts_tree_cursor_delete(&cursor);
    printf("\n");
}

void qs_parse_result_free(QS_ParseResult *result)
{
    if (!result) return;

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
