#include "mql.h"
#include <string.h>
#include <stdio.h>

// ============================================================
// Character helpers
// ============================================================

static b8 is_alpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static b8 is_digit(u8 c) { return c >= '0' && c <= '9'; }
static b8 is_alnum(u8 c) { return is_alpha(c) || is_digit(c); }
static b8 is_space(u8 c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// ============================================================
// Cursor helpers
// ============================================================

static b8  at_end  (MQL_Lexer *l)           { return l->pos >= l->src.size; }
static u8  cur     (MQL_Lexer *l)           { return at_end(l) ? 0 : l->src.str[l->pos]; }
static u8  peek_ch (MQL_Lexer *l, u64 off)  { u64 p = l->pos + off; return p < l->src.size ? l->src.str[p] : 0; }

static u8 advance(MQL_Lexer *l)
{
    u8 c = cur(l);
    if (!at_end(l)) {
        l->pos++;
        if (c == '\n') { l->line++; l->col = 1; }
        else           { l->col++;              }
    }
    return c;
}

// Consume next char only if it equals expected; return 1 if consumed.
static b8 eat(MQL_Lexer *l, u8 expected)
{
    if (cur(l) == expected) { advance(l); return 1; }
    return 0;
}

// Build a token whose lexeme starts at start_pos with a saved start line/col.
static MQL_Token make_token(MQL_Lexer *l, MQL_TokenKind kind,
                            u64 start_pos, u32 start_line, u32 start_col)
{
    MQL_Token t;
    t.kind   = kind;
    t.lexeme = (string){ .str = l->src.str + start_pos, .size = l->pos - start_pos };
    t.line   = start_line;
    t.col    = start_col;
    return t;
}

static MQL_Token make_error(MQL_Lexer *l, u64 start_pos, u32 start_line, u32 start_col)
{
    return make_token(l, MQL_Tok_Error, start_pos, start_line, start_col);
}

// ============================================================
// Keyword / gate name table
// ============================================================

static b8 lexeme_eq_cstr(string s, const char *word)
{
    u64 len = strlen(word);
    if (s.size != len) return 0;
    return memcmp(s.str, word, len) == 0;
}

typedef struct { const char *word; MQL_TokenKind kind; } MQL_KwEntry;

static const MQL_KwEntry kw_table[] = {
    // Language keywords
    { "gate",      MQL_Tok_Gate      },
    { "operation", MQL_Tok_Operation },
    { "circuit",   MQL_Tok_Circuit   },
    { "param",     MQL_Tok_Param     },
    { "let",       MQL_Tok_Let       },
    { "set",       MQL_Tok_Set       },
    { "qubit",     MQL_Tok_Qubit     },
    { "bit",       MQL_Tok_Bit       },
    { "measure",   MQL_Tok_Measure   },
    { "reset",     MQL_Tok_Reset     },
    { "barrier",   MQL_Tok_Barrier   },
    { "delay",     MQL_Tok_Delay     },
    { "adjoint",   MQL_Tok_Adjoint   },
    { "ctrl",      MQL_Tok_Ctrl      },
    { "ctrl0",     MQL_Tok_Ctrl0     },
    { "if",        MQL_Tok_If        },
    { "elif",      MQL_Tok_Elif      },
    { "else",      MQL_Tok_Else      },
    { "for",       MQL_Tok_For       },
    { "in",        MQL_Tok_In        },
    { "while",     MQL_Tok_While     },
    { "break",     MQL_Tok_Break     },
    { "continue",  MQL_Tok_Continue  },
    { "return",    MQL_Tok_Return    },
    { "true",      MQL_Tok_True      },
    { "false",     MQL_Tok_False     },
    { "void",      MQL_Tok_Void      },
    { "bool",      MQL_Tok_Bool      },
    { "int",       MQL_Tok_Int       },
    { "float",     MQL_Tok_Float     },
    { "angle",     MQL_Tok_Angle     },
    
    // Built-in math functions
    { "sin",       MQL_Tok_Sin       },
    { "cos",       MQL_Tok_Cos       },
    { "tan",       MQL_Tok_Tan       },
    { "asin",      MQL_Tok_Asin      },
    { "acos",      MQL_Tok_Acos      },
    { "atan",      MQL_Tok_Atan      },
    { "sqrt",      MQL_Tok_Sqrt      },
    { "exp",       MQL_Tok_Exp       },
    { "log",       MQL_Tok_Log       },
    { "abs",       MQL_Tok_Abs       },
    
    // Built-in gate names
    { "I",         MQL_Tok_GateI     },
    { "H",         MQL_Tok_GateH     },
    { "X",         MQL_Tok_GateX     },
    { "Y",         MQL_Tok_GateY     },
    { "Z",         MQL_Tok_GateZ     },
    { "S",         MQL_Tok_GateS     },
    { "Sdg",       MQL_Tok_GateSdg   },
    { "T",         MQL_Tok_GateT     },
    { "Tdg",       MQL_Tok_GateTdg   },
    { "P",         MQL_Tok_GateP     },
    { "RX",        MQL_Tok_GateRX    },
    { "RY",        MQL_Tok_GateRY    },
    { "RZ",        MQL_Tok_GateRZ    },
    { "U",         MQL_Tok_GateU     },
    { "SWAP",      MQL_Tok_GateSWAP  },
    { "ISWAP",     MQL_Tok_GateISWAP },
    { "RZZ",       MQL_Tok_GateRZZ   },
    { "RXX",       MQL_Tok_GateRXX   },
    { "RYY",       MQL_Tok_GateRYY   },
    { "CCX",       MQL_Tok_GateCCX   },
    { "CSWAP",     MQL_Tok_GateCSWAP },
    { "CNOT",      MQL_Tok_GateCNOT  },
    { "CZ",        MQL_Tok_GateCZ    },
};

static MQL_TokenKind classify_ident(string s)
{
    for (u64 i = 0; i < sizeof(kw_table) / sizeof(kw_table[0]); i++) {
        if (lexeme_eq_cstr(s, kw_table[i].word))
            return kw_table[i].kind;
    }
    return MQL_Tok_Ident;
}

// ============================================================
// Time unit detection
//
// Called after lexing a numeric literal when no whitespace separates
// the number from the following characters.  Tries to consume a time
// unit suffix; returns 1 and advances pos if one is found.
// ============================================================

static b8 try_lex_time_unit(MQL_Lexer *l)
{
    u8 c0 = cur(l);
    u8 c1 = peek_ch(l, 1);
    
    // Two-character units first: dt  ns  us  ms
    if ((c0 == 'd' && c1 == 't') ||
        (c0 == 'n' && c1 == 's') ||
        (c0 == 'u' && c1 == 's') ||
        (c0 == 'm' && c1 == 's'))
    {
        // Only consume if not followed by a further alphanumeric char
        // (avoids turning "100ms_foo" into a partial time literal).
        if (!is_alnum(peek_ch(l, 2))) {
            advance(l);
            advance(l);
            return 1;
        }
    }
    
    // Single-character unit: s
    if (c0 == 's' && !is_alnum(c1)) {
        advance(l);
        return 1;
    }
    
    return 0;
}

// ============================================================
// Sub-lexers
// ============================================================

static MQL_Token lex_line_comment(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    // Already consumed "//"; consume to end of line (not including newline).
    while (!at_end(l) && cur(l) != '\n')
        advance(l);
    return make_token(l, MQL_Tok_Comment, sp, sl, sc);
}

static MQL_Token lex_block_comment(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    // Already consumed "/*"; consume until matching "*/".
    while (!at_end(l)) {
        if (cur(l) == '*' && peek_ch(l, 1) == '/') {
            advance(l); // *
            advance(l); // /
            return make_token(l, MQL_Tok_Comment, sp, sl, sc);
        }
        advance(l);
    }
    // Unterminated block comment
    return make_error(l, sp, sl, sc);
}

static MQL_Token lex_ident_or_keyword(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    while (is_alnum(cur(l)))
        advance(l);
    
    string lexeme = { .str = l->src.str + sp, .size = l->pos - sp };
    MQL_TokenKind kind = classify_ident(lexeme);
    
    return make_token(l, kind, sp, sl, sc);
}

static MQL_Token lex_number(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    // Integer digits
    while (is_digit(cur(l)))
        advance(l);
    
    MQL_TokenKind num_kind = MQL_Tok_IntLit;
    
    // Optional fractional part: "." [digits] [exponent]
    if (cur(l) == '.' && is_digit(peek_ch(l, 1))) {
        num_kind = MQL_Tok_FloatLit;
        advance(l); // .
        while (is_digit(cur(l)))
            advance(l);
        
        // Optional exponent: [eE][+-]?[digits]
        if (cur(l) == 'e' || cur(l) == 'E') {
            advance(l);
            if (cur(l) == '+' || cur(l) == '-')
                advance(l);
            while (is_digit(cur(l)))
                advance(l);
        }
    }
    
    // Time unit suffix check: only valid if immediately adjacent (no space).
    if (try_lex_time_unit(l))
        return make_token(l, MQL_Tok_TimeLit, sp, sl, sc);
    
    return make_token(l, num_kind, sp, sl, sc);
}

static MQL_Token lex_string(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    // Already consumed the opening '"'.
    while (!at_end(l) && cur(l) != '"') {
        if (cur(l) == '\\') {
            advance(l); // backslash
            if (!at_end(l))
                advance(l); // escaped char
        } else {
            advance(l);
        }
    }
    
    if (at_end(l))
        return make_error(l, sp, sl, sc); // unterminated string
    
    advance(l); // closing '"'
    return make_token(l, MQL_Tok_StringLit, sp, sl, sc);
}

static MQL_Token lex_pragma(MQL_Lexer *l, u64 sp, u32 sl, u32 sc)
{
    // Already consumed '#'; expect the literal text "pragma".
    const char *pragma_word = "pragma";
    for (int i = 0; pragma_word[i]; i++) {
        if (at_end(l) || cur(l) != (u8)pragma_word[i])
            return make_error(l, sp, sl, sc);
        advance(l);
    }
    // "#pragma" must not be immediately followed by an alphanumeric char.
    if (is_alnum(cur(l)))
        return make_error(l, sp, sl, sc);
    return make_token(l, MQL_Tok_Pragma, sp, sl, sc);
}

// ============================================================
// Core: produce one token, advancing the lexer state.
// Whitespace is consumed silently.
// ============================================================

static MQL_Token mql_lex_one(MQL_Lexer *l)
{
    // Skip whitespace
    while (!at_end(l) && is_space(cur(l)))
        advance(l);
    
    if (at_end(l)) {
        MQL_Token t = { .kind = MQL_Tok_Eof, .line = l->line, .col = l->col };
        t.lexeme = (string){0};
        return t;
    }
    
    u64 sp = l->pos;
    u32 sl = l->line;
    u32 sc = l->col;
    
    u8 c = advance(l);
    
    switch (c) {
        
        // Single-character unambiguous punctuation
        case '(': return make_token(l, MQL_Tok_LParen,    sp, sl, sc);
        case ')': return make_token(l, MQL_Tok_RParen,    sp, sl, sc);
        case '{': return make_token(l, MQL_Tok_LBrace,    sp, sl, sc);
        case '}': return make_token(l, MQL_Tok_RBrace,    sp, sl, sc);
        case '[': return make_token(l, MQL_Tok_LBracket,  sp, sl, sc);
        case ']': return make_token(l, MQL_Tok_RBracket,  sp, sl, sc);
        case ';': return make_token(l, MQL_Tok_Semicolon, sp, sl, sc);
        case ',': return make_token(l, MQL_Tok_Comma,     sp, sl, sc);
        case ':': return make_token(l, MQL_Tok_Colon,     sp, sl, sc);
        case '~': return make_token(l, MQL_Tok_Tilde,     sp, sl, sc);
        case '%': return make_token(l, MQL_Tok_Percent,   sp, sl, sc);
        case '.': return make_token(l, MQL_Tok_Dot,       sp, sl, sc);
        
        // '!' or '!='
        case '!':
        if (eat(l, '=')) return make_token(l, MQL_Tok_BangEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Bang, sp, sl, sc);
        
        // '=' or '=='
        case '=':
        if (eat(l, '=')) return make_token(l, MQL_Tok_EqEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Eq, sp, sl, sc);
        
        // '+' or '+='
        case '+':
        if (eat(l, '=')) return make_token(l, MQL_Tok_PlusEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Plus, sp, sl, sc);
        
        // '-' or '-=' or '->'
        case '-':
        if (eat(l, '>')) return make_token(l, MQL_Tok_Arrow,   sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_MinusEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Minus, sp, sl, sc);
        
        // '*' or '**' or '*='
        case '*':
        if (eat(l, '*')) return make_token(l, MQL_Tok_StarStar, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_StarEq,   sp, sl, sc);
        return make_token(l, MQL_Tok_Star, sp, sl, sc);
        
        // '/' or '/=' or '//' comment or '/* */' comment
        case '/':
        if (eat(l, '/')) return lex_line_comment (l, sp, sl, sc);
        if (eat(l, '*')) return lex_block_comment(l, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_SlashEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Slash, sp, sl, sc);
        
        // '<' or '<=' or '<<' or '<<='
        case '<':
        if (eat(l, '<')) {
            if (eat(l, '=')) return make_token(l, MQL_Tok_LtLtEq, sp, sl, sc);
            return make_token(l, MQL_Tok_LtLt, sp, sl, sc);
        }
        if (eat(l, '=')) return make_token(l, MQL_Tok_LtEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Lt, sp, sl, sc);
        
        // '>' or '>=' or '>>' or '>>='
        case '>':
        if (eat(l, '>')) {
            if (eat(l, '=')) return make_token(l, MQL_Tok_GtGtEq, sp, sl, sc);
            return make_token(l, MQL_Tok_GtGt, sp, sl, sc);
        }
        if (eat(l, '=')) return make_token(l, MQL_Tok_GtEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Gt, sp, sl, sc);
        
        // '&' or '&&' or '&='
        case '&':
        if (eat(l, '&')) return make_token(l, MQL_Tok_AmpAmp, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_AmpEq,  sp, sl, sc);
        return make_token(l, MQL_Tok_Amp, sp, sl, sc);
        
        // '|' or '||' or '|='
        case '|':
        if (eat(l, '|')) return make_token(l, MQL_Tok_PipePipe, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_PipeEq,   sp, sl, sc);
        return make_token(l, MQL_Tok_Pipe, sp, sl, sc);
        
        // '^' or '^='
        case '^':
        if (eat(l, '=')) return make_token(l, MQL_Tok_CaretEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Caret, sp, sl, sc);
        
        // '#' -> #pragma
        case '#': return lex_pragma(l, sp, sl, sc);
        
        // String literal
        case '"': return lex_string(l, sp, sl, sc);
        
        default:
        if (is_digit(c)) return lex_number(l, sp, sl, sc);
        if (is_alpha(c)) return lex_ident_or_keyword(l, sp, sl, sc);
        return make_error(l, sp, sl, sc);
    }
}

// ============================================================
// Public API
// ============================================================

void mql_lexer_init(MQL_Lexer *lex, string src)
{
    lex->src        = src;
    lex->pos        = 0;
    lex->line       = 1;
    lex->col        = 1;
    lex->has_peeked = 0;
    lex->peeked     = (MQL_Token){0};
}

MQL_Token mql_next(MQL_Lexer *lex)
{
    if (lex->has_peeked) {
        lex->has_peeked = 0;
        return lex->peeked;
    }
    return mql_lex_one(lex);
}

MQL_Token mql_peek(MQL_Lexer *lex)
{
    if (!lex->has_peeked) {
        lex->peeked     = mql_lex_one(lex);
        lex->has_peeked = 1;
    }
    return lex->peeked;
}

// ============================================================
// Debug helper
// ============================================================

const char *mql_token_kind_str(MQL_TokenKind kind)
{
    switch (kind) {
        case MQL_Tok_Eof:       return "Eof";
        case MQL_Tok_Error:     return "Error";
        case MQL_Tok_Comment:   return "Comment";
        case MQL_Tok_Ident:     return "Ident";
        case MQL_Tok_IntLit:    return "IntLit";
        case MQL_Tok_FloatLit:  return "FloatLit";
        case MQL_Tok_StringLit: return "StringLit";
        case MQL_Tok_TimeLit:   return "TimeLit";
        
        case MQL_Tok_Gate:      return "gate";
        case MQL_Tok_Operation: return "operation";
        case MQL_Tok_Circuit:   return "circuit";
        case MQL_Tok_Param:     return "param";
        case MQL_Tok_Let:       return "let";
        case MQL_Tok_Set:       return "set";
        case MQL_Tok_Qubit:     return "qubit";
        case MQL_Tok_Bit:       return "bit";
        case MQL_Tok_Measure:   return "measure";
        case MQL_Tok_Reset:     return "reset";
        case MQL_Tok_Barrier:   return "barrier";
        case MQL_Tok_Delay:     return "delay";
        case MQL_Tok_Adjoint:   return "adjoint";
        case MQL_Tok_Ctrl:      return "ctrl";
        case MQL_Tok_Ctrl0:     return "ctrl0";
        case MQL_Tok_If:        return "if";
        case MQL_Tok_Elif:      return "elif";
        case MQL_Tok_Else:      return "else";
        case MQL_Tok_For:       return "for";
        case MQL_Tok_In:        return "in";
        case MQL_Tok_While:     return "while";
        case MQL_Tok_Break:     return "break";
        case MQL_Tok_Continue:  return "continue";
        case MQL_Tok_Return:    return "return";
        case MQL_Tok_True:      return "true";
        case MQL_Tok_False:     return "false";
        case MQL_Tok_Void:      return "void";
        case MQL_Tok_Bool:      return "bool";
        case MQL_Tok_Int:       return "int";
        case MQL_Tok_Float:     return "float";
        case MQL_Tok_Angle:     return "angle";
        
        case MQL_Tok_Sin:       return "sin";
        case MQL_Tok_Cos:       return "cos";
        case MQL_Tok_Tan:       return "tan";
        case MQL_Tok_Asin:      return "asin";
        case MQL_Tok_Acos:      return "acos";
        case MQL_Tok_Atan:      return "atan";
        case MQL_Tok_Sqrt:      return "sqrt";
        case MQL_Tok_Exp:       return "exp";
        case MQL_Tok_Log:       return "log";
        case MQL_Tok_Abs:       return "abs";
        
        case MQL_Tok_GateI:     return "I";
        case MQL_Tok_GateH:     return "H";
        case MQL_Tok_GateX:     return "X";
        case MQL_Tok_GateY:     return "Y";
        case MQL_Tok_GateZ:     return "Z";
        case MQL_Tok_GateS:     return "S";
        case MQL_Tok_GateSdg:   return "Sdg";
        case MQL_Tok_GateT:     return "T";
        case MQL_Tok_GateTdg:   return "Tdg";
        case MQL_Tok_GateP:     return "P";
        case MQL_Tok_GateRX:    return "RX";
        case MQL_Tok_GateRY:    return "RY";
        case MQL_Tok_GateRZ:    return "RZ";
        case MQL_Tok_GateU:     return "U";
        case MQL_Tok_GateSWAP:  return "SWAP";
        case MQL_Tok_GateISWAP: return "ISWAP";
        case MQL_Tok_GateRZZ:   return "RZZ";
        case MQL_Tok_GateRXX:   return "RXX";
        case MQL_Tok_GateRYY:   return "RYY";
        case MQL_Tok_GateCCX:   return "CCX";
        case MQL_Tok_GateCSWAP: return "CSWAP";
        case MQL_Tok_GateCNOT:  return "CNOT";
        case MQL_Tok_GateCZ:    return "CZ";
        
        case MQL_Tok_LParen:    return "(";
        case MQL_Tok_RParen:    return ")";
        case MQL_Tok_LBrace:    return "{";
        case MQL_Tok_RBrace:    return "}";
        case MQL_Tok_LBracket:  return "[";
        case MQL_Tok_RBracket:  return "]";
        case MQL_Tok_Semicolon: return ";";
        case MQL_Tok_Comma:     return ",";
        case MQL_Tok_Colon:     return ":";
        case MQL_Tok_Dot:       return ".";
        case MQL_Tok_Star:      return "*";
        case MQL_Tok_StarStar:  return "**";
        case MQL_Tok_Plus:      return "+";
        case MQL_Tok_Minus:     return "-";
        case MQL_Tok_Slash:     return "/";
        case MQL_Tok_Percent:   return "%";
        case MQL_Tok_Tilde:     return "~";
        case MQL_Tok_Pragma:    return "#pragma";
        case MQL_Tok_Eq:        return "=";
        case MQL_Tok_EqEq:      return "==";
        case MQL_Tok_Bang:      return "!";
        case MQL_Tok_BangEq:    return "!=";
        case MQL_Tok_Lt:        return "<";
        case MQL_Tok_LtEq:      return "<=";
        case MQL_Tok_Gt:        return ">";
        case MQL_Tok_GtEq:      return ">=";
        case MQL_Tok_AmpAmp:    return "&&";
        case MQL_Tok_PipePipe:  return "||";
        case MQL_Tok_Arrow:     return "->";
        case MQL_Tok_Amp:       return "&";
        case MQL_Tok_Pipe:      return "|";
        case MQL_Tok_Caret:     return "^";
        case MQL_Tok_LtLt:      return "<<";
        case MQL_Tok_GtGt:      return ">>";
        case MQL_Tok_PlusEq:    return "+=";
        case MQL_Tok_MinusEq:   return "-=";
        case MQL_Tok_StarEq:    return "*=";
        case MQL_Tok_SlashEq:   return "/=";
        case MQL_Tok_AmpEq:     return "&=";
        case MQL_Tok_PipeEq:    return "|=";
        case MQL_Tok_CaretEq:   return "^=";
        case MQL_Tok_LtLtEq:    return "<<=";
        case MQL_Tok_GtGtEq:    return ">>=";
    }
    return "<?>";
}

// ============================================================
// Parser
// ============================================================


// --------------------------------------------------------
// internals
// --------------------------------------------------------

static void p_error(MQL_Parser *p, MQL_Token t, const char *msg) {
    MQL_Error *e  = arena_alloc_zero(p->arena, sizeof(MQL_Error));
    e->message    = str_make(msg);
    e->line       = t.line;
    e->col        = t.col;
    e->next       = 0;
    if (!p->errors) p->errors = e;
    else            p->errors_tail->next = e;
    p->errors_tail = e;
    p->had_error   = 1;
}

static MQL_Token p_peek(MQL_Parser *p) {
    return mql_peek(&p->lex);
}

static MQL_Token p_next(MQL_Parser *p) {
    return mql_next(&p->lex);
}

// Consume the next token skipping comments, and assert it matches kind.
// Returns the consumed token (or an error token on mismatch).
static MQL_Token p_expect(MQL_Parser *p, MQL_TokenKind kind) {
    // Skip comments transparently
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    
    MQL_Token t = p_next(p);
    if (t.kind != kind) {
        // Build a simple error message using the static kind strings.
        // We use a scratch buffer on the stack; not worth arena-allocating.
        static char buf[128];
        snprintf(buf, sizeof(buf), "expected '%s', got '%s'",
                 mql_token_kind_str(kind), mql_token_kind_str(t.kind));
        p_error(p, t, buf);
    }
    return t;
}

// Peek skipping comments.
static MQL_Token p_peek_nc(MQL_Parser *p) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    return p_peek(p);
}

// Consume and return current token, skipping any comments first.
static MQL_Token p_advance(MQL_Parser *p) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    return p_next(p);
}

// Consume the next token if its kind matches; return 1 if consumed.
static b8 p_eat(MQL_Parser *p, MQL_TokenKind kind) {
    if (p_peek_nc(p).kind == kind) { p_advance(p); return 1; }
    return 0;
}

// Convert a string lexeme that may be a keyword to an interned string.
// For identifiers the lexeme is already a clean view; for keywords we
// use the same view since it points into the source buffer.
static string tok_str(MQL_Token t) {
    return t.lexeme;
}

// --------------------------------------------------------
// Forward declarations for recursive grammar rules
// --------------------------------------------------------

static MQ_Expr *parse_expr(MQL_Parser *p);
static MQ_Stmt *parse_stmt(MQL_Parser *p);
static MQ_Stmt *parse_block(MQL_Parser *p);

// --------------------------------------------------------
// Type parsing
// --------------------------------------------------------

static MQ_Type *parse_type(MQL_Parser *p) {
    MQL_Token t = p_advance(p);
    switch (t.kind) {
        case MQL_Tok_Void:  return mq_type_scalar(p->arena, MQ_Type_Void);
        case MQL_Tok_Bool:  return mq_type_scalar(p->arena, MQ_Type_Bool);
        case MQL_Tok_Float: return mq_type_scalar(p->arena, MQ_Type_Float);
        case MQL_Tok_Angle: return mq_type_scalar(p->arena, MQ_Type_Angle);
        
        case MQL_Tok_Int: {
            u32 width = 64;
            if (p_eat(p, MQL_Tok_Lt)) {
                MQL_Token w = p_expect(p, MQL_Tok_IntLit);
                width = (u32)strtol((char*)w.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_Gt);
            }
            return mq_type_int(p->arena, width);
        }
        
        case MQL_Tok_Bit: {
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token n = p_expect(p, MQL_Tok_IntLit);
                u32 sz = (u32)strtol((char*)n.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
                return mq_type_reg(p->arena, MQ_Type_BitReg, sz);
            }
            return mq_type_scalar(p->arena, MQ_Type_Bit);
        }
        
        case MQL_Tok_Qubit: {
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token n = p_expect(p, MQL_Tok_IntLit);
                u32 sz = (u32)strtol((char*)n.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
                return mq_type_reg(p->arena, MQ_Type_QubitReg, sz);
            }
            return mq_type_scalar(p->arena, MQ_Type_Qubit);
        }
        
        default:
        p_error(p, t, "expected type");
        return mq_type_scalar(p->arena, MQ_Type_Void);
    }
}

// --------------------------------------------------------
// Expression parsing  (Pratt / precedence climbing)
// --------------------------------------------------------

static int binop_prec(MQL_TokenKind k) {
    switch (k) {
        case MQL_Tok_PipePipe:          return 1;
        case MQL_Tok_AmpAmp:            return 2;
        case MQL_Tok_Pipe:              return 3;
        case MQL_Tok_Caret:             return 4;
        case MQL_Tok_Amp:               return 5;
        case MQL_Tok_EqEq:
        case MQL_Tok_BangEq:            return 6;
        case MQL_Tok_Lt: case MQL_Tok_LtEq:
        case MQL_Tok_Gt: case MQL_Tok_GtEq: return 7;
        case MQL_Tok_LtLt:
        case MQL_Tok_GtGt:              return 8;
        case MQL_Tok_Plus:
        case MQL_Tok_Minus:             return 9;
        case MQL_Tok_Star:
        case MQL_Tok_Slash:
        case MQL_Tok_Percent:           return 10;
        case MQL_Tok_StarStar:          return 11; // right-assoc; handled below
        default:                        return -1;
    }
}

static MQ_BinOp tok_to_binop(MQL_TokenKind k) {
    switch (k) {
        case MQL_Tok_Plus:      return MQ_BinOp_Add;
        case MQL_Tok_Minus:     return MQ_BinOp_Sub;
        case MQL_Tok_Star:      return MQ_BinOp_Mul;
        case MQL_Tok_Slash:     return MQ_BinOp_Div;
        case MQL_Tok_Percent:   return MQ_BinOp_Mod;
        case MQL_Tok_StarStar:  return MQ_BinOp_Pow;
        case MQL_Tok_Amp:       return MQ_BinOp_And;
        case MQL_Tok_Pipe:      return MQ_BinOp_Or;
        case MQL_Tok_Caret:     return MQ_BinOp_Xor;
        case MQL_Tok_LtLt:      return MQ_BinOp_Shl;
        case MQL_Tok_GtGt:      return MQ_BinOp_Shr;
        case MQL_Tok_EqEq:      return MQ_BinOp_Eq;
        case MQL_Tok_BangEq:    return MQ_BinOp_Ne;
        case MQL_Tok_Lt:        return MQ_BinOp_Lt;
        case MQL_Tok_LtEq:      return MQ_BinOp_Le;
        case MQL_Tok_Gt:        return MQ_BinOp_Gt;
        case MQL_Tok_GtEq:      return MQ_BinOp_Ge;
        case MQL_Tok_AmpAmp:    return MQ_BinOp_LogAnd;
        case MQL_Tok_PipePipe:  return MQ_BinOp_LogOr;
        default:                return MQ_BinOp_Add; // unreachable
    }
}

static MQ_UnOp tok_to_unop_fn(MQL_TokenKind k) {
    switch (k) {
        case MQL_Tok_Sin:  return MQ_UnOp_Sin;
        case MQL_Tok_Cos:  return MQ_UnOp_Cos;
        case MQL_Tok_Tan:  return MQ_UnOp_Tan;
        case MQL_Tok_Asin: return MQ_UnOp_Asin;
        case MQL_Tok_Acos: return MQ_UnOp_Acos;
        case MQL_Tok_Atan: return MQ_UnOp_Atan;
        case MQL_Tok_Sqrt: return MQ_UnOp_Sqrt;
        case MQL_Tok_Exp:  return MQ_UnOp_Exp;
        case MQL_Tok_Log:  return MQ_UnOp_Log;
        case MQL_Tok_Abs:  return MQ_UnOp_Abs;
        default:           return MQ_UnOp_Abs; // unreachable
    }
}

static b8 tok_is_math_fn(MQL_TokenKind k) {
    return k >= MQL_Tok_Sin && k <= MQL_Tok_Abs;
}

// Parse a comma-separated list of expressions into an arena-allocated array.
static MQ_Expr **parse_expr_list(MQL_Parser *p, u32 *out_count) {
    // Use a local linked list then flatten
    typedef struct ExprNode { MQ_Expr *e; struct ExprNode *next; } ExprNode;
    ExprNode *head = 0, *tail = 0;
    u32 count = 0;
    
    if (p_peek_nc(p).kind != MQL_Tok_RParen) {
        do {
            MQ_Expr *e = parse_expr(p);
            ExprNode *n = arena_alloc_zero(p->arena, sizeof(ExprNode));
            n->e = e;
            if (!head) head = n;
            else        tail->next = n;
            tail = n;
            count++;
        } while (p_eat(p, MQL_Tok_Comma));
    }
    
    *out_count = count;
    if (count == 0) return 0;
    
    MQ_Expr **arr = arena_alloc_array(p->arena, MQ_Expr *, count);
    ExprNode *cur = head;
    for (u32 i = 0; i < count; i++, cur = cur->next)
        arr[i] = cur->e;
    return arr;
}

// Parse a primary expression (no binary operators).
static MQ_Expr *parse_primary(MQL_Parser *p) {
    MQL_Token t = p_advance(p);
    
    switch (t.kind) {
        // Literals
        case MQL_Tok_IntLit:
        return mq_expr_int(p->arena, strtoll((char*)t.lexeme.str, 0, 10));
        
        case MQL_Tok_FloatLit:
        return mq_expr_float(p->arena, strtod((char*)t.lexeme.str, 0));
        
        case MQL_Tok_True:  return mq_expr_bool(p->arena, 1);
        case MQL_Tok_False: return mq_expr_bool(p->arena, 0);
        
        // Unary prefix operators
        case MQL_Tok_Minus: {
            MQ_Expr *operand = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_Neg, operand);
        }
        case MQL_Tok_Bang: {
            MQ_Expr *operand = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_Not, operand);
        }
        case MQL_Tok_Tilde: {
            MQ_Expr *operand = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_BitNot, operand);
        }
        
        // Math functions: sin(x), cos(x), ...
        case MQL_Tok_Sin: case MQL_Tok_Cos: case MQL_Tok_Tan:
        case MQL_Tok_Asin: case MQL_Tok_Acos: case MQL_Tok_Atan:
        case MQL_Tok_Sqrt: case MQL_Tok_Exp: case MQL_Tok_Log:
        case MQL_Tok_Abs: {
            MQ_UnOp op = tok_to_unop_fn(t.kind);
            p_expect(p, MQL_Tok_LParen);
            MQ_Expr *operand = parse_expr(p);
            p_expect(p, MQL_Tok_RParen);
            return mq_expr_unop(p->arena, op, operand);
        }
        
        // Grouped expression
        case MQL_Tok_LParen: {
            MQ_Expr *e = parse_expr(p);
            p_expect(p, MQL_Tok_RParen);
            return e;
        }
        
        // Array literal: [ expr, ... ]
        case MQL_Tok_LBracket: {
            u32 count = 0;
            MQ_Expr **elems = 0;
            if (p_peek_nc(p).kind != MQL_Tok_RBracket) {
                // reuse expr_list but with ] as terminator
                typedef struct EN { MQ_Expr *e; struct EN *next; } EN;
                EN *head = 0, *tail = 0;
                do {
                    MQ_Expr *e = parse_expr(p);
                    EN *n = arena_alloc_zero(p->arena, sizeof(EN));
                    n->e = e;
                    if (!head) head = n; else tail->next = n;
                    tail = n; count++;
                } while (p_eat(p, MQL_Tok_Comma));
                elems = arena_alloc_array(p->arena, MQ_Expr *, count);
                EN *c = head;
                for (u32 i = 0; i < count; i++, c = c->next) elems[i] = c->e;
            }
            p_expect(p, MQL_Tok_RBracket);
            return mq_expr_array(p->arena, elems, count);
        }
        
        // Identifier: variable, symbol, or function call
        case MQL_Tok_Ident: {
            string name = tok_str(t);
            // Function call
            if (p_peek_nc(p).kind == MQL_Tok_LParen) {
                p_advance(p);
                u32 argc = 0;
                MQ_Expr **args = parse_expr_list(p, &argc);
                p_expect(p, MQL_Tok_RParen);
                return mq_expr_call(p->arena, name, args, argc);
            }
            // Register index: name[expr]
            if (p_peek_nc(p).kind == MQL_Tok_LBracket) {
                p_advance(p);
                MQ_Expr *idx = parse_expr(p);
                p_expect(p, MQL_Tok_RBracket);
                return mq_expr_reg_index(p->arena, name, idx);
            }
            // Plain variable / symbolic param
            return mq_expr_var(p->arena, name);
        }
        
        // Keywords that can appear as names inside expressions (param names etc.)
        // Treat them as symbolic references so "theta" declared as param works.
        default:
        if (tok_is_math_fn(t.kind)) {
            // already handled above; fallthrough is unreachable
        }
        p_error(p, t, "expected expression");
        return mq_expr_int(p->arena, 0);
    }
}

// Precedence-climbing binary expression parser.
static MQ_Expr *parse_expr_prec(MQL_Parser *p, int min_prec) {
    MQ_Expr *lhs = parse_primary(p);
    
    for (;;) {
        MQL_TokenKind k = p_peek_nc(p).kind;
        int prec = binop_prec(k);
        if (prec < min_prec) break;
        
        p_advance(p);
        MQ_BinOp op = tok_to_binop(k);
        
        // '**' is right-associative: recurse at same precedence
        int next_prec = (k == MQL_Tok_StarStar) ? prec : prec + 1;
        MQ_Expr *rhs = parse_expr_prec(p, next_prec);
        lhs = mq_expr_binop(p->arena, op, lhs, rhs);
    }
    return lhs;
}

static MQ_Expr *parse_expr(MQL_Parser *p) {
    return parse_expr_prec(p, 0);
}

// --------------------------------------------------------
// Scope table helpers
// --------------------------------------------------------

static void scope_clear(MQL_Parser *p) {
    p->scope_count = 0;
}

static void scope_add(MQL_Parser *p, string name, MQL_SymKind kind,
                      u32 base_id, u32 size) {
    if (p->scope_count >= MQL_SCOPE_MAX) {
        fprintf(stderr, "mql: scope table overflow\n");
        return;
    }
    MQL_Symbol *s = &p->scope[p->scope_count++];
    s->name    = name;
    s->kind    = kind;
    s->base_id = base_id;
    s->size    = size;
}

static MQL_Symbol *scope_find(MQL_Parser *p, string name) {
    for (u32 i = 0; i < p->scope_count; i++)
        if (str_eq(p->scope[i].name, name))
        return &p->scope[i];
    return 0;
}


// --------------------------------------------------------
// Qubit argument  (e.g. q[0] or q)
// Returns an MQ_Expr_QubitRef with the flat id looked up from the name,
// or MQ_Expr_RegIndex if indexed.
// --------------------------------------------------------


static u32 parse_qubit_arg_id(MQL_Parser *p) {
    MQL_Token name_tok = p_expect(p, MQL_Tok_Ident);
    string    name     = tok_str(name_tok);
    u32       index    = 0;
    
    if (p_peek_nc(p).kind == MQL_Tok_LBracket) {
        p_advance(p);
        MQL_Token idx_tok = p_expect(p, MQL_Tok_IntLit);
        index = (u32)strtol((char*)idx_tok.lexeme.str, 0, 10);
        p_expect(p, MQL_Tok_RBracket);
    }
    
    MQL_Symbol *sym = scope_find(p, name);
    if (!sym) {
        p_error(p, name_tok, "unknown qubit name");
        return 0;
    }
    if (sym->kind != MQL_Sym_Qubit) {
        p_error(p, name_tok, "expected qubit, got classical register");
        return 0;
    }
    if (index >= sym->size) {
        p_error(p, name_tok, "qubit index out of range");
        return 0;
    }
    return sym->base_id + index;
}

// Parse a classical bit argument and return the flat bit id.
static u32 parse_cbit_arg_id(MQL_Parser *p) {
    MQL_Token name_tok = p_expect(p, MQL_Tok_Ident);
    string    name     = tok_str(name_tok);
    u32       index    = 0;
    
    if (p_peek_nc(p).kind == MQL_Tok_LBracket) {
        p_advance(p);
        MQL_Token idx_tok = p_expect(p, MQL_Tok_IntLit);
        index = (u32)strtol((char*)idx_tok.lexeme.str, 0, 10);
        p_expect(p, MQL_Tok_RBracket);
    }
    
    MQL_Symbol *sym = scope_find(p, name);
    if (!sym) {
        p_error(p, name_tok, "unknown bit name");
        return 0;
    }
    if (sym->kind != MQL_Sym_Bit) {
        p_error(p, name_tok, "expected classical bit, got qubit register");
        return 0;
    }
    if (index >= sym->size) {
        p_error(p, name_tok, "bit index out of range");
        return 0;
    }
    return sym->base_id + index;
}


// --------------------------------------------------------
// Gate instruction parsing
// --------------------------------------------------------

// Returns an MQ_GateType for built-in gate tokens; MQ_Gate_Custom otherwise.
static b8 tok_to_gate_type(MQL_TokenKind k, MQ_GateType *out) {
    switch (k) {
        case MQL_Tok_GateI:     *out = MQ_Gate_I;     return 1;
        case MQL_Tok_GateH:     *out = MQ_Gate_H;     return 1;
        case MQL_Tok_GateX:     *out = MQ_Gate_X;     return 1;
        case MQL_Tok_GateY:     *out = MQ_Gate_Y;     return 1;
        case MQL_Tok_GateZ:     *out = MQ_Gate_Z;     return 1;
        case MQL_Tok_GateS:     *out = MQ_Gate_S;     return 1;
        case MQL_Tok_GateSdg:   *out = MQ_Gate_Sdg;   return 1;
        case MQL_Tok_GateT:     *out = MQ_Gate_T;     return 1;
        case MQL_Tok_GateTdg:   *out = MQ_Gate_Tdg;   return 1;
        case MQL_Tok_GateP:     *out = MQ_Gate_P;     return 1;
        case MQL_Tok_GateRX:    *out = MQ_Gate_RX;    return 1;
        case MQL_Tok_GateRY:    *out = MQ_Gate_RY;    return 1;
        case MQL_Tok_GateRZ:    *out = MQ_Gate_RZ;    return 1;
        case MQL_Tok_GateU:     *out = MQ_Gate_U;     return 1;
        case MQL_Tok_GateSWAP:  *out = MQ_Gate_SWAP;  return 1;
        case MQL_Tok_GateISWAP: *out = MQ_Gate_ISWAP; return 1;
        case MQL_Tok_GateRZZ:   *out = MQ_Gate_RZZ;   return 1;
        case MQL_Tok_GateRXX:   *out = MQ_Gate_RXX;   return 1;
        case MQL_Tok_GateRYY:   *out = MQ_Gate_RYY;   return 1;
        case MQL_Tok_GateCCX:   *out = MQ_Gate_CCX;   return 1;
        case MQL_Tok_GateCSWAP: *out = MQ_Gate_CSWAP; return 1;
        default:                return 0;
    }
}

static MQ_Stmt *parse_gate_instr(MQL_Parser *p) {
    // Collect any ctrl / ctrl0 prefixes
    u32 ctrl_qubits[MQ_MAX_CONTROLS];
    u8  ctrl_states[MQ_MAX_CONTROLS];
    u8  ctrl_count = 0;
    
    while (1) {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        u8 state;
        if      (pk == MQL_Tok_Ctrl)  state = 1;
        else if (pk == MQL_Tok_Ctrl0) state = 0;
        else break;
        
        p_advance(p); // consume ctrl / ctrl0
        p_expect(p, MQL_Tok_LBracket);
        u32 cid = parse_qubit_arg_id(p);
        p_expect(p, MQL_Tok_RBracket);
        
        if (ctrl_count < MQ_MAX_CONTROLS) {
            ctrl_qubits[ctrl_count] = cid;
            ctrl_states[ctrl_count] = state;
            ctrl_count++;
        } else {
            MQL_Token err_tok = p_peek_nc(p);
            p_error(p, err_tok, "too many control qubits");
        }
    }
    
    // Gate name token
    MQL_Token gate_tok = p_advance(p);
    MQ_GateType gate_type;
    b8 is_builtin = tok_to_gate_type(gate_tok.kind, &gate_type);
    string custom_name = {0};
    
    // CNOT / CZ sugar  ->  ctrl[a] X/Z b
    b8 is_sugar_two_qubit = 0;
    if (gate_tok.kind == MQL_Tok_GateCNOT || gate_tok.kind == MQL_Tok_GateCZ) {
        is_sugar_two_qubit = 1;
        gate_type = (gate_tok.kind == MQL_Tok_GateCNOT) ? MQ_Gate_X : MQ_Gate_Z;
    }
    
    if (!is_builtin && !is_sugar_two_qubit) {
        if (gate_tok.kind == MQL_Tok_Ident) {
            gate_type   = MQ_Gate_Custom;
            custom_name = tok_str(gate_tok);
        } else {
            p_error(p, gate_tok, "expected gate name");
        }
    }
    
    // Optional parameter list
    MQ_Expr *param_exprs[MQ_MAX_GATE_PARAMS];
    u8 param_count = 0;
    b8 has_sym = 0;
    
    if (p_eat(p, MQL_Tok_LParen)) {
        while (p_peek_nc(p).kind != MQL_Tok_RParen &&
               p_peek_nc(p).kind != MQL_Tok_Eof) {
            MQ_Expr *e = parse_expr(p);
            if (param_count < MQ_MAX_GATE_PARAMS) {
                param_exprs[param_count++] = e;
                if (e->kind == MQ_Expr_Symbol || e->kind == MQ_Expr_Var ||
                    e->kind == MQ_Expr_BinOp  || e->kind == MQ_Expr_UnOp)
                    has_sym = 1;
            }
            if (!p_eat(p, MQL_Tok_Comma)) break;
        }
        p_expect(p, MQL_Tok_RParen);
    }
    
    // Qubit arguments
    u32 qubits[MQ_MAX_GATE_QUBITS];
    u8  qubit_count = 0;
    
    // CNOT/CZ sugar: first qubit -> second qubit
    if (is_sugar_two_qubit) {
        qubits[qubit_count++] = parse_qubit_arg_id(p);
        p_expect(p, MQL_Tok_Arrow);
        // The "control" qubit becomes ctrl[first], target is second
        // Shift the first into ctrl array and parse the target
        if (ctrl_count < MQ_MAX_CONTROLS) {
            ctrl_qubits[ctrl_count] = qubits[0];
            ctrl_states[ctrl_count] = 1;
            ctrl_count++;
        }
        qubit_count = 0;
        qubits[qubit_count++] = parse_qubit_arg_id(p);
    } else {
        // General qubit list: consume idents until we hit ';' or 'if'
        while (p_peek_nc(p).kind == MQL_Tok_Ident &&
               qubit_count < MQ_MAX_GATE_QUBITS) {
            qubits[qubit_count++] = parse_qubit_arg_id(p);
        }
    }
    
    // Build the instruction
    MQ_Instruction instr;
    if (param_count == 0) {
        instr = mq_instr_gate(gate_type, qubits, qubit_count);
    } else if (has_sym) {
        instr = mq_instr_gate_sym(gate_type, qubits, qubit_count,
                                  param_exprs, param_count);
    } else {
        f64 params[MQ_MAX_GATE_PARAMS];
        for (u8 i = 0; i < param_count; i++) {
            params[i] = (param_exprs[i]->kind == MQ_Expr_FloatLit)
                ? param_exprs[i]->lit.float_val
                : (f64)param_exprs[i]->lit.int_val;
        }
        instr = mq_instr_gate_p(gate_type, qubits, qubit_count,
                                params, param_count);
    }
    
    if (gate_type == MQ_Gate_Custom)
        instr.gate.custom_name = custom_name;
    
    // Apply collected control qubits
    for (u8 i = 0; i < ctrl_count; i++)
        mq_instr_add_control(&instr, ctrl_qubits[i], ctrl_states[i]);
    
    // Optional classical conditioning suffix: if name[n] == expr
    if (p_eat(p, MQL_Tok_If)) {
        MQL_Token reg = p_expect(p, MQL_Tok_Ident);
        (void)reg;
        p_expect(p, MQL_Tok_LBracket);
        MQL_Token idx = p_expect(p, MQL_Tok_IntLit);
        p_expect(p, MQL_Tok_RBracket);
        p_expect(p, MQL_Tok_EqEq);
        MQL_Token val = p_expect(p, MQL_Tok_IntLit);
        u32 bit_idx  = (u32)strtol((char*)idx.lexeme.str, 0, 10);
        u32 cond_val = (u32)strtol((char*)val.lexeme.str, 0, 10);
        mq_instr_set_classical_cond(&instr, bit_idx, cond_val);
    }
    
    p_expect(p, MQL_Tok_Semicolon);
    return mq_stmt_instr(p->arena, instr);
}

// --------------------------------------------------------
// Statement parsing
// --------------------------------------------------------

static MQ_Stmt *parse_block(MQL_Parser *p) {
    p_expect(p, MQL_Tok_LBrace);
    
    typedef struct SN { MQ_Stmt *s; struct SN *next; } SN;
    SN *head = 0, *tail = 0;
    u32 count = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RBrace &&
           p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQ_Stmt *s = parse_stmt(p);
        if (!s) continue;
        SN *n = arena_alloc_zero(p->arena, sizeof(SN));
        n->s = s;
        if (!head) head = n; else tail->next = n;
        tail = n;
        count++;
    }
    p_expect(p, MQL_Tok_RBrace);
    
    MQ_Stmt **arr = 0;
    if (count > 0) {
        arr = arena_alloc_array(p->arena, MQ_Stmt *, count);
        SN *c = head;
        for (u32 i = 0; i < count; i++, c = c->next) arr[i] = c->s;
    }
    return mq_stmt_block(p->arena, arr, count);
}

static MQ_Stmt *parse_stmt(MQL_Parser *p) {
    MQL_Token t = p_peek_nc(p);
    
    // Comment -> preserve
    if (t.kind == MQL_Tok_Comment) {
        p_next(p);
        return mq_stmt_comment(p->arena, tok_str(t));
    }
    
    // Pragma
    if (t.kind == MQL_Tok_Pragma) {
        p_advance(p);
        MQL_Token key = p_expect(p, MQL_Tok_Ident);
        MQL_Token val = p_expect(p, MQL_Tok_StringLit);
        return mq_stmt_pragma(p->arena, tok_str(key), tok_str(val));
    }
    
    // Adjoint block
    if (t.kind == MQL_Tok_Adjoint) {
        p_advance(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_adjoint(p->arena, body);
    }
    
    // Let declaration
    if (t.kind == MQL_Tok_Let) {
        p_advance(p);
        MQL_Token name = p_expect(p, MQL_Tok_Ident);
        p_expect(p, MQL_Tok_Colon);
        MQ_Type *ty = parse_type(p);
        MQ_Expr *init = 0;
        if (p_eat(p, MQL_Tok_Eq)) init = parse_expr(p);
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_decl_classical(p->arena, tok_str(name), ty, init);
    }
    
    // Param declaration (inside circuit body)
    if (t.kind == MQL_Tok_Param) {
        p_advance(p);
        MQL_Token name = p_expect(p, MQL_Tok_Ident);
        MQ_Expr *def = 0;
        if (p_eat(p, MQL_Tok_Eq)) def = parse_expr(p);
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_decl_param(p->arena, tok_str(name), def);
    }
    
    // Set / augmented assignment
    if (t.kind == MQL_Tok_Set) {
        p_advance(p);
        MQ_Expr *lhs = parse_expr(p);
        
        // Check for augmented operators
        MQL_TokenKind ak = p_peek_nc(p).kind;
        MQ_BinOp aug_op = MQ_BinOp_Add;
        b8 augmented = 0;
        switch (ak) {
            case MQL_Tok_PlusEq:  aug_op = MQ_BinOp_Add; augmented = 1; break;
            case MQL_Tok_MinusEq: aug_op = MQ_BinOp_Sub; augmented = 1; break;
            case MQL_Tok_StarEq:  aug_op = MQ_BinOp_Mul; augmented = 1; break;
            case MQL_Tok_SlashEq: aug_op = MQ_BinOp_Div; augmented = 1; break;
            case MQL_Tok_AmpEq:   aug_op = MQ_BinOp_And; augmented = 1; break;
            case MQL_Tok_PipeEq:  aug_op = MQ_BinOp_Or;  augmented = 1; break;
            case MQL_Tok_CaretEq: aug_op = MQ_BinOp_Xor; augmented = 1; break;
            case MQL_Tok_LtLtEq:  aug_op = MQ_BinOp_Shl; augmented = 1; break;
            case MQL_Tok_GtGtEq:  aug_op = MQ_BinOp_Shr; augmented = 1; break;
            default: break;
        }
        if (augmented) p_advance(p);
        else           p_expect(p, MQL_Tok_Eq);
        
        MQ_Expr *rhs = parse_expr(p);
        p_expect(p, MQL_Tok_Semicolon);
        if (augmented) return mq_stmt_set_aug(p->arena, lhs, aug_op, rhs);
        return mq_stmt_set(p->arena, lhs, rhs);
    }
    
    // If / elif / else
    if (t.kind == MQL_Tok_If) {
        p_advance(p);
        
        typedef struct BN { MQ_IfBranch b; struct BN *next; } BN;
        BN *head = 0, *tail = 0;
        u32 count = 0;
        
        // if branch
        MQ_Expr *cond = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        BN *n = arena_alloc_zero(p->arena, sizeof(BN));
        n->b = mq_if_branch(cond, body);
        head = tail = n; count++;
        
        // elif branches
        while (p_peek_nc(p).kind == MQL_Tok_Elif) {
            p_advance(p);
            cond = parse_expr(p);
            body = parse_block(p);
            n = arena_alloc_zero(p->arena, sizeof(BN));
            n->b = mq_if_branch(cond, body);
            tail->next = n; tail = n; count++;
        }
        
        // else branch
        if (p_eat(p, MQL_Tok_Else)) {
            body = parse_block(p);
            n = arena_alloc_zero(p->arena, sizeof(BN));
            n->b = mq_else_branch(body);
            tail->next = n; tail = n; count++;
        }
        
        MQ_IfBranch *branches = arena_alloc_array(p->arena, MQ_IfBranch, count);
        BN *c = head;
        for (u32 i = 0; i < count; i++, c = c->next) branches[i] = c->b;
        return mq_stmt_if(p->arena, branches, count);
    }
    
    // For loop
    if (t.kind == MQL_Tok_For) {
        p_advance(p);
        MQL_Token var = p_expect(p, MQL_Tok_Ident);
        MQ_Type *var_type = 0;
        if (p_eat(p, MQL_Tok_Colon)) var_type = parse_type(p);
        p_expect(p, MQL_Tok_In);
        MQ_Expr *iter = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_for(p->arena, tok_str(var), var_type, iter, body);
    }
    
    // While loop
    if (t.kind == MQL_Tok_While) {
        p_advance(p);
        MQ_Expr *cond = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_while(p->arena, cond, body);
    }
    
    // Break / continue / return
    if (t.kind == MQL_Tok_Break) {
        p_advance(p); p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_break(p->arena);
    }
    if (t.kind == MQL_Tok_Continue) {
        p_advance(p); p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_continue(p->arena);
    }
    if (t.kind == MQL_Tok_Return) {
        p_advance(p);
        MQ_Expr *val = 0;
        if (p_peek_nc(p).kind != MQL_Tok_Semicolon) val = parse_expr(p);
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_return(p->arena, val);
    }
    
    // Measure
    if (t.kind == MQL_Tok_Measure) {
        p_advance(p);
        u32 src_q = parse_qubit_arg_id(p);
        MQ_Instruction instr;
        if (p_eat(p, MQL_Tok_Arrow)) {
            u32 bit_id = parse_cbit_arg_id(p);
            instr = mq_instr_measure(src_q, bit_id);
        } else {
            instr = mq_instr_measure_discard(src_q);
        }
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, instr);
    }
    
    // Reset
    if (t.kind == MQL_Tok_Reset) {
        p_advance(p);
        u32 qid = parse_qubit_arg_id(p);
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, mq_instr_reset(qid));
    }
    
    // Barrier
    if (t.kind == MQL_Tok_Barrier) {
        p_advance(p);
        u32 qubits[MQ_MAX_GATE_QUBITS];
        u8  qcount = 0;
        
        if (p_peek_nc(p).kind == MQL_Tok_Star) {
            // barrier *  -- all qubits; emit an empty barrier (backend expands)
            p_advance(p);
        } else {
            do {
                if (qcount < MQ_MAX_GATE_QUBITS)
                    qubits[qcount++] = parse_qubit_arg_id(p);
            } while (p_eat(p, MQL_Tok_Comma));
        }
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, mq_instr_barrier(qubits, qcount));
    }
    
    // Delay
    if (t.kind == MQL_Tok_Delay) {
        p_advance(p);
        MQL_Token time_tok = p_expect(p, MQL_Tok_TimeLit);
        
        // Split the time literal into value and unit suffix.
        // Lexeme is e.g. "100ns" or "1.5us".
        string lex = time_tok.lexeme;
        // Find where the numeric part ends
        u64 num_end = 0;
        while (num_end < lex.size &&
               (is_digit(lex.str[num_end]) || lex.str[num_end] == '.' ||
                lex.str[num_end] == 'e'    || lex.str[num_end] == 'E' ||
                ((lex.str[num_end] == '+' || lex.str[num_end] == '-') && num_end > 0)))
            num_end++;
        
        // strtod on a non-null-terminated buffer: copy to small stack buffer
        char num_buf[64] = {0};
        u64 copy_len = num_end < 63 ? num_end : 63;
        memcpy(num_buf, lex.str, copy_len);
        f64 duration = strtod(num_buf, 0);
        
        string unit_str = { .str = lex.str + num_end, .size = lex.size - num_end };
        MQ_TimeUnit unit = MQ_Time_ns; // default
        if      (lexeme_eq_cstr(unit_str, "dt")) unit = MQ_Time_Dt;
        else if (lexeme_eq_cstr(unit_str, "ns")) unit = MQ_Time_ns;
        else if (lexeme_eq_cstr(unit_str, "us")) unit = MQ_Time_us;
        else if (lexeme_eq_cstr(unit_str, "ms")) unit = MQ_Time_ms;
        else if (lexeme_eq_cstr(unit_str, "s"))  unit = MQ_Time_s;
        
        u32 qubits[MQ_MAX_GATE_QUBITS];
        u8  qcount = 0;
        do {
            if (qcount < MQ_MAX_GATE_QUBITS)
                qubits[qcount++] = parse_qubit_arg_id(p);
        } while (p_eat(p, MQL_Tok_Comma));
        
        p_expect(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, mq_instr_delay(qubits, qcount, duration, unit));
    }
    
    // Subroutine call:  ident ( args ) ;
    if (t.kind == MQL_Tok_Ident) {
        // Disambiguate: if the ident is followed by '(' it's a call stmt,
        // otherwise it might be a gate (user-defined) -- gate parser handles that.
        if (p_peek_nc(p).kind == MQL_Tok_Ident) {
            MQL_Token lookahead_after = t; // re-peek done below
        }
        // Peek two ahead: ident then '('
        p_advance(p); // consume ident
        if (p_peek_nc(p).kind == MQL_Tok_LParen) {
            p_advance(p); // consume '('
            u32 argc = 0;
            MQ_Expr **args = parse_expr_list(p, &argc);
            p_expect(p, MQL_Tok_RParen);
            p_expect(p, MQL_Tok_Semicolon);
            return mq_stmt_call(p->arena, tok_str(t), args, argc);
        }
        // Not a call: back-track by re-injecting the consumed ident as peeked.
        // We can't truly un-advance, so we rebuild the token and set peeked.
        // This works because gate parsing only needs the first token.
        p->lex.peeked     = (MQL_Token){ .kind = MQL_Tok_Ident, .lexeme = t.lexeme,
            .line = t.line, .col = t.col };
        p->lex.has_peeked = 1;
        // Fall through to gate parsing
    }
    
    // Gate instructions (built-in names, ctrl, ctrl0, or user ident falling through)
    {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        MQ_GateType dummy;
        if (pk == MQL_Tok_Ctrl   || pk == MQL_Tok_Ctrl0    ||
            pk == MQL_Tok_GateCNOT || pk == MQL_Tok_GateCZ ||
            tok_to_gate_type(pk, &dummy) ||
            pk == MQL_Tok_Ident) {
            return parse_gate_instr(p);
        }
    }
    
    // Unknown token; skip and report error
    p_advance(p);
    p_error(p, t, "unexpected token in statement");
    return 0;
}


// --------------------------------------------------------
// Formal parameter list  (for gate/operation defs)
// --------------------------------------------------------

static void parse_formal_list(MQL_Parser *p,
                              MQ_FormalParam **out_params, u32 *out_count) {
    typedef struct FN { MQ_FormalParam fp; struct FN *next; } FN;
    FN *head = 0, *tail = 0;
    u32 count = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RParen &&
           p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQL_Token name = p_expect(p, MQL_Tok_Ident);
        p_expect(p, MQL_Tok_Colon);
        MQ_Type *ty = parse_type(p);
        MQ_Expr *def = 0;
        if (p_eat(p, MQL_Tok_Eq)) def = parse_expr(p);
        
        FN *n = arena_alloc_zero(p->arena, sizeof(FN));
        n->fp = mq_formal_param(tok_str(name), ty, def);
        if (!head) head = n; else tail->next = n;
        tail = n; count++;
        
        if (!p_eat(p, MQL_Tok_Comma)) break;
    }
    
    *out_count = count;
    if (count == 0) { *out_params = 0; return; }
    MQ_FormalParam *arr = arena_alloc_array(p->arena, MQ_FormalParam, count);
    FN *c = head;
    for (u32 i = 0; i < count; i++, c = c->next) arr[i] = c->fp;
    *out_params = arr;
}

// --------------------------------------------------------
// Gate definition
// --------------------------------------------------------

static void scope_add_formal_params(MQL_Parser *p,
                                    MQ_FormalParam *params, u32 count) {
    u32 next_q = 0;
    u32 next_c = 0;
    for (u32 i = 0; i < count; i++) {
        MQ_Type *ty = params[i].type;
        if (!ty) continue;
        if (ty->kind == MQ_Type_Qubit) {
            scope_add(p, params[i].name, MQL_Sym_Qubit, next_q, 1);
            next_q++;
        } else if (ty->kind == MQ_Type_QubitReg) {
            u32 w = ty->width ? ty->width : 1;
            scope_add(p, params[i].name, MQL_Sym_Qubit, next_q, w);
            next_q += w;
        } else if (ty->kind == MQ_Type_Bit) {
            scope_add(p, params[i].name, MQL_Sym_Bit, next_c, 1);
            next_c++;
        } else if (ty->kind == MQ_Type_BitReg) {
            u32 w = ty->width ? ty->width : 1;
            scope_add(p, params[i].name, MQL_Sym_Bit, next_c, w);
            next_c += w;
        }
    }
}

static MQ_Routine *parse_gate_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Gate);
    MQL_Token name = p_expect(p, MQL_Tok_Ident);
    p_expect(p, MQL_Tok_LParen);
    MQ_FormalParam *params = 0;
    u32 param_count = 0;
    parse_formal_list(p, &params, &param_count);
    p_expect(p, MQL_Tok_RParen);
    scope_clear(p);
    scope_add_formal_params(p, params, param_count);
    MQ_Stmt *body = parse_block(p);
    return mq_routine(p->arena, tok_str(name), MQ_Routine_Gate,
                      params, param_count, 0, body);
}


// --------------------------------------------------------
// Operation definition
// --------------------------------------------------------


static MQ_Routine *parse_operation_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Operation);
    MQL_Token name = p_expect(p, MQL_Tok_Ident);
    p_expect(p, MQL_Tok_LParen);
    MQ_FormalParam *params = 0;
    u32 param_count = 0;
    parse_formal_list(p, &params, &param_count);
    p_expect(p, MQL_Tok_RParen);
    MQ_Type *ret = 0;
    if (p_eat(p, MQL_Tok_Arrow)) ret = parse_type(p);
    scope_clear(p);
    scope_add_formal_params(p, params, param_count);
    MQ_Stmt *body = parse_block(p);
    return mq_routine(p->arena, tok_str(name), MQ_Routine_Operation,
                      params, param_count, ret, body);
}


// --------------------------------------------------------
// Circuit definition
// --------------------------------------------------------

static MQ_Circuit *parse_circuit_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Circuit);
    MQL_Token name = p_expect(p, MQL_Tok_Ident);
    
    // Optional circuit-level parameter list: circuit foo(theta: angle)
    if (p_eat(p, MQL_Tok_LParen)) {
        // These are registered as symbolic params, not formal params.
        // We just consume them here; a full implementation would pre-register.
        while (p_peek_nc(p).kind != MQL_Tok_RParen &&
               p_peek_nc(p).kind != MQL_Tok_Eof) {
            p_expect(p, MQL_Tok_Ident);
            p_expect(p, MQL_Tok_Colon);
            parse_type(p);
            if (p_eat(p, MQL_Tok_Eq)) parse_expr(p);
            if (!p_eat(p, MQL_Tok_Comma)) break;
        }
        p_expect(p, MQL_Tok_RParen);
    }
    
    p_expect(p, MQL_Tok_LBrace);
    
    // Clear scope for this circuit
    scope_clear(p);
    
    // --- Declarations section ---
    typedef struct RegNode { MQ_Register r; struct RegNode *next; } RegNode;
    RegNode *reg_head = 0, *reg_tail = 0;
    u32 reg_count = 0;
    u32 total_qubits = 0, total_cbits = 0;
    
    typedef struct ParamNode { string name; MQ_Expr *def; struct ParamNode *next; } ParamNode;
    ParamNode *pn_head = 0, *pn_tail = 0;
    u32 param_count = 0;
    
    // Collect leading declarations
    while (1) {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        
        if (pk == MQL_Tok_Qubit) {
            p_advance(p);
            MQL_Token rname = p_expect(p, MQL_Tok_Ident);
            u32 size = 1;
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token sz = p_expect(p, MQL_Tok_IntLit);
                size = (u32)strtol((char*)sz.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
            }
            p_expect(p, MQL_Tok_Semicolon);
            
            MQ_QubitMeta *meta = arena_alloc_array(p->arena, MQ_QubitMeta, size);
            for (u32 i = 0; i < size; i++) {
                meta[i].id             = total_qubits + i;
                meta[i].style          = MQ_Qubit_Flat;
                meta[i].register_name  = tok_str(rname);
                meta[i].register_index = i;
            }
            MQ_Register r = mq_register_quantum(tok_str(rname), size, total_qubits, meta);
            scope_add(p, tok_str(rname), MQL_Sym_Qubit, total_qubits, size);
            total_qubits += size;
            
            RegNode *n = arena_alloc_zero(p->arena, sizeof(RegNode));
            n->r = r;
            if (!reg_head) reg_head = n; else reg_tail->next = n;
            reg_tail = n; reg_count++;
            continue;
        }
        
        if (pk == MQL_Tok_Bit) {
            p_advance(p);
            MQL_Token rname = p_expect(p, MQL_Tok_Ident);
            u32 size = 1;
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token sz = p_expect(p, MQL_Tok_IntLit);
                size = (u32)strtol((char*)sz.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
            }
            p_expect(p, MQL_Tok_Semicolon);
            
            MQ_Register r = mq_register_classical(tok_str(rname), size, total_cbits);
            scope_add(p, tok_str(rname), MQL_Sym_Bit, total_cbits, size);
            total_cbits += size;
            
            RegNode *n = arena_alloc_zero(p->arena, sizeof(RegNode));
            n->r = r;
            if (!reg_head) reg_head = n; else reg_tail->next = n;
            reg_tail = n; reg_count++;
            continue;
        }
        
        if (pk == MQL_Tok_Param) {
            p_advance(p);
            MQL_Token pname = p_expect(p, MQL_Tok_Ident);
            if (p_peek_nc(p).kind == MQL_Tok_Colon) {
                p_advance(p);
                parse_type(p); // consume type annotation; not stored
            }
            MQ_Expr *def = 0;
            if (p_eat(p, MQL_Tok_Eq)) def = parse_expr(p);
            p_expect(p, MQL_Tok_Semicolon);
            
            ParamNode *pn = arena_alloc_zero(p->arena, sizeof(ParamNode));
            pn->name = tok_str(pname);
            pn->def  = def;
            if (!pn_head) pn_head = pn; else pn_tail->next = pn;
            pn_tail = pn; param_count++;
            continue;
        }
        
        break; // no more declarations
    }
    
    // Build flat register array
    MQ_Register *regs = 0;
    if (reg_count > 0) {
        regs = arena_alloc_array(p->arena, MQ_Register, reg_count);
        RegNode *c = reg_head;
        for (u32 i = 0; i < reg_count; i++, c = c->next) regs[i] = c->r;
    }
    
    MQ_Circuit *circuit = mq_circuit(p->arena, tok_str(name),
                                     regs, reg_count, total_qubits, total_cbits);
    
    // Register symbolic params
    ParamNode *pnc = pn_head;
    for (u32 i = 0; i < param_count; i++, pnc = pnc->next)
        mq_circuit_add_param(p->arena, circuit, pnc->name, pnc->def);
    
    // --- Statement body ---
    typedef struct SN { MQ_Stmt *s; struct SN *next; } SN;
    SN *shead = 0, *stail = 0;
    u32 scount = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RBrace &&
           p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQ_Stmt *s = parse_stmt(p);
        if (!s) continue;
        SN *n = arena_alloc_zero(p->arena, sizeof(SN));
        n->s = s;
        if (!shead) shead = n; else stail->next = n;
        stail = n; scount++;
    }
    p_expect(p, MQL_Tok_RBrace);
    
    if (scount > 0) {
        MQ_Stmt **arr = arena_alloc_array(p->arena, MQ_Stmt *, scount);
        SN *c = shead;
        for (u32 i = 0; i < scount; i++, c = c->next) arr[i] = c->s;
        circuit->body = mq_stmt_block(p->arena, arr, scount);
    }
    
    return circuit;
}

// --------------------------------------------------------
// Public API
// --------------------------------------------------------

void mql_parser_init(MQL_Parser *p, M_Arena *arena, string src) {
    memset(p, 0, sizeof(*p));
    p->arena = arena;
    mql_lexer_init(&p->lex, src);
}

MQ_Program *mql_parse_program(MQL_Parser *p) {
    MQ_Program *prog = mq_program(p->arena, str_lit("program"), MQ_Lang_MQ);
    
    
    while (p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQL_Token t = p_peek_nc(p);
        
        if (t.kind == MQL_Tok_Gate) {
            MQ_Routine *r = parse_gate_def(p);
            mq_program_add_routine(p->arena, prog, r);
        } else if (t.kind == MQL_Tok_Operation) {
            MQ_Routine *r = parse_operation_def(p);
            mq_program_add_routine(p->arena, prog, r);
        } else if (t.kind == MQL_Tok_Circuit) {
            MQ_Circuit *c = parse_circuit_def(p);
            mq_program_add_circuit(p->arena, prog, c);
        } else if (t.kind == MQL_Tok_Comment) {
            p_next(p); // discard top-level comments
        } else if (t.kind == MQL_Tok_Pragma) {
            // Top-level pragmas: consume key and value, ignore for now
            p_advance(p);
            if (p_peek_nc(p).kind == MQL_Tok_Ident)  p_advance(p);
            if (p_peek_nc(p).kind == MQL_Tok_StringLit) p_advance(p);
        } else {
            p_advance(p);
            p_error(p, t, "expected 'gate', 'operation', or 'circuit'");
            if (p->had_error) break;
        }
    }
    
    return p->had_error ? 0 : prog;
}


// ============================================================
// Emitter
// ============================================================


typedef struct EmitCtx {
    FILE               *f;
    M_Arena            *arena;
    int                 indent;
    MQ_QubitNameTable   qnames;
} EmitCtx;

static void ec_indent(EmitCtx *ec) {
    for (int i = 0; i < ec->indent; i++) fprintf(ec->f, "    ");
}

static void ec_str(EmitCtx *ec, string s) {
    if (!str_is_null(s)) fprintf(ec->f, "%.*s", str_expand(s));
}

static void emit_type(EmitCtx *ec, MQ_Type *t) {
    if (!t) { fprintf(ec->f, "void"); return; }
    switch (t->kind) {
        case MQ_Type_Void:     fprintf(ec->f, "void");           break;
        case MQ_Type_Bool:     fprintf(ec->f, "bool");           break;
        case MQ_Type_Float:    fprintf(ec->f, "float");          break;
        case MQ_Type_Angle:    fprintf(ec->f, "angle");          break;
        case MQ_Type_Bit:      fprintf(ec->f, "bit");            break;
        case MQ_Type_Qubit:    fprintf(ec->f, "qubit");          break;
        case MQ_Type_Int:
        if (t->width && t->width != 64) fprintf(ec->f, "int<%u>", t->width);
        else                            fprintf(ec->f, "int");
        break;
        case MQ_Type_BitReg:   fprintf(ec->f, "bit[%u]",   t->width); break;
        case MQ_Type_QubitReg: fprintf(ec->f, "qubit[%u]", t->width); break;
        case MQ_Type_Array:
        emit_type(ec, t->element_type);
        fprintf(ec->f, "[%u]", t->width);
        break;
    }
}

static void emit_expr(EmitCtx *ec, MQ_Expr *e);

static const char *binop_sym_mql[] = {
    "+", "-", "*", "/", "%", "**",
    "&", "|", "^", "<<", ">>",
    "==", "!=", "<", "<=", ">", ">=",
    "&&", "||"
};

static const char *unop_fn_mql[] = {
    0, 0, 0,   // Neg Not BitNot handled inline
    "sin", "cos", "tan", "asin", "acos", "atan",
    "sqrt", "exp", "log", "abs"
};

static void emit_expr(EmitCtx *ec, MQ_Expr *e) {
    if (!e) { fprintf(ec->f, "0"); return; }
    
    switch (e->kind) {
        case MQ_Expr_BoolLit:
        fprintf(ec->f, "%s", e->lit.bool_val ? "true" : "false");
        break;
        case MQ_Expr_IntLit:
        fprintf(ec->f, "%lld", (long long)e->lit.int_val);
        break;
        case MQ_Expr_FloatLit:
        // Use enough precision that parsing back gives the same value
        fprintf(ec->f, "%.17g", e->lit.float_val);
        break;
        case MQ_Expr_Symbol:
        case MQ_Expr_Var:
        /* Normalize all pi variants to MQL's "PI" keyword.
         * Note: MQL strings are non-null-terminated source slices,
         * so we must use size + strncmp, not strcmp. */
        if (e->name.str) {
            u64 sz = e->name.size;
            const char *n = (const char *)e->name.str;
            if ((sz == 2 && strncmp(n, "pi", 2) == 0) ||
                (sz == 2 && strncmp(n, "PI", 2) == 0) ||
                (sz == 7 && strncmp(n, "math.pi", 7) == 0) ||
                (sz == 5 && strncmp(n, "np.pi", 5) == 0)) {
                fprintf(ec->f, "PI");
                break;
            }
        }
        ec_str(ec, e->name);
        break;
        case MQ_Expr_QubitRef: {
            string n = mq_qubit_name(&ec->qnames, e->qubit_id);
            if (!str_is_null(n)) ec_str(ec, n);
            else                 fprintf(ec->f, "q[%u]", e->qubit_id);
            break;
        }
        case MQ_Expr_RegIndex:
        ec_str(ec, e->reg.name);
        fprintf(ec->f, "[");
        emit_expr(ec, e->reg.index_expr);
        fprintf(ec->f, "]");
        break;
        case MQ_Expr_BinOp:
        fprintf(ec->f, "(");
        emit_expr(ec, e->bin.lhs);
        fprintf(ec->f, " %s ", binop_sym_mql[e->bin.op]);
        emit_expr(ec, e->bin.rhs);
        fprintf(ec->f, ")");
        break;
        case MQ_Expr_UnOp:
        if (e->un.op == MQ_UnOp_Neg) {
            fprintf(ec->f, "-("); emit_expr(ec, e->un.operand); fprintf(ec->f, ")");
        } else if (e->un.op == MQ_UnOp_Not) {
            fprintf(ec->f, "!("); emit_expr(ec, e->un.operand); fprintf(ec->f, ")");
        } else if (e->un.op == MQ_UnOp_BitNot) {
            fprintf(ec->f, "~("); emit_expr(ec, e->un.operand); fprintf(ec->f, ")");
        } else {
            fprintf(ec->f, "%s(", unop_fn_mql[e->un.op]);
            emit_expr(ec, e->un.operand);
            fprintf(ec->f, ")");
        }
        break;
        case MQ_Expr_Call:
        case MQ_Expr_Array:
        /* Intercept PI() calls (from Q# IR) → bare PI */
        if (e->kind == MQ_Expr_Call && e->call.arg_count == 0 &&
            e->call.name.str && e->call.name.size == 2 &&
            strncmp((char *)e->call.name.str, "PI", 2) == 0) {
            fprintf(ec->f, "PI");
            break;
        }
        if (!str_is_null(e->call.name)) ec_str(ec, e->call.name);
        else                            fprintf(ec->f, "[");
        if (e->kind == MQ_Expr_Call) fprintf(ec->f, "(");
        for (u32 i = 0; i < e->call.arg_count; i++) {
            if (i) fprintf(ec->f, ", ");
            emit_expr(ec, e->call.args[i]);
        }
        if (e->kind == MQ_Expr_Call)  fprintf(ec->f, ")");
        else                          fprintf(ec->f, "]");
        break;
        case MQ_Expr_BitRead:
        ec_str(ec, e->bit.reg_name);
        fprintf(ec->f, "[%u]", e->bit.index);
        break;
    }
}

static void emit_qubit(EmitCtx *ec, u32 qubit_id) {
    string n = mq_qubit_name(&ec->qnames, qubit_id);
    if (!str_is_null(n)) ec_str(ec, n);
    else                 fprintf(ec->f, "q[%u]", qubit_id);
}

static const char *gate_name_mql[] = {
    "I", "H", "X", "Y", "Z", "S", "Sdg", "T", "Tdg",
    "P", "RX", "RY", "RZ", "U",
    "CNOT", "SWAP", "ISWAP", "RZZ", "RXX", "RYY",
    "CCX", "CSWAP",
    0, 0   // Custom and Unitary handled separately
};

static void emit_instr(EmitCtx *ec, MQ_Instruction *instr) {
    ec_indent(ec);
    
    switch (instr->type) {
        case MQ_Instr_Gate: {
            // Control prefixes
            for (u8 i = 0; i < instr->gate.control_count; i++) {
                fprintf(ec->f, instr->gate.control_states[i] ? "ctrl" : "ctrl0");
                fprintf(ec->f, "[");
                emit_qubit(ec, instr->gate.controls[i]);
                fprintf(ec->f, "] ");
            }
            
            // Gate name
            if (instr->gate.gate == MQ_Gate_Custom) {
                ec_str(ec, instr->gate.custom_name);
            } else if (instr->gate.gate == MQ_Gate_Unitary) {
                // Unitary matrices have no source representation; emit as a comment
                // and fall back to a custom name placeholder.
                fprintf(ec->f, "unitary_gate /* unitary matrix cannot be round-tripped */");
            } else {
                fprintf(ec->f, "%s", gate_name_mql[instr->gate.gate]);
            }
            
            // Parameters
            if (instr->gate.param_count > 0) {
                fprintf(ec->f, "(");
                for (u8 i = 0; i < instr->gate.param_count; i++) {
                    if (i) fprintf(ec->f, ", ");
                    if (instr->gate.params_symbolic)
                        emit_expr(ec, instr->gate.param_exprs[i]);
                    else
                        fprintf(ec->f, "%.17g", instr->gate.params[i]);
                }
                fprintf(ec->f, ")");
            }
            
            // Qubit arguments
            for (u8 i = 0; i < instr->qubit_count; i++) {
                fprintf(ec->f, " ");
                emit_qubit(ec, instr->qubits[i]);
            }
            
            // Hardware classical conditioning suffix
            if (instr->gate.has_classical_cond) {
                fprintf(ec->f, " if c[%u] == %u",
                        instr->gate.classical_bit,
                        instr->gate.classical_val);
            }
            
            fprintf(ec->f, ";\n");
            break;
        }
        
        case MQ_Instr_Measure:
        fprintf(ec->f, "measure ");
        emit_qubit(ec, instr->qubits[0]);
        if (instr->measure.has_target)
            fprintf(ec->f, " -> c[%u]", instr->measure.clbit);
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Instr_Reset:
        fprintf(ec->f, "reset ");
        emit_qubit(ec, instr->qubits[0]);
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Instr_Barrier:
        fprintf(ec->f, "barrier");
        if (instr->qubit_count == 0) {
            fprintf(ec->f, " *");
        } else {
            for (u8 i = 0; i < instr->qubit_count; i++) {
                fprintf(ec->f, i ? ", " : " ");
                emit_qubit(ec, instr->qubits[i]);
            }
        }
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Instr_Delay: {
            static const char *unit_str[] = { "dt", "ns", "us", "ms", "s" };
            fprintf(ec->f, "delay %.17g%s",
                    instr->delay.duration, unit_str[instr->delay.unit]);
            for (u8 i = 0; i < instr->qubit_count; i++) {
                fprintf(ec->f, i ? ", " : " ");
                emit_qubit(ec, instr->qubits[i]);
            }
            fprintf(ec->f, ";\n");
            break;
        }
    }
}

static void emit_stmt(EmitCtx *ec, MQ_Stmt *s);

static void emit_block_body(EmitCtx *ec, MQ_Stmt *s) {
    // s is expected to be a MQ_Stmt_Block; emit its children indented
    if (!s) return;
    if (s->kind == MQ_Stmt_Block) {
        for (u32 i = 0; i < s->block.count; i++)
            emit_stmt(ec, s->block.stmts[i]);
    } else {
        emit_stmt(ec, s);
    }
}

static void emit_block(EmitCtx *ec, MQ_Stmt *s) {
    fprintf(ec->f, " {\n");
    ec->indent++;
    emit_block_body(ec, s);
    ec->indent--;
    ec_indent(ec);
    fprintf(ec->f, "}");
}

static void emit_stmt(EmitCtx *ec, MQ_Stmt *s) {
    if (!s) return;
    
    switch (s->kind) {
        case MQ_Stmt_Block:
        for (u32 i = 0; i < s->block.count; i++)
            emit_stmt(ec, s->block.stmts[i]);
        break;
        
        case MQ_Stmt_Instr:
        emit_instr(ec, &s->instr);
        break;
        
        case MQ_Stmt_Adjoint:
        ec_indent(ec);
        fprintf(ec->f, "adjoint");
        emit_block(ec, s->adjoint_body);
        fprintf(ec->f, "\n");
        break;
        
        case MQ_Stmt_DeclQubit:
        // Inside a circuit body, qubit decls were already emitted in the
        // header section.  Inside an operation body they may appear mid-scope.
        ec_indent(ec);
        fprintf(ec->f, "qubit ");
        ec_str(ec, s->decl_qubit.name);
        if (s->decl_qubit.count > 1) fprintf(ec->f, "[%u]", s->decl_qubit.count);
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Stmt_DeclClassical:
        ec_indent(ec);
        fprintf(ec->f, "let ");
        ec_str(ec, s->decl_classical.name);
        fprintf(ec->f, " : ");
        emit_type(ec, s->decl_classical.type);
        if (s->decl_classical.init) {
            fprintf(ec->f, " = ");
            emit_expr(ec, s->decl_classical.init);
        }
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Stmt_DeclParam:
        ec_indent(ec);
        fprintf(ec->f, "param ");
        ec_str(ec, s->decl_param.name);
        if (s->decl_param.default_val) {
            fprintf(ec->f, " = ");
            emit_expr(ec, s->decl_param.default_val);
        }
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Stmt_Set: {
            ec_indent(ec);
            fprintf(ec->f, "set ");
            emit_expr(ec, s->set.lhs);
            if (s->set.augmented) {
                fprintf(ec->f, " %s= ", binop_sym_mql[s->set.aug_op]);
            } else {
                fprintf(ec->f, " = ");
            }
            emit_expr(ec, s->set.rhs);
            fprintf(ec->f, ";\n");
            break;
        }
        
        case MQ_Stmt_If:
        for (u32 i = 0; i < s->if_stmt.count; i++) {
            MQ_IfBranch *br = &s->if_stmt.branches[i];
            ec_indent(ec);
            if (!br->cond) {
                fprintf(ec->f, "else");
            } else {
                fprintf(ec->f, i == 0 ? "if " : "elif ");
                emit_expr(ec, br->cond);
            }
            emit_block(ec, br->body);
            fprintf(ec->f, "\n");
        }
        break;
        
        case MQ_Stmt_For:
        ec_indent(ec);
        fprintf(ec->f, "for ");
        ec_str(ec, s->for_loop.var_name);
        if (s->for_loop.var_type) {
            fprintf(ec->f, " : ");
            emit_type(ec, s->for_loop.var_type);
        }
        fprintf(ec->f, " in ");
        emit_expr(ec, s->for_loop.iterable);
        emit_block(ec, s->for_loop.body);
        fprintf(ec->f, "\n");
        break;
        
        case MQ_Stmt_While:
        ec_indent(ec);
        fprintf(ec->f, "while ");
        emit_expr(ec, s->while_loop.cond);
        emit_block(ec, s->while_loop.body);
        fprintf(ec->f, "\n");
        break;
        
        case MQ_Stmt_Break:
        ec_indent(ec); fprintf(ec->f, "break;\n");
        break;
        
        case MQ_Stmt_Continue:
        ec_indent(ec); fprintf(ec->f, "continue;\n");
        break;
        
        case MQ_Stmt_Return:
        ec_indent(ec);
        fprintf(ec->f, "return");
        if (s->return_val) { fprintf(ec->f, " "); emit_expr(ec, s->return_val); }
        fprintf(ec->f, ";\n");
        break;
        
        case MQ_Stmt_Call:
        ec_indent(ec);
        ec_str(ec, s->call.callee);
        fprintf(ec->f, "(");
        for (u32 i = 0; i < s->call.arg_count; i++) {
            if (i) fprintf(ec->f, ", ");
            emit_expr(ec, s->call.args[i]);
        }
        fprintf(ec->f, ");\n");
        break;
        
        case MQ_Stmt_Comment:
        ec_indent(ec);
        fprintf(ec->f, "// ");
        ec_str(ec, s->comment_text);
        fprintf(ec->f, "\n");
        break;
        
        case MQ_Stmt_Pragma:
        ec_indent(ec);
        fprintf(ec->f, "#pragma ");
        ec_str(ec, s->pragma.key);
        fprintf(ec->f, " \"");
        ec_str(ec, s->pragma.value);
        fprintf(ec->f, "\"\n");
        break;
    }
}

static void emit_routine(EmitCtx *ec, MQ_Routine *r) {
    if (r->is_intrinsic) {
        // Intrinsics have no source body; emit a comment marking them.
        fprintf(ec->f, "// intrinsic %s -> ",
                r->kind == MQ_Routine_Gate ? "gate" : "operation");
        ec_str(ec, r->name);
        fprintf(ec->f, " (%.*s)\n\n", str_expand(r->intrinsic_name));
        return;
    }
    
    if (!str_is_null(r->doc_comment)) {
        fprintf(ec->f, "// ");
        ec_str(ec, r->doc_comment);
        fprintf(ec->f, "\n");
    }
    
    fprintf(ec->f, "%s ", r->kind == MQ_Routine_Gate ? "gate" : "operation");
    ec_str(ec, r->name);
    fprintf(ec->f, "(");
    
    for (u32 i = 0; i < r->param_count; i++) {
        if (i) fprintf(ec->f, ", ");
        ec_str(ec, r->params[i].name);
        fprintf(ec->f, " : ");
        emit_type(ec, r->params[i].type);
        if (r->params[i].default_val) {
            fprintf(ec->f, " = ");
            emit_expr(ec, r->params[i].default_val);
        }
    }
    fprintf(ec->f, ")");
    
    if (r->kind == MQ_Routine_Operation && r->return_type &&
        r->return_type->kind != MQ_Type_Void) {
        fprintf(ec->f, " -> ");
        emit_type(ec, r->return_type);
    }
    
    // Build qubit name table from formal params for use inside the body
    ec->qnames = mq_qubit_names_from_routine(ec->arena, r);
    
    fprintf(ec->f, " {\n");
    ec->indent++;
    emit_block_body(ec, r->body);
    ec->indent--;
    fprintf(ec->f, "}\n\n");
}

static void emit_circuit(EmitCtx *ec, MQ_Circuit *c) {
    fprintf(ec->f, "circuit ");
    ec_str(ec, c->name);
    
    // Circuit-level inputs as formal params
    if (c->param_count > 0) {
        fprintf(ec->f, "(");
        for (u32 i = 0; i < c->param_count; i++) {
            if (i) fprintf(ec->f, ", ");
            ec_str(ec, c->param_names[i]);
            fprintf(ec->f, " : angle");
            if (c->param_defaults[i]) {
                fprintf(ec->f, " = ");
                emit_expr(ec, c->param_defaults[i]);
            }
        }
        fprintf(ec->f, ")");
    }
    
    fprintf(ec->f, " {\n");
    ec->indent++;
    
    // Qubit registers
    for (u32 i = 0; i < c->register_count; i++) {
        MQ_Register *r = &c->registers[i];
        ec_indent(ec);
        if (r->kind == MQ_Reg_Quantum) {
            fprintf(ec->f, "qubit ");
            ec_str(ec, r->name);
            if (r->size > 1) fprintf(ec->f, "[%u]", r->size);
            fprintf(ec->f, ";\n");
        } else {
            fprintf(ec->f, "bit ");
            ec_str(ec, r->name);
            if (r->size > 1) fprintf(ec->f, "[%u]", r->size);
            fprintf(ec->f, ";\n");
        }
    }
    
    // Unbound symbolic params declared inline (those without default)
    for (u32 i = 0; i < c->param_count; i++) {
        if (!c->param_defaults[i]) {
            ec_indent(ec);
            fprintf(ec->f, "param ");
            ec_str(ec, c->param_names[i]);
            fprintf(ec->f, ";\n");
        }
    }
    
    if (c->register_count > 0 || c->param_count > 0)
        fprintf(ec->f, "\n");
    
    // Build qubit name table from register metadata
    ec->qnames = mq_qubit_names_from_circuit(ec->arena, c);
    
    // Body statements
    emit_block_body(ec, c->body);
    
    ec->indent--;
    fprintf(ec->f, "}\n\n");
}

void mql_emit(FILE *f, MQ_Program *prog) {
    M_Arena scratch;
    arena_init(&scratch);
    
    if (!prog) return;
    
    EmitCtx ec = {0};
    ec.f      = f;
    ec.arena  = &scratch;
    ec.indent = 0;
    
    // Routines first so circuits can call them
    for (u32 i = 0; i < prog->routine_count; i++)
        emit_routine(&ec, prog->routines[i]);
    
    for (u32 i = 0; i < prog->circuit_count; i++)
        emit_circuit(&ec, prog->circuits[i]);
    
    arena_free(&scratch);
}
