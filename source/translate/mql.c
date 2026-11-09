#include "mql.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ======================================================================
 * Character Helpers
 * ====================================================================== */

static b8 is_alpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static b8 is_digit(u8 c) { return c >= '0' && c <= '9'; }
static b8 is_alnum(u8 c) { return is_alpha(c) || is_digit(c); }
static b8 is_space(u8 c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* ======================================================================
 * Lexer Cursor Helpers
 * ====================================================================== */

static b8 at_end(MQL_Lexer *l) { return l->pos >= l->src.size; }
static u8 cur(MQL_Lexer *l) { return at_end(l) ? 0 : l->src.str[l->pos]; }
static u8 peek_ch(MQL_Lexer *l, u64 off) {
    u64 p = l->pos + off;
    return p < l->src.size ? l->src.str[p] : 0;
}

static u8 advance(MQL_Lexer *l) {
    u8 c = cur(l);
    if (!at_end(l)) {
        l->pos++;
        if (c == '\n') { l->line++; l->col = 1; }
        else           { l->col++; }
    }
    return c;
}

static b8 eat(MQL_Lexer *l, u8 expected) {
    if (cur(l) == expected) { advance(l); return 1; }
    return 0;
}

static MQL_Token make_token(MQL_Lexer *l, MQL_TokenKind kind,
                            u64 start_pos, u32 start_line, u32 start_col) {
    MQL_Token t;
    t.kind   = kind;
    t.lexeme = (string){ .str = l->src.str + start_pos, .size = l->pos - start_pos };
    t.line   = start_line;
    t.col    = start_col;
    return t;
}

static MQL_Token make_error(MQL_Lexer *l, u64 start_pos, u32 start_line, u32 start_col) {
    return make_token(l, MQL_Tok_Error, start_pos, start_line, start_col);
}

/* ======================================================================
 * Keyword / Gate / Macro Table
 * ====================================================================== */

static b8 lexeme_eq_cstr(string s, const char *word) {
    u64 len = strlen(word);
    if (s.size != len) return 0;
    return memcmp(s.str, word, len) == 0;
}

typedef struct { const char *word; MQL_TokenKind kind; } MQL_KwEntry;

static const MQL_KwEntry kw_table[] = {
    // Language keywords
    { "gate",               MQL_Tok_Gate                },
    { "operation",          MQL_Tok_Operation           },
    { "circuit",            MQL_Tok_Circuit             },
    { "param",              MQL_Tok_Param               },
    { "let",                MQL_Tok_Let                 },
    { "set",                MQL_Tok_Set                 },
    { "qubit",              MQL_Tok_Qubit               },
    { "bit",                MQL_Tok_Bit                 },
    { "measure",            MQL_Tok_Measure             },
    { "reset",              MQL_Tok_Reset               },
    { "barrier",            MQL_Tok_Barrier             },
    { "delay",              MQL_Tok_Delay               },
    { "adjoint",            MQL_Tok_Adjoint             },
    { "ctrl",               MQL_Tok_Ctrl                },
    { "ctrl0",              MQL_Tok_Ctrl0               },
    { "if",                 MQL_Tok_If                  },
    { "elif",               MQL_Tok_Elif                },
    { "else",               MQL_Tok_Else                },
    { "for",                MQL_Tok_For                 },
    { "in",                 MQL_Tok_In                  },
    { "while",              MQL_Tok_While               },
    { "break",              MQL_Tok_Break               },
    { "continue",           MQL_Tok_Continue            },
    { "return",             MQL_Tok_Return              },
    { "true",               MQL_Tok_True                },
    { "false",              MQL_Tok_False               },
    { "void",               MQL_Tok_Void                },
    { "bool",               MQL_Tok_Bool                },
    { "int",                MQL_Tok_Int                 },
    { "float",              MQL_Tok_Float               },
    { "angle",              MQL_Tok_Angle               },
    { "string",             MQL_Tok_String              },
    
    // Built-in math functions & constants
    { "sin",                MQL_Tok_Sin                 },
    { "cos",                MQL_Tok_Cos                 },
    { "tan",                MQL_Tok_Tan                 },
    { "asin",               MQL_Tok_Asin                },
    { "acos",               MQL_Tok_Acos                },
    { "atan",               MQL_Tok_Atan                },
    { "sqrt",               MQL_Tok_Sqrt                },
    { "exp",                MQL_Tok_Exp                 },
    { "log",                MQL_Tok_Log                 },
    { "abs",                MQL_Tok_Abs                 },
    { "pi",                 MQL_Tok_Pi                  },
    { "PI",                 MQL_Tok_Pi                  },
    
    // Built-in gate names & common aliases
    { "I",                  MQL_Tok_GateI               },
    { "H",                  MQL_Tok_GateH               },
    { "X",                  MQL_Tok_GateX               },
    { "Y",                  MQL_Tok_GateY               },
    { "Z",                  MQL_Tok_GateZ               },
    { "S",                  MQL_Tok_GateS               },
    { "Sdg",                MQL_Tok_GateSdg             },
    { "T",                  MQL_Tok_GateT               },
    { "Tdg",                MQL_Tok_GateTdg             },
    { "P",                  MQL_Tok_GateP               },
    { "Phase",              MQL_Tok_GateP               },
    { "phase",              MQL_Tok_GateP               },
    { "RX",                 MQL_Tok_GateRX              },
    { "RY",                 MQL_Tok_GateRY              },
    { "RZ",                 MQL_Tok_GateRZ              },
    { "U",                  MQL_Tok_GateU               },
    { "U3",                 MQL_Tok_GateU               },
    { "SWAP",               MQL_Tok_GateSWAP            },
    { "swap",               MQL_Tok_GateSWAP            },
    { "ISWAP",              MQL_Tok_GateISWAP           },
    { "iswap",              MQL_Tok_GateISWAP           },
    { "RZZ",                MQL_Tok_GateRZZ             },
    { "RXX",                MQL_Tok_GateRXX             },
    { "RYY",                MQL_Tok_GateRYY             },
    { "CCX",                MQL_Tok_GateCCX             },
    { "Toffoli",            MQL_Tok_GateCCX             },
    { "toffoli",            MQL_Tok_GateCCX             },
    { "CSWAP",              MQL_Tok_GateCSWAP           },
    { "Fredkin",            MQL_Tok_GateCSWAP           },
    { "fredkin",            MQL_Tok_GateCSWAP           },
    { "CNOT",               MQL_Tok_GateCNOT            },
    { "cnot",               MQL_Tok_GateCNOT            },
    { "CX",                 MQL_Tok_GateCNOT            },
    { "cx",                 MQL_Tok_GateCNOT            },
    { "CZ",                 MQL_Tok_GateCZ              },
    { "cz",                 MQL_Tok_GateCZ              },
    { "CP",                 MQL_Tok_GateCP              },
    { "cp",                 MQL_Tok_GateCP              },
    { "CRX",                MQL_Tok_GateCRX             },
    { "crx",                MQL_Tok_GateCRX             },
    { "CRY",                MQL_Tok_GateCRY             },
    { "cry",                MQL_Tok_GateCRY             },
    { "CRZ",                MQL_Tok_GateCRZ             },
    { "crz",                MQL_Tok_GateCRZ             },
    
    // Built-in algorithmic macros / subroutines
    { "grover_oracle",      MQL_Tok_MacroGroverOracle   },
    { "oracle",             MQL_Tok_MacroGroverOracle   },
    { "phase_oracle",       MQL_Tok_MacroGroverOracle   },
    { "grover_diffuser",    MQL_Tok_MacroGroverDiffuser },
    { "diffuser",           MQL_Tok_MacroGroverDiffuser },
    { "bell_pair",          MQL_Tok_MacroBellPair       },
    { "create_bell_pair",   MQL_Tok_MacroBellPair       },
    { "qft",                MQL_Tok_MacroQFT            },
    { "iqft",               MQL_Tok_MacroIQFT           },
    { "inverse_qft",        MQL_Tok_MacroIQFT           },
    { "teleport",           MQL_Tok_MacroTeleport       },
    { "teleportation",      MQL_Tok_MacroTeleport       },
};

static MQL_TokenKind classify_ident(string s) {
    for (u64 i = 0; i < sizeof(kw_table) / sizeof(kw_table[0]); i++) {
        if (lexeme_eq_cstr(s, kw_table[i].word))
            return kw_table[i].kind;
    }
    return MQL_Tok_Ident;
}

/* ======================================================================
 * Sub-lexers
 * ====================================================================== */

static b8 try_lex_time_unit(MQL_Lexer *l) {
    u8 c0 = cur(l);
    u8 c1 = peek_ch(l, 1);
    
    if ((c0 == 'd' && c1 == 't') ||
        (c0 == 'n' && c1 == 's') ||
        (c0 == 'u' && c1 == 's') ||
        (c0 == 'm' && c1 == 's')) {
        if (!is_alnum(peek_ch(l, 2))) {
            advance(l); advance(l);
            return 1;
        }
    }
    if (c0 == 's' && !is_alnum(c1)) {
        advance(l);
        return 1;
    }
    return 0;
}

static MQL_Token lex_line_comment(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    while (!at_end(l) && cur(l) != '\n')
        advance(l);
    return make_token(l, MQL_Tok_Comment, sp, sl, sc);
}

static MQL_Token lex_block_comment(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    while (!at_end(l)) {
        if (cur(l) == '*' && peek_ch(l, 1) == '/') {
            advance(l); advance(l);
            return make_token(l, MQL_Tok_Comment, sp, sl, sc);
        }
        advance(l);
    }
    return make_error(l, sp, sl, sc);
}

static MQL_Token lex_ident_or_keyword(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    while (is_alnum(cur(l)))
        advance(l);
    string lexeme = { .str = l->src.str + sp, .size = l->pos - sp };
    MQL_TokenKind kind = classify_ident(lexeme);
    return make_token(l, kind, sp, sl, sc);
}

static MQL_Token lex_number(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    while (is_digit(cur(l)))
        advance(l);
    
    MQL_TokenKind num_kind = MQL_Tok_IntLit;
    
    if (cur(l) == '.' && peek_ch(l, 1) != '.' && is_digit(peek_ch(l, 1))) {
        num_kind = MQL_Tok_FloatLit;
        advance(l);
        while (is_digit(cur(l)))
            advance(l);
        if (cur(l) == 'e' || cur(l) == 'E') {
            advance(l);
            if (cur(l) == '+' || cur(l) == '-')
                advance(l);
            while (is_digit(cur(l)))
                advance(l);
        }
    }
    
    if (try_lex_time_unit(l))
        return make_token(l, MQL_Tok_TimeLit, sp, sl, sc);
    
    return make_token(l, num_kind, sp, sl, sc);
}

static MQL_Token lex_string(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    while (!at_end(l) && cur(l) != '"') {
        if (cur(l) == '\\') {
            advance(l);
            if (!at_end(l)) advance(l);
        } else {
            advance(l);
        }
    }
    if (at_end(l))
        return make_error(l, sp, sl, sc);
    advance(l);
    return make_token(l, MQL_Tok_StringLit, sp, sl, sc);
}

static MQL_Token lex_pragma_or_hash_comment(MQL_Lexer *l, u64 sp, u32 sl, u32 sc) {
    const char *pragma_word = "pragma";
    b8 is_pragma = 1;
    for (int i = 0; pragma_word[i]; i++) {
        if (peek_ch(l, i) != (u8)pragma_word[i]) {
            is_pragma = 0;
            break;
        }
    }
    if (is_pragma && !is_alnum(peek_ch(l, 6))) {
        for (int i = 0; i < 6; i++) advance(l);
        return make_token(l, MQL_Tok_Pragma, sp, sl, sc);
    }
    while (!at_end(l) && cur(l) != '\n')
        advance(l);
    return make_token(l, MQL_Tok_Comment, sp, sl, sc);
}

static MQL_Token mql_lex_one(MQL_Lexer *l) {
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
        
        case '.':
        if (eat(l, '.')) {
            eat(l, '=');
            return make_token(l, MQL_Tok_DotDot, sp, sl, sc);
        }
        return make_token(l, MQL_Tok_Dot, sp, sl, sc);
        
        case '!':
        if (eat(l, '=')) return make_token(l, MQL_Tok_BangEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Bang, sp, sl, sc);
        
        case '=':
        if (eat(l, '=')) return make_token(l, MQL_Tok_EqEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Eq, sp, sl, sc);
        
        case '+':
        if (eat(l, '=')) return make_token(l, MQL_Tok_PlusEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Plus, sp, sl, sc);
        
        case '-':
        if (eat(l, '>')) return make_token(l, MQL_Tok_Arrow,   sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_MinusEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Minus, sp, sl, sc);
        
        case '*':
        if (eat(l, '*')) return make_token(l, MQL_Tok_StarStar, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_StarEq,   sp, sl, sc);
        return make_token(l, MQL_Tok_Star, sp, sl, sc);
        
        case '/':
        if (eat(l, '/')) return lex_line_comment (l, sp, sl, sc);
        if (eat(l, '*')) return lex_block_comment(l, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_SlashEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Slash, sp, sl, sc);
        
        case '<':
        if (eat(l, '<')) {
            if (eat(l, '=')) return make_token(l, MQL_Tok_LtLtEq, sp, sl, sc);
            return make_token(l, MQL_Tok_LtLt, sp, sl, sc);
        }
        if (eat(l, '=')) return make_token(l, MQL_Tok_LtEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Lt, sp, sl, sc);
        
        case '>':
        if (eat(l, '>')) {
            if (eat(l, '=')) return make_token(l, MQL_Tok_GtGtEq, sp, sl, sc);
            return make_token(l, MQL_Tok_GtGt, sp, sl, sc);
        }
        if (eat(l, '=')) return make_token(l, MQL_Tok_GtEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Gt, sp, sl, sc);
        
        case '&':
        if (eat(l, '&')) return make_token(l, MQL_Tok_AmpAmp, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_AmpEq,  sp, sl, sc);
        return make_token(l, MQL_Tok_Amp, sp, sl, sc);
        
        case '|':
        if (eat(l, '|')) return make_token(l, MQL_Tok_PipePipe, sp, sl, sc);
        if (eat(l, '=')) return make_token(l, MQL_Tok_PipeEq,   sp, sl, sc);
        return make_token(l, MQL_Tok_Pipe, sp, sl, sc);
        
        case '^':
        if (eat(l, '=')) return make_token(l, MQL_Tok_CaretEq, sp, sl, sc);
        return make_token(l, MQL_Tok_Caret, sp, sl, sc);
        
        case '#':
        return lex_pragma_or_hash_comment(l, sp, sl, sc);
        
        case '"':
        return lex_string(l, sp, sl, sc);
        
        default:
        if (is_digit(c)) return lex_number(l, sp, sl, sc);
        if (is_alpha(c)) return lex_ident_or_keyword(l, sp, sl, sc);
        return make_error(l, sp, sl, sc);
    }
}

void mql_lexer_init(MQL_Lexer *lex, string src) {
    lex->src        = src;
    lex->pos        = 0;
    lex->line       = 1;
    lex->col        = 1;
    lex->has_peeked = 0;
    lex->peeked     = (MQL_Token){0};
}

MQL_Token mql_next(MQL_Lexer *lex) {
    if (lex->has_peeked) {
        lex->has_peeked = 0;
        return lex->peeked;
    }
    return mql_lex_one(lex);
}

MQL_Token mql_peek(MQL_Lexer *lex) {
    if (!lex->has_peeked) {
        lex->peeked     = mql_lex_one(lex);
        lex->has_peeked = 1;
    }
    return lex->peeked;
}

const char *mql_token_kind_str(MQL_TokenKind kind) {
    switch (kind) {
        case MQL_Tok_Eof:                   return "Eof";
        case MQL_Tok_Error:                 return "Error";
        case MQL_Tok_Comment:               return "Comment";
        case MQL_Tok_Ident:                 return "Ident";
        case MQL_Tok_IntLit:                return "IntLit";
        case MQL_Tok_FloatLit:              return "FloatLit";
        case MQL_Tok_StringLit:             return "StringLit";
        case MQL_Tok_TimeLit:               return "TimeLit";
        case MQL_Tok_Gate:                  return "gate";
        case MQL_Tok_Operation:             return "operation";
        case MQL_Tok_Circuit:               return "circuit";
        case MQL_Tok_Param:                 return "param";
        case MQL_Tok_Let:                   return "let";
        case MQL_Tok_Set:                   return "set";
        case MQL_Tok_Qubit:                 return "qubit";
        case MQL_Tok_Bit:                   return "bit";
        case MQL_Tok_Measure:               return "measure";
        case MQL_Tok_Reset:                 return "reset";
        case MQL_Tok_Barrier:               return "barrier";
        case MQL_Tok_Delay:                 return "delay";
        case MQL_Tok_Adjoint:               return "adjoint";
        case MQL_Tok_Ctrl:                  return "ctrl";
        case MQL_Tok_Ctrl0:                 return "ctrl0";
        case MQL_Tok_If:                    return "if";
        case MQL_Tok_Elif:                  return "elif";
        case MQL_Tok_Else:                  return "else";
        case MQL_Tok_For:                   return "for";
        case MQL_Tok_In:                    return "in";
        case MQL_Tok_While:                 return "while";
        case MQL_Tok_Break:                 return "break";
        case MQL_Tok_Continue:              return "continue";
        case MQL_Tok_Return:                return "return";
        case MQL_Tok_True:                  return "true";
        case MQL_Tok_False:                 return "false";
        case MQL_Tok_Void:                  return "void";
        case MQL_Tok_Bool:                  return "bool";
        case MQL_Tok_Int:                   return "int";
        case MQL_Tok_Float:                 return "float";
        case MQL_Tok_Angle:                 return "angle";
        case MQL_Tok_String:                return "string";
        case MQL_Tok_Sin:                   return "sin";
        case MQL_Tok_Cos:                   return "cos";
        case MQL_Tok_Tan:                   return "tan";
        case MQL_Tok_Asin:                  return "asin";
        case MQL_Tok_Acos:                  return "acos";
        case MQL_Tok_Atan:                  return "atan";
        case MQL_Tok_Sqrt:                  return "sqrt";
        case MQL_Tok_Exp:                   return "exp";
        case MQL_Tok_Log:                   return "log";
        case MQL_Tok_Abs:                   return "abs";
        case MQL_Tok_Pi:                    return "pi";
        case MQL_Tok_GateI:                 return "I";
        case MQL_Tok_GateH:                 return "H";
        case MQL_Tok_GateX:                 return "X";
        case MQL_Tok_GateY:                 return "Y";
        case MQL_Tok_GateZ:                 return "Z";
        case MQL_Tok_GateS:                 return "S";
        case MQL_Tok_GateSdg:               return "Sdg";
        case MQL_Tok_GateT:                 return "T";
        case MQL_Tok_GateTdg:               return "Tdg";
        case MQL_Tok_GateP:                 return "P";
        case MQL_Tok_GateRX:                return "RX";
        case MQL_Tok_GateRY:                return "RY";
        case MQL_Tok_GateRZ:                return "RZ";
        case MQL_Tok_GateU:                 return "U";
        case MQL_Tok_GateSWAP:              return "SWAP";
        case MQL_Tok_GateISWAP:             return "ISWAP";
        case MQL_Tok_GateRZZ:               return "RZZ";
        case MQL_Tok_GateRXX:               return "RXX";
        case MQL_Tok_GateRYY:               return "RYY";
        case MQL_Tok_GateCCX:               return "CCX";
        case MQL_Tok_GateCSWAP:             return "CSWAP";
        case MQL_Tok_GateCNOT:              return "CNOT";
        case MQL_Tok_GateCZ:                return "CZ";
        case MQL_Tok_GateCP:                return "CP";
        case MQL_Tok_GateCRX:               return "CRX";
        case MQL_Tok_GateCRY:               return "CRY";
        case MQL_Tok_GateCRZ:               return "CRZ";
        case MQL_Tok_MacroGroverOracle:     return "grover_oracle";
        case MQL_Tok_MacroGroverDiffuser:   return "grover_diffuser";
        case MQL_Tok_MacroBellPair:         return "bell_pair";
        case MQL_Tok_MacroQFT:              return "qft";
        case MQL_Tok_MacroIQFT:             return "iqft";
        case MQL_Tok_MacroTeleport:         return "teleport";
        case MQL_Tok_LParen:                return "(";
        case MQL_Tok_RParen:                return ")";
        case MQL_Tok_LBrace:                return "{";
        case MQL_Tok_RBrace:                return "}";
        case MQL_Tok_LBracket:              return "[";
        case MQL_Tok_RBracket:              return "]";
        case MQL_Tok_Semicolon:             return ";";
        case MQL_Tok_Comma:                 return ",";
        case MQL_Tok_Colon:                 return ":";
        case MQL_Tok_Dot:                   return ".";
        case MQL_Tok_DotDot:                return "..";
        case MQL_Tok_Star:                  return "*";
        case MQL_Tok_StarStar:              return "**";
        case MQL_Tok_Plus:                  return "+";
        case MQL_Tok_Minus:                 return "-";
        case MQL_Tok_Slash:                 return "/";
        case MQL_Tok_Percent:               return "%";
        case MQL_Tok_Tilde:                 return "~";
        case MQL_Tok_Pragma:                return "#pragma";
        case MQL_Tok_Eq:                    return "=";
        case MQL_Tok_EqEq:                  return "==";
        case MQL_Tok_Bang:                  return "!";
        case MQL_Tok_BangEq:                return "!=";
        case MQL_Tok_Lt:                    return "<";
        case MQL_Tok_LtEq:                  return "<=";
        case MQL_Tok_Gt:                    return ">";
        case MQL_Tok_GtEq:                  return ">=";
        case MQL_Tok_AmpAmp:                return "&&";
        case MQL_Tok_PipePipe:              return "||";
        case MQL_Tok_Arrow:                 return "->";
        case MQL_Tok_Amp:                   return "&";
        case MQL_Tok_Pipe:                  return "|";
        case MQL_Tok_Caret:                 return "^";
        case MQL_Tok_LtLt:                  return "<<";
        case MQL_Tok_GtGt:                  return ">>";
        case MQL_Tok_PlusEq:                return "+=";
        case MQL_Tok_MinusEq:               return "-=";
        case MQL_Tok_StarEq:                return "*=";
        case MQL_Tok_SlashEq:               return "/=";
        case MQL_Tok_AmpEq:                 return "&=";
        case MQL_Tok_PipeEq:                return "|=";
        case MQL_Tok_CaretEq:               return "^=";
        case MQL_Tok_LtLtEq:                return "<<=";
        case MQL_Tok_GtGtEq:                return ">>=";
    }
    return "<?>";
}

/* ======================================================================
 * Parser Internal Helpers
 * ====================================================================== */

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

static MQL_Token p_peek(MQL_Parser *p) { return mql_peek(&p->lex); }
static MQL_Token p_next(MQL_Parser *p) { return mql_next(&p->lex); }

static MQL_Token p_peek_nc(MQL_Parser *p) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    return p_peek(p);
}

static MQL_Token p_advance(MQL_Parser *p) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    return p_next(p);
}

static MQL_Token p_expect(MQL_Parser *p, MQL_TokenKind kind) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    MQL_Token t = p_next(p);
    if (t.kind != kind) {
        static char buf[128];
        snprintf(buf, sizeof(buf), "expected '%s', got '%s'",
                 mql_token_kind_str(kind), mql_token_kind_str(t.kind));
        p_error(p, t, buf);
    }
    return t;
}

static b8 is_name_token(MQL_TokenKind kind) {
    if (kind == MQL_Tok_Ident) return 1;
    if (kind >= MQL_Tok_GateI && kind <= MQL_Tok_MacroTeleport) return 1;
    if (kind == MQL_Tok_String || kind == MQL_Tok_Int || kind == MQL_Tok_Float ||
        kind == MQL_Tok_Bool || kind == MQL_Tok_Angle || kind == MQL_Tok_Void) return 1;
    return 0;
}

static MQL_Token p_expect_name(MQL_Parser *p) {
    while (p_peek(p).kind == MQL_Tok_Comment)
        p_next(p);
    MQL_Token t = p_peek(p);
    if (is_name_token(t.kind)) {
        return p_next(p);
    }
    return p_expect(p, MQL_Tok_Ident);
}

static b8 p_eat(MQL_Parser *p, MQL_TokenKind kind) {
    if (p_peek_nc(p).kind == kind) { p_advance(p); return 1; }
    return 0;
}

static string tok_str(MQL_Token t) { return t.lexeme; }

static void scope_clear(MQL_Parser *p) {
    p->scope_count = 0;
}

static void scope_add(MQL_Parser *p, string name, MQL_SymKind kind, u32 base_id, u32 size) {
    if (p->scope_count >= MQL_SCOPE_MAX) return;
    MQL_Symbol *s = &p->scope[p->scope_count++];
    s->name    = name;
    s->kind    = kind;
    s->base_id = base_id;
    s->size    = size;
}

static MQL_Symbol *scope_find(MQL_Parser *p, string name) {
    for (u32 i = 0; i < p->scope_count; i++) {
        if (str_eq(p->scope[i].name, name))
            return &p->scope[i];
    }
    return 0;
}

/* ======================================================================
 * Qubit & Bit Range Structures
 * ====================================================================== */

typedef struct MQL_IdList {
    u32 ids[64];
    u32 count;
} MQL_IdList;

static MQ_Expr *parse_expr(MQL_Parser *p);
static MQ_Stmt *parse_stmt(MQL_Parser *p);
static MQ_Stmt *parse_block(MQL_Parser *p);

/* ======================================================================
 * Type Parsing
 * ====================================================================== */

static MQ_Type *parse_type(MQL_Parser *p) {
    MQL_Token t = p_advance(p);
    switch (t.kind) {
        case MQL_Tok_Void:   return mq_type_scalar(p->arena, MQ_Type_Void);
        case MQL_Tok_Bool:   return mq_type_scalar(p->arena, MQ_Type_Bool);
        case MQL_Tok_Float:  return mq_type_scalar(p->arena, MQ_Type_Float);
        case MQL_Tok_Angle:  return mq_type_scalar(p->arena, MQ_Type_Angle);
        case MQL_Tok_String: return mq_type_scalar(p->arena, MQ_Type_Void);
        
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
                /* qubit[] means open/unknown size (width=0); qubit[N] means fixed size N */
                u32 sz = 0;
                if (p_peek_nc(p).kind != MQL_Tok_RBracket) {
                    MQL_Token n = p_expect(p, MQL_Tok_IntLit);
                    sz = (u32)strtol((char*)n.lexeme.str, 0, 10);
                }
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

/* ======================================================================
 * Expression Parsing (Pratt Precedence Climbing)
 * ====================================================================== */

static int binop_prec(MQL_TokenKind k) {
    switch (k) {
        case MQL_Tok_PipePipe:               return 1;
        case MQL_Tok_AmpAmp:                 return 2;
        case MQL_Tok_Pipe:                   return 3;
        case MQL_Tok_Caret:                  return 4;
        case MQL_Tok_Amp:                    return 5;
        case MQL_Tok_EqEq:
        case MQL_Tok_BangEq:                 return 6;
        case MQL_Tok_Lt: case MQL_Tok_LtEq:
        case MQL_Tok_Gt: case MQL_Tok_GtEq:  return 7;
        case MQL_Tok_LtLt:
        case MQL_Tok_GtGt:                   return 8;
        case MQL_Tok_Plus:
        case MQL_Tok_Minus:                  return 9;
        case MQL_Tok_Star:
        case MQL_Tok_Slash:
        case MQL_Tok_Percent:                return 10;
        case MQL_Tok_StarStar:               return 11;
        default:                             return -1;
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
        default:                return MQ_BinOp_Add;
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
        default:           return MQ_UnOp_Abs;
    }
}

static MQ_Expr **parse_expr_list(MQL_Parser *p, u32 *out_count) {
    typedef struct ExprNode { MQ_Expr *e; struct ExprNode *next; } ExprNode;
    ExprNode *head = 0, *tail = 0;
    u32 count = 0;
    
    if (p_peek_nc(p).kind != MQL_Tok_RParen) {
        do {
            MQ_Expr *e = parse_expr(p);
            ExprNode *n = arena_alloc_zero(p->arena, sizeof(ExprNode));
            n->e = e;
            if (!head) head = n; else tail->next = n;
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

static MQ_Expr *parse_primary(MQL_Parser *p) {
    MQL_Token t = p_advance(p);
    
    switch (t.kind) {
        case MQL_Tok_IntLit:
        return mq_expr_int(p->arena, strtoll((char*)t.lexeme.str, 0, 10));
        
        case MQL_Tok_FloatLit:
        return mq_expr_float(p->arena, strtod((char*)t.lexeme.str, 0));
        
        case MQL_Tok_True:  return mq_expr_bool(p->arena, 1);
        case MQL_Tok_False: return mq_expr_bool(p->arena, 0);
        case MQL_Tok_Pi:    return mq_expr_symbol(p->arena, str_lit("pi"));
        
        case MQL_Tok_Minus: {
            MQ_Expr *op = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_Neg, op);
        }
        case MQL_Tok_Bang: {
            MQ_Expr *op = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_Not, op);
        }
        case MQL_Tok_Tilde: {
            MQ_Expr *op = parse_primary(p);
            return mq_expr_unop(p->arena, MQ_UnOp_BitNot, op);
        }
        
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
        
        case MQL_Tok_LParen: {
            MQ_Expr *e = parse_expr(p);
            p_expect(p, MQL_Tok_RParen);
            return e;
        }
        
        case MQL_Tok_LBracket: {
            u32 count = 0;
            MQ_Expr **elems = 0;
            if (p_peek_nc(p).kind != MQL_Tok_RBracket) {
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
        
        default:
        if (is_name_token(t.kind)) {
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
            return mq_expr_var(p->arena, name);
        }
        p_error(p, t, "expected expression");
        return mq_expr_int(p->arena, 0);
    }
}

static MQ_Expr *parse_expr_prec(MQL_Parser *p, int min_prec) {
    MQ_Expr *lhs = parse_primary(p);
    for (;;) {
        MQL_TokenKind k = p_peek_nc(p).kind;
        int prec = binop_prec(k);
        if (prec < min_prec) break;
        p_advance(p);
        MQ_BinOp op = tok_to_binop(k);
        int next_prec = (k == MQL_Tok_StarStar) ? prec : prec + 1;
        MQ_Expr *rhs = parse_expr_prec(p, next_prec);
        lhs = mq_expr_binop(p->arena, op, lhs, rhs);
    }
    return lhs;
}

static MQ_Expr *parse_expr(MQL_Parser *p) {
    return parse_expr_prec(p, 0);
}

/* ======================================================================
 * Qubit / Classical Bit Range Resolution
 * ====================================================================== */

static MQL_IdList parse_qubit_range(MQL_Parser *p) {
    MQL_IdList list = {0};
    MQL_Token name_tok = p_expect_name(p);
    string name = tok_str(name_tok);
    
    MQL_Symbol *sym = scope_find(p, name);
    if (!sym) {
        p_error(p, name_tok, "unknown qubit register name");
        return list;
    }
    if (sym->kind != MQL_Sym_Qubit) {
        p_error(p, name_tok, "expected qubit register");
        return list;
    }
    
    if (p_peek_nc(p).kind != MQL_Tok_LBracket) {
        for (u32 i = 0; i < sym->size && list.count < 64; i++) {
            list.ids[list.count++] = sym->base_id + i;
        }
        return list;
    }
    
    p_expect(p, MQL_Tok_LBracket);
    
    if (p_peek_nc(p).kind == MQL_Tok_RBracket) {
        p_advance(p);
        for (u32 i = 0; i < sym->size && list.count < 64; i++)
            list.ids[list.count++] = sym->base_id + i;
        return list;
    }
    
    do {
        MQL_Token start_tok = p_expect(p, MQL_Tok_IntLit);
        u32 start = (u32)strtol((char*)start_tok.lexeme.str, 0, 10);
        
        if (p_eat(p, MQL_Tok_DotDot)) {
            MQL_Token end_tok = p_expect(p, MQL_Tok_IntLit);
            u32 end = (u32)strtol((char*)end_tok.lexeme.str, 0, 10);
            if (end >= sym->size) end = sym->size - 1;
            if (start <= end) {
                for (u32 idx = start; idx <= end && list.count < 64; idx++)
                    list.ids[list.count++] = sym->base_id + idx;
            }
        } else if (p_eat(p, MQL_Tok_Colon)) {
            MQL_Token end_tok = p_expect(p, MQL_Tok_IntLit);
            u32 end = (u32)strtol((char*)end_tok.lexeme.str, 0, 10);
            if (end > sym->size) end = sym->size;
            if (start < end) {
                for (u32 idx = start; idx < end && list.count < 64; idx++)
                    list.ids[list.count++] = sym->base_id + idx;
            }
        } else {
            if (start >= sym->size) {
                p_error(p, start_tok, "qubit index out of range");
            } else if (list.count < 64) {
                list.ids[list.count++] = sym->base_id + start;
            }
        }
    } while (p_eat(p, MQL_Tok_Comma));
    
    p_expect(p, MQL_Tok_RBracket);
    return list;
}

static MQL_IdList parse_cbit_range(MQL_Parser *p, string *out_reg_name) {
    MQL_IdList list = {0};
    MQL_Token name_tok = p_expect_name(p);
    string name = tok_str(name_tok);
    if (out_reg_name) *out_reg_name = name;
    
    MQL_Symbol *sym = scope_find(p, name);
    if (!sym) {
        u32 inferred_size = 1;
        if (p_peek_nc(p).kind == MQL_Tok_LBracket) {
            p_advance(p);
            MQL_Token sz_tok = p_expect(p, MQL_Tok_IntLit);
            u32 val = (u32)strtol((char*)sz_tok.lexeme.str, 0, 10);
            p_expect(p, MQL_Tok_RBracket);
            inferred_size = val > 0 ? val : 1;
        }
        scope_add(p, name, MQL_Sym_Bit, p->auto_cbits_count, inferred_size);
        for (u32 i = 0; i < inferred_size && list.count < 64; i++)
            list.ids[list.count++] = p->auto_cbits_count + i;
        p->auto_cbits_count += inferred_size;
        return list;
    }
    
    if (p_peek_nc(p).kind != MQL_Tok_LBracket) {
        for (u32 i = 0; i < sym->size && list.count < 64; i++)
            list.ids[list.count++] = sym->base_id + i;
        return list;
    }
    
    p_expect(p, MQL_Tok_LBracket);
    do {
        MQL_Token start_tok = p_expect(p, MQL_Tok_IntLit);
        u32 start = (u32)strtol((char*)start_tok.lexeme.str, 0, 10);
        
        if (p_eat(p, MQL_Tok_DotDot)) {
            MQL_Token end_tok = p_expect(p, MQL_Tok_IntLit);
            u32 end = (u32)strtol((char*)end_tok.lexeme.str, 0, 10);
            if (end >= sym->size) end = sym->size - 1;
            for (u32 idx = start; idx <= end && list.count < 64; idx++)
                list.ids[list.count++] = sym->base_id + idx;
        } else if (p_eat(p, MQL_Tok_Colon)) {
            MQL_Token end_tok = p_expect(p, MQL_Tok_IntLit);
            u32 end = (u32)strtol((char*)end_tok.lexeme.str, 0, 10);
            if (end > sym->size) end = sym->size;
            for (u32 idx = start; idx < end && list.count < 64; idx++)
                list.ids[list.count++] = sym->base_id + idx;
        } else {
            if (start >= sym->size) {
                p_error(p, start_tok, "bit index out of range");
            } else if (list.count < 64) {
                list.ids[list.count++] = sym->base_id + start;
            }
        }
    } while (p_eat(p, MQL_Tok_Comma));
    p_expect(p, MQL_Tok_RBracket);
    return list;
}

/* ======================================================================
 * Gate Mapping & Gate Token Helpers
 * ====================================================================== */

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

static b8 tok_is_param_gate(MQL_TokenKind k) {
    return k == MQL_Tok_GateP || k == MQL_Tok_GateRX || k == MQL_Tok_GateRY ||
           k == MQL_Tok_GateRZ || k == MQL_Tok_GateU || k == MQL_Tok_GateRZZ ||
           k == MQL_Tok_GateRXX || k == MQL_Tok_GateRYY || k == MQL_Tok_GateCP ||
           k == MQL_Tok_GateCRX || k == MQL_Tok_GateCRY || k == MQL_Tok_GateCRZ;
}

static MQ_Instruction make_gate_1q(MQ_GateType g, u32 q, MQ_Expr **params, u8 pcount) {
    u32 qs[1] = { q };
    if (pcount == 0) {
        return mq_instr_gate(g, qs, 1);
    }
    b8 has_sym = 0;
    for (u8 i = 0; i < pcount; i++) {
        if (params[i]->kind != MQ_Expr_FloatLit && params[i]->kind != MQ_Expr_IntLit)
            has_sym = 1;
    }
    if (has_sym) {
        return mq_instr_gate_sym(g, qs, 1, params, pcount);
    }
    f64 pvals[MQ_MAX_GATE_PARAMS];
    for (u8 i = 0; i < pcount; i++) {
        pvals[i] = (params[i]->kind == MQ_Expr_FloatLit) ? params[i]->lit.float_val : (f64)params[i]->lit.int_val;
    }
    return mq_instr_gate_p(g, qs, 1, pvals, pcount);
}

/* ======================================================================
 * Algorithmic Macro Expander Engine
 * ====================================================================== */

typedef struct StmtBuilder {
    MQ_Stmt **stmts;
    u32       count;
    u32       cap;
    M_Arena  *arena;
} StmtBuilder;

static void sb_init(StmtBuilder *sb, M_Arena *arena) {
    sb->arena = arena;
    sb->count = 0;
    sb->cap   = 64;
    sb->stmts = arena_alloc_array(arena, MQ_Stmt *, sb->cap);
}

static void sb_add(StmtBuilder *sb, MQ_Stmt *s) {
    if (sb->count >= sb->cap) {
        u32 new_cap = sb->cap * 2;
        MQ_Stmt **ns = arena_alloc_array(sb->arena, MQ_Stmt *, new_cap);
        memcpy(ns, sb->stmts, sizeof(MQ_Stmt *) * sb->count);
        sb->stmts = ns;
        sb->cap   = new_cap;
    }
    sb->stmts[sb->count++] = s;
}

static void sb_add_gate(StmtBuilder *sb, MQ_Instruction instr) {
    sb_add(sb, mq_stmt_instr(sb->arena, instr));
}

static MQ_Stmt *expand_grover_oracle(MQL_Parser *p, MQL_IdList qs, string pattern_str) {
    StmtBuilder sb;
    sb_init(&sb, p->arena);
    
    u32 n = qs.count;
    if (n == 0) return mq_stmt_block(p->arena, 0, 0);
    
    for (u32 i = 0; i < n && i < pattern_str.size; i++) {
        if (pattern_str.str[i] == '0') {
            u32 target[1] = { qs.ids[i] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_X, target, 1));
        }
    }
    
    if (n == 1) {
        u32 target[1] = { qs.ids[0] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_Z, target, 1));
    } else if (n == 2) {
        u32 target[1] = { qs.ids[1] };
        MQ_Instruction cz = mq_instr_gate(MQ_Gate_Z, target, 1);
        mq_instr_add_control(&cz, qs.ids[0], 1);
        sb_add_gate(&sb, cz);
    } else {
        u32 last_q = qs.ids[n - 1];
        u32 last_arr[1] = { last_q };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, last_arr, 1));
        
        MQ_Instruction mcx = mq_instr_gate(MQ_Gate_X, last_arr, 1);
        for (u32 i = 0; i < n - 1; i++) {
            mq_instr_add_control(&mcx, qs.ids[i], 1);
        }
        sb_add_gate(&sb, mcx);
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, last_arr, 1));
    }
    
    for (u32 i = 0; i < n && i < pattern_str.size; i++) {
        if (pattern_str.str[i] == '0') {
            u32 target[1] = { qs.ids[i] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_X, target, 1));
        }
    }
    
    return mq_stmt_block(p->arena, sb.stmts, sb.count);
}

static MQ_Stmt *expand_grover_diffuser(MQL_Parser *p, MQL_IdList qs) {
    StmtBuilder sb;
    sb_init(&sb, p->arena);
    u32 n = qs.count;
    if (n == 0) return mq_stmt_block(p->arena, 0, 0);
    
    for (u32 i = 0; i < n; i++) {
        u32 target[1] = { qs.ids[i] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, target, 1));
    }
    for (u32 i = 0; i < n; i++) {
        u32 target[1] = { qs.ids[i] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_X, target, 1));
    }
    
    if (n == 1) {
        u32 target[1] = { qs.ids[0] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_Z, target, 1));
    } else if (n == 2) {
        u32 target[1] = { qs.ids[1] };
        MQ_Instruction cz = mq_instr_gate(MQ_Gate_Z, target, 1);
        mq_instr_add_control(&cz, qs.ids[0], 1);
        sb_add_gate(&sb, cz);
    } else {
        u32 last_q = qs.ids[n - 1];
        u32 last_arr[1] = { last_q };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, last_arr, 1));
        
        MQ_Instruction mcx = mq_instr_gate(MQ_Gate_X, last_arr, 1);
        for (u32 i = 0; i < n - 1; i++) {
            mq_instr_add_control(&mcx, qs.ids[i], 1);
        }
        sb_add_gate(&sb, mcx);
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, last_arr, 1));
    }
    
    for (u32 i = 0; i < n; i++) {
        u32 target[1] = { qs.ids[i] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_X, target, 1));
    }
    for (u32 i = 0; i < n; i++) {
        u32 target[1] = { qs.ids[i] };
        sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, target, 1));
    }
    
    return mq_stmt_block(p->arena, sb.stmts, sb.count);
}

static MQ_Stmt *expand_bell_pair(MQL_Parser *p, u32 q0, u32 q1) {
    StmtBuilder sb;
    sb_init(&sb, p->arena);
    
    u32 t0[1] = { q0 };
    sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, t0, 1));
    
    u32 t1[1] = { q1 };
    MQ_Instruction cx = mq_instr_gate(MQ_Gate_X, t1, 1);
    mq_instr_add_control(&cx, q0, 1);
    sb_add_gate(&sb, cx);
    
    return mq_stmt_block(p->arena, sb.stmts, sb.count);
}

static MQ_Stmt *expand_qft(MQL_Parser *p, MQL_IdList qs, b8 inverse) {
    StmtBuilder sb;
    sb_init(&sb, p->arena);
    u32 n = qs.count;
    if (n == 0) return mq_stmt_block(p->arena, 0, 0);
    
    if (!inverse) {
        for (u32 i = 0; i < n; i++) {
            u32 ti[1] = { qs.ids[i] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, ti, 1));
            
            for (u32 j = i + 1; j < n; j++) {
                i64 div = (i64)1 << (j - i);
                MQ_Expr *angle = mq_expr_binop(p->arena, MQ_BinOp_Div,
                                              mq_expr_symbol(p->arena, str_lit("pi")),
                                              mq_expr_int(p->arena, div));
                MQ_Expr *param_arr[1] = { angle };
                MQ_Instruction cp = mq_instr_gate_sym(MQ_Gate_P, ti, 1, param_arr, 1);
                mq_instr_add_control(&cp, qs.ids[j], 1);
                sb_add_gate(&sb, cp);
            }
        }
        for (u32 i = 0; i < n / 2; i++) {
            u32 swap_qs[2] = { qs.ids[i], qs.ids[n - 1 - i] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_SWAP, swap_qs, 2));
        }
    } else {
        for (u32 i = 0; i < n / 2; i++) {
            u32 swap_qs[2] = { qs.ids[i], qs.ids[n - 1 - i] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_SWAP, swap_qs, 2));
        }
        for (i32 i = (i32)n - 1; i >= 0; i--) {
            u32 ti[1] = { qs.ids[i] };
            for (i32 j = (i32)n - 1; j > i; j--) {
                i64 div = (i64)1 << (j - i);
                MQ_Expr *angle = mq_expr_unop(p->arena, MQ_UnOp_Neg,
                                              mq_expr_binop(p->arena, MQ_BinOp_Div,
                                                            mq_expr_symbol(p->arena, str_lit("pi")),
                                                            mq_expr_int(p->arena, div)));
                MQ_Expr *param_arr[1] = { angle };
                MQ_Instruction cp = mq_instr_gate_sym(MQ_Gate_P, ti, 1, param_arr, 1);
                mq_instr_add_control(&cp, qs.ids[j], 1);
                sb_add_gate(&sb, cp);
            }
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, ti, 1));
        }
    }
    return mq_stmt_block(p->arena, sb.stmts, sb.count);
}

/* ======================================================================
 * Gate Instruction Parsing with Vectorized Broadcasting
 * ====================================================================== */

static MQ_Stmt *parse_gate_statement(MQL_Parser *p) {
    u32 ctrl_qubits[MQ_MAX_CONTROLS];
    u8  ctrl_states[MQ_MAX_CONTROLS];
    u8  ctrl_count = 0;
    
    while (1) {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        u8 state;
        if      (pk == MQL_Tok_Ctrl)  state = 1;
        else if (pk == MQL_Tok_Ctrl0) state = 0;
        else break;
        
        p_advance(p);
        p_expect(p, MQL_Tok_LBracket);
        
        MQL_Token name_tok = p_expect_name(p);
        string name = tok_str(name_tok);
        MQL_Symbol *sym = scope_find(p, name);
        if (!sym || sym->kind != MQL_Sym_Qubit) {
            p_error(p, name_tok, "invalid control qubit");
        } else {
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token idx_tok = p_expect(p, MQL_Tok_IntLit);
                u32 start = (u32)strtol((char*)idx_tok.lexeme.str, 0, 10);
                if (p_eat(p, MQL_Tok_DotDot)) {
                    MQL_Token end_tok = p_expect(p, MQL_Tok_IntLit);
                    u32 end = (u32)strtol((char*)end_tok.lexeme.str, 0, 10);
                    for (u32 i = start; i <= end && ctrl_count < MQ_MAX_CONTROLS; i++) {
                        ctrl_qubits[ctrl_count] = sym->base_id + i;
                        ctrl_states[ctrl_count] = state;
                        ctrl_count++;
                    }
                } else {
                    if (ctrl_count < MQ_MAX_CONTROLS) {
                        ctrl_qubits[ctrl_count] = sym->base_id + start;
                        ctrl_states[ctrl_count] = state;
                        ctrl_count++;
                    }
                }
                p_expect(p, MQL_Tok_RBracket);
            } else {
                if (ctrl_count < MQ_MAX_CONTROLS) {
                    ctrl_qubits[ctrl_count] = sym->base_id;
                    ctrl_states[ctrl_count] = state;
                    ctrl_count++;
                }
            }
        }
        p_expect(p, MQL_Tok_RBracket);
    }
    
    MQL_Token gate_tok = p_advance(p);
    MQ_GateType gate_type = MQ_Gate_I;
    b8 is_builtin = tok_to_gate_type(gate_tok.kind, &gate_type);
    string custom_name = {0};
    b8 is_cnot = (gate_tok.kind == MQL_Tok_GateCNOT);
    b8 is_cz   = (gate_tok.kind == MQL_Tok_GateCZ);
    b8 is_cp   = (gate_tok.kind == MQL_Tok_GateCP);
    b8 is_crx  = (gate_tok.kind == MQL_Tok_GateCRX);
    b8 is_cry  = (gate_tok.kind == MQL_Tok_GateCRY);
    b8 is_crz  = (gate_tok.kind == MQL_Tok_GateCRZ);
    
    if (is_cnot) { gate_type = MQ_Gate_X; }
    if (is_cz)   { gate_type = MQ_Gate_Z; }
    if (is_cp)   { gate_type = MQ_Gate_P; }
    if (is_crx)  { gate_type = MQ_Gate_RX; }
    if (is_cry)  { gate_type = MQ_Gate_RY; }
    if (is_crz)  { gate_type = MQ_Gate_RZ; }
    
    if (!is_builtin && !is_cnot && !is_cz && !is_cp && !is_crx && !is_cry && !is_crz) {
        if (is_name_token(gate_tok.kind)) {
            gate_type = MQ_Gate_Custom;
            custom_name = tok_str(gate_tok);
        } else {
            p_error(p, gate_tok, "expected gate name");
        }
    }
    
    MQ_Expr *param_exprs[MQ_MAX_GATE_PARAMS];
    u8 param_count = 0;
    
    // Check if followed by '('
    if (p_peek_nc(p).kind == MQL_Tok_LParen) {
        if (tok_is_param_gate(gate_tok.kind) || gate_type == MQ_Gate_Custom) {
            p_advance(p); // consume '('
            while (p_peek_nc(p).kind != MQL_Tok_RParen && p_peek_nc(p).kind != MQL_Tok_Eof) {
                MQ_Expr *e = parse_expr(p);
                if (param_count < MQ_MAX_GATE_PARAMS)
                    param_exprs[param_count++] = e;
                if (!p_eat(p, MQL_Tok_Comma)) break;
            }
            p_expect(p, MQL_Tok_RParen);
        } else if (!is_builtin && !is_cnot && !is_cz) {
            // Function/subroutine call: foo(args);
            p_advance(p); // consume '('
            u32 argc = 0;
            MQ_Expr **args = parse_expr_list(p, &argc);
            p_expect(p, MQL_Tok_RParen);
            p_eat(p, MQL_Tok_Semicolon);
            return mq_stmt_call(p->arena, tok_str(gate_tok), args, argc);
        }
    }
    
    b8 is_2q = is_cnot || is_cz || is_cp || is_crx || is_cry || is_crz ||
               gate_type == MQ_Gate_SWAP || gate_type == MQ_Gate_ISWAP ||
               gate_type == MQ_Gate_RZZ || gate_type == MQ_Gate_RXX || gate_type == MQ_Gate_RYY;
    
    b8 is_3q = (gate_type == MQ_Gate_CCX || gate_type == MQ_Gate_CSWAP);
    
    StmtBuilder sb;
    sb_init(&sb, p->arena);
    
    if (is_cnot || is_cz || is_cp || is_crx || is_cry || is_crz) {
        MQL_IdList c_list = parse_qubit_range(p);
        p_eat(p, MQL_Tok_Comma);
        p_eat(p, MQL_Tok_Arrow);
        MQL_IdList t_list = parse_qubit_range(p);
        
        u32 pairs = c_list.count < t_list.count ? c_list.count : t_list.count;
        if (pairs == 0 && c_list.count >= 1 && t_list.count >= 1) pairs = 1;
        
        for (u32 i = 0; i < pairs; i++) {
            MQ_Instruction instr = make_gate_1q(gate_type, t_list.ids[i], param_exprs, param_count);
            for (u8 c = 0; c < ctrl_count; c++)
                mq_instr_add_control(&instr, ctrl_qubits[c], ctrl_states[c]);
            mq_instr_add_control(&instr, c_list.ids[i], 1);
            sb_add_gate(&sb, instr);
        }
    } else if (is_3q) {
        MQL_IdList q0 = parse_qubit_range(p);
        p_eat(p, MQL_Tok_Comma);
        MQL_IdList q1 = parse_qubit_range(p);
        p_eat(p, MQL_Tok_Comma);
        MQL_IdList q2 = parse_qubit_range(p);
        
        if (q0.count >= 1 && q1.count >= 1 && q2.count >= 1) {
            u32 qs[3] = { q0.ids[0], q1.ids[0], q2.ids[0] };
            MQ_Instruction instr = mq_instr_gate(gate_type, qs, 3);
            for (u8 c = 0; c < ctrl_count; c++)
                mq_instr_add_control(&instr, ctrl_qubits[c], ctrl_states[c]);
            sb_add_gate(&sb, instr);
        }
    } else if (is_2q) {
        MQL_IdList q0 = parse_qubit_range(p);
        p_eat(p, MQL_Tok_Comma);
        p_eat(p, MQL_Tok_Arrow);
        MQL_IdList q1 = parse_qubit_range(p);
        
        if (q0.count >= 1 && q1.count >= 1) {
            u32 qs[2] = { q0.ids[0], q1.ids[0] };
            MQ_Instruction instr;
            if (param_count == 0) {
                instr = mq_instr_gate(gate_type, qs, 2);
            } else {
                instr = mq_instr_gate_sym(gate_type, qs, 2, param_exprs, param_count);
            }
            for (u8 c = 0; c < ctrl_count; c++)
                mq_instr_add_control(&instr, ctrl_qubits[c], ctrl_states[c]);
            sb_add_gate(&sb, instr);
        }
    } else {
        while (is_name_token(p_peek_nc(p).kind)) {
            MQL_IdList q_list = parse_qubit_range(p);
            for (u32 i = 0; i < q_list.count; i++) {
                MQ_Instruction instr = make_gate_1q(gate_type, q_list.ids[i], param_exprs, param_count);
                if (gate_type == MQ_Gate_Custom)
                    instr.gate.custom_name = custom_name;
                for (u8 c = 0; c < ctrl_count; c++)
                    mq_instr_add_control(&instr, ctrl_qubits[c], ctrl_states[c]);
                sb_add_gate(&sb, instr);
            }
            p_eat(p, MQL_Tok_Comma);
        }
    }
    
    // Optional classical condition: if c[1] == 1
    if (p_eat(p, MQL_Tok_If)) {
        MQL_Token cname = p_expect_name(p);
        u32 bit_idx = 0;
        if (p_eat(p, MQL_Tok_LBracket)) {
            MQL_Token cidx = p_expect(p, MQL_Tok_IntLit);
            bit_idx = (u32)strtol((char*)cidx.lexeme.str, 0, 10);
            p_expect(p, MQL_Tok_RBracket);
        }
        MQL_Symbol *sym = scope_find(p, tok_str(cname));
        if (sym && sym->kind == MQL_Sym_Bit) {
            bit_idx += sym->base_id;
        }
        p_expect(p, MQL_Tok_EqEq);
        MQL_Token val_tok = p_expect(p, MQL_Tok_IntLit);
        u32 val = (u32)strtol((char*)val_tok.lexeme.str, 0, 10);
        for (u32 i = 0; i < sb.count; i++) {
            if (sb.stmts[i]->kind == MQ_Stmt_Instr && sb.stmts[i]->instr.type == MQ_Instr_Gate) {
                sb.stmts[i]->instr.gate.has_classical_cond = 1;
                sb.stmts[i]->instr.gate.classical_bit = bit_idx;
                sb.stmts[i]->instr.gate.classical_val = (u8)val;
            }
        }
    }
    
    p_eat(p, MQL_Tok_Semicolon);
    if (sb.count == 1) return sb.stmts[0];
    return mq_stmt_block(p->arena, sb.stmts, sb.count);
}

/* ======================================================================
 * Statement Parser
 * ====================================================================== */

static MQ_Stmt *parse_stmt(MQL_Parser *p) {
    MQL_Token t = p_peek_nc(p);
    
    if (t.kind == MQL_Tok_Comment) {
        p_next(p);
        return mq_stmt_comment(p->arena, tok_str(t));
    }
    
    if (t.kind == MQL_Tok_Pragma) {
        p_advance(p);
        MQL_Token key = p_expect_name(p);
        MQL_Token val = p_expect(p, MQL_Tok_StringLit);
        return mq_stmt_pragma(p->arena, tok_str(key), tok_str(val));
    }
    
    if (t.kind == MQL_Tok_Adjoint) {
        p_advance(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_adjoint(p->arena, body);
    }
    
    if (t.kind == MQL_Tok_Let) {
        p_advance(p);
        MQL_Token name = p_expect_name(p);
        p_expect(p, MQL_Tok_Colon);
        MQ_Type *ty = parse_type(p);
        MQ_Expr *init = 0;
        if (p_eat(p, MQL_Tok_Eq)) init = parse_expr(p);
        p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_decl_classical(p->arena, tok_str(name), ty, init);
    }
    
    if (t.kind == MQL_Tok_Param) {
        p_advance(p);
        MQL_Token name = p_expect_name(p);
        if (p_eat(p, MQL_Tok_Colon)) parse_type(p);
        MQ_Expr *def = 0;
        if (p_eat(p, MQL_Tok_Eq)) def = parse_expr(p);
        p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_decl_param(p->arena, tok_str(name), def);
    }
    
    if (t.kind == MQL_Tok_Set) {
        p_advance(p);
        MQ_Expr *lhs = parse_expr(p);
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
            default: break;
        }
        if (augmented) p_advance(p);
        else           p_expect(p, MQL_Tok_Eq);
        MQ_Expr *rhs = parse_expr(p);
        p_eat(p, MQL_Tok_Semicolon);
        if (augmented) return mq_stmt_set_aug(p->arena, lhs, aug_op, rhs);
        return mq_stmt_set(p->arena, lhs, rhs);
    }
    
    if (t.kind == MQL_Tok_If) {
        p_advance(p);
        typedef struct BN { MQ_IfBranch b; struct BN *next; } BN;
        BN *head = 0, *tail = 0;
        u32 count = 0;
        
        MQ_Expr *cond = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        BN *n = arena_alloc_zero(p->arena, sizeof(BN));
        n->b = mq_if_branch(cond, body);
        head = tail = n; count++;
        
        while (p_peek_nc(p).kind == MQL_Tok_Elif) {
            p_advance(p);
            cond = parse_expr(p);
            body = parse_block(p);
            n = arena_alloc_zero(p->arena, sizeof(BN));
            n->b = mq_if_branch(cond, body);
            tail->next = n; tail = n; count++;
        }
        
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
    
    if (t.kind == MQL_Tok_For) {
        p_advance(p);
        MQL_Token var = p_expect_name(p);
        MQ_Type *var_type = 0;
        if (p_eat(p, MQL_Tok_Colon)) var_type = parse_type(p);
        p_expect(p, MQL_Tok_In);
        MQ_Expr *iter = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_for(p->arena, tok_str(var), var_type, iter, body);
    }
    
    if (t.kind == MQL_Tok_While) {
        p_advance(p);
        MQ_Expr *cond = parse_expr(p);
        MQ_Stmt *body = parse_block(p);
        return mq_stmt_while(p->arena, cond, body);
    }
    
    if (t.kind == MQL_Tok_Break) {
        p_advance(p); p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_break(p->arena);
    }
    if (t.kind == MQL_Tok_Continue) {
        p_advance(p); p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_continue(p->arena);
    }
    if (t.kind == MQL_Tok_Return) {
        p_advance(p);
        MQ_Expr *val = 0;
        if (p_peek_nc(p).kind != MQL_Tok_Semicolon && p_peek_nc(p).kind != MQL_Tok_RBrace)
            val = parse_expr(p);
        p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_return(p->arena, val);
    }
    
    if (t.kind == MQL_Tok_Measure) {
        p_advance(p);
        MQL_IdList src_qs = parse_qubit_range(p);
        
        StmtBuilder sb;
        sb_init(&sb, p->arena);
        
        if (p_eat(p, MQL_Tok_Arrow)) {
            string c_name = {0};
            MQL_IdList dst_c = parse_cbit_range(p, &c_name);
            u32 count = src_qs.count < dst_c.count ? src_qs.count : dst_c.count;
            if (count == 0 && src_qs.count >= 1 && dst_c.count >= 1) count = 1;
            
            for (u32 i = 0; i < count; i++) {
                MQ_Instruction instr = mq_instr_measure(src_qs.ids[i], dst_c.ids[i]);
                sb_add_gate(&sb, instr);
            }
        } else {
            MQL_Symbol *c_sym = scope_find(p, str_lit("c"));
            if (c_sym && c_sym->kind == MQL_Sym_Bit) {
                u32 count = src_qs.count < c_sym->size ? src_qs.count : c_sym->size;
                for (u32 i = 0; i < count; i++) {
                    sb_add_gate(&sb, mq_instr_measure(src_qs.ids[i], c_sym->base_id + i));
                }
            } else {
                for (u32 i = 0; i < src_qs.count; i++) {
                    sb_add_gate(&sb, mq_instr_measure_discard(src_qs.ids[i]));
                }
            }
        }
        p_eat(p, MQL_Tok_Semicolon);
        if (sb.count == 1) return sb.stmts[0];
        return mq_stmt_block(p->arena, sb.stmts, sb.count);
    }
    
    if (t.kind == MQL_Tok_Reset) {
        p_advance(p);
        MQL_IdList qs = parse_qubit_range(p);
        p_eat(p, MQL_Tok_Semicolon);
        if (qs.count == 1) {
            return mq_stmt_instr(p->arena, mq_instr_reset(qs.ids[0]));
        }
        StmtBuilder sb;
        sb_init(&sb, p->arena);
        for (u32 i = 0; i < qs.count; i++)
            sb_add_gate(&sb, mq_instr_reset(qs.ids[i]));
        return mq_stmt_block(p->arena, sb.stmts, sb.count);
    }
    
    if (t.kind == MQL_Tok_Barrier) {
        p_advance(p);
        u32 qubits[MQ_MAX_GATE_QUBITS];
        u8 qcount = 0;
        if (p_eat(p, MQL_Tok_Star)) {
            // barrier *
        } else {
            while (is_name_token(p_peek_nc(p).kind) && qcount < MQ_MAX_GATE_QUBITS) {
                MQL_IdList qs = parse_qubit_range(p);
                for (u32 i = 0; i < qs.count && qcount < MQ_MAX_GATE_QUBITS; i++)
                    qubits[qcount++] = qs.ids[i];
                p_eat(p, MQL_Tok_Comma);
            }
        }
        p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, mq_instr_barrier(qubits, qcount));
    }
    
    if (t.kind == MQL_Tok_Delay) {
        p_advance(p);
        MQL_Token time_tok = p_expect(p, MQL_Tok_TimeLit);
        string lex = time_tok.lexeme;
        u64 num_end = 0;
        while (num_end < lex.size &&
               (is_digit(lex.str[num_end]) || lex.str[num_end] == '.' ||
                lex.str[num_end] == 'e'    || lex.str[num_end] == 'E' ||
                ((lex.str[num_end] == '+' || lex.str[num_end] == '-') && num_end > 0)))
            num_end++;
        
        char num_buf[64] = {0};
        u64 copy_len = num_end < 63 ? num_end : 63;
        memcpy(num_buf, lex.str, copy_len);
        f64 duration = strtod(num_buf, 0);
        
        string unit_str = { .str = lex.str + num_end, .size = lex.size - num_end };
        MQ_TimeUnit unit = MQ_Time_ns;
        if      (lexeme_eq_cstr(unit_str, "dt")) unit = MQ_Time_Dt;
        else if (lexeme_eq_cstr(unit_str, "ns")) unit = MQ_Time_ns;
        else if (lexeme_eq_cstr(unit_str, "us")) unit = MQ_Time_us;
        else if (lexeme_eq_cstr(unit_str, "ms")) unit = MQ_Time_ms;
        else if (lexeme_eq_cstr(unit_str, "s"))  unit = MQ_Time_s;
        
        u32 qubits[MQ_MAX_GATE_QUBITS];
        u8 qcount = 0;
        while (is_name_token(p_peek_nc(p).kind) && qcount < MQ_MAX_GATE_QUBITS) {
            MQL_IdList qs = parse_qubit_range(p);
            for (u32 i = 0; i < qs.count && qcount < MQ_MAX_GATE_QUBITS; i++)
                qubits[qcount++] = qs.ids[i];
            p_eat(p, MQL_Tok_Comma);
        }
        p_eat(p, MQL_Tok_Semicolon);
        return mq_stmt_instr(p->arena, mq_instr_delay(qubits, qcount, duration, unit));
    }
    
    if (t.kind == MQL_Tok_MacroGroverOracle) {
        p_advance(p);
        p_expect(p, MQL_Tok_LParen);
        MQL_IdList qs = parse_qubit_range(p);
        p_expect(p, MQL_Tok_Comma);
        MQL_Token pat_tok = p_expect(p, MQL_Tok_StringLit);
        p_expect(p, MQL_Tok_RParen);
        p_eat(p, MQL_Tok_Semicolon);
        
        string pat = pat_tok.lexeme;
        if (pat.size >= 2 && pat.str[0] == '"' && pat.str[pat.size - 1] == '"') {
            pat.str += 1;
            pat.size -= 2;
        }
        return expand_grover_oracle(p, qs, pat);
    }
    
    if (t.kind == MQL_Tok_MacroGroverDiffuser) {
        p_advance(p);
        p_expect(p, MQL_Tok_LParen);
        MQL_IdList qs = parse_qubit_range(p);
        p_expect(p, MQL_Tok_RParen);
        p_eat(p, MQL_Tok_Semicolon);
        return expand_grover_diffuser(p, qs);
    }
    
    if (t.kind == MQL_Tok_MacroBellPair) {
        p_advance(p);
        p_expect(p, MQL_Tok_LParen);
        MQL_IdList q0 = parse_qubit_range(p);
        p_expect(p, MQL_Tok_Comma);
        MQL_IdList q1 = parse_qubit_range(p);
        p_expect(p, MQL_Tok_RParen);
        p_eat(p, MQL_Tok_Semicolon);
        if (q0.count >= 1 && q1.count >= 1)
            return expand_bell_pair(p, q0.ids[0], q1.ids[0]);
        return mq_stmt_block(p->arena, 0, 0);
    }
    
    if (t.kind == MQL_Tok_MacroQFT || t.kind == MQL_Tok_MacroIQFT) {
        b8 is_inv = (t.kind == MQL_Tok_MacroIQFT);
        p_advance(p);
        p_expect(p, MQL_Tok_LParen);
        MQL_IdList qs = parse_qubit_range(p);
        p_expect(p, MQL_Tok_RParen);
        p_eat(p, MQL_Tok_Semicolon);
        return expand_qft(p, qs, is_inv);
    }
    
    if (t.kind == MQL_Tok_MacroTeleport) {
        p_advance(p);
        p_expect(p, MQL_Tok_LParen);
        MQL_IdList q0 = parse_qubit_range(p);
        p_expect(p, MQL_Tok_Comma);
        MQL_IdList q1 = parse_qubit_range(p);
        p_expect(p, MQL_Tok_Comma);
        MQL_IdList q2 = parse_qubit_range(p);
        p_expect(p, MQL_Tok_RParen);
        p_eat(p, MQL_Tok_Semicolon);
        
        StmtBuilder sb;
        sb_init(&sb, p->arena);
        if (q0.count >= 1 && q1.count >= 1 && q2.count >= 1) {
            sb_add(&sb, expand_bell_pair(p, q1.ids[0], q2.ids[0]));
            u32 t1[1] = { q1.ids[0] };
            MQ_Instruction cx = mq_instr_gate(MQ_Gate_X, t1, 1);
            mq_instr_add_control(&cx, q0.ids[0], 1);
            sb_add_gate(&sb, cx);
            u32 t0[1] = { q0.ids[0] };
            sb_add_gate(&sb, mq_instr_gate(MQ_Gate_H, t0, 1));
        }
        return mq_stmt_block(p->arena, sb.stmts, sb.count);
    }
    
    // Gate statement or Subroutine call
    {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        MQ_GateType dummy;
        if (pk == MQL_Tok_Ctrl || pk == MQL_Tok_Ctrl0 ||
            pk == MQL_Tok_GateCNOT || pk == MQL_Tok_GateCZ ||
            pk == MQL_Tok_GateCP || pk == MQL_Tok_GateCRX ||
            pk == MQL_Tok_GateCRY || pk == MQL_Tok_GateCRZ ||
            tok_to_gate_type(pk, &dummy) ||
            is_name_token(pk)) {
            return parse_gate_statement(p);
        }
    }
    
    p_advance(p);
    p_error(p, t, "unexpected statement token");
    return 0;
}

static MQ_Stmt *parse_block(MQL_Parser *p) {
    p_expect(p, MQL_Tok_LBrace);
    
    typedef struct SN { MQ_Stmt *s; struct SN *next; } SN;
    SN *head = 0, *tail = 0;
    u32 count = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RBrace && p_peek_nc(p).kind != MQL_Tok_Eof) {
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

/* ======================================================================
 * Formal Parameter List & Routine Definitions
 * ====================================================================== */

static void parse_formal_list(MQL_Parser *p, MQ_FormalParam **out_params, u32 *out_count) {
    typedef struct FN { MQ_FormalParam fp; struct FN *next; } FN;
    FN *head = 0, *tail = 0;
    u32 count = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RParen && p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQL_Token name = p_expect_name(p);
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

static void scope_add_formal_params(MQL_Parser *p, MQ_FormalParam *params, u32 count) {
    u32 next_q = 0, next_c = 0;
    for (u32 i = 0; i < count; i++) {
        MQ_Type *ty = params[i].type;
        if (!ty) continue;
        if (ty->kind == MQ_Type_Qubit) {
            scope_add(p, params[i].name, MQL_Sym_Qubit, next_q++, 1);
        } else if (ty->kind == MQ_Type_QubitReg) {
            /* width==0 means qubit[] (open/unknown size). Use a large sentinel
             * so that any qubit index (e.g. qs[0], qs[1], qs[2]) is accepted
             * without an "index out of range" error during parsing. */
            u32 w = ty->width ? ty->width : 64;
            scope_add(p, params[i].name, MQL_Sym_Qubit, next_q, w);
            next_q += w;
        } else if (ty->kind == MQ_Type_Bit) {
            scope_add(p, params[i].name, MQL_Sym_Bit, next_c++, 1);
        } else if (ty->kind == MQ_Type_BitReg) {
            u32 w = ty->width ? ty->width : 1;
            scope_add(p, params[i].name, MQL_Sym_Bit, next_c, w);
            next_c += w;
        }
    }
}

static MQ_Routine *parse_gate_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Gate);
    MQL_Token name = p_expect_name(p);
    p_expect(p, MQL_Tok_LParen);
    MQ_FormalParam *params = 0;
    u32 param_count = 0;
    parse_formal_list(p, &params, &param_count);
    p_expect(p, MQL_Tok_RParen);
    scope_clear(p);
    scope_add_formal_params(p, params, param_count);
    MQ_Stmt *body = parse_block(p);
    return mq_routine(p->arena, tok_str(name), MQ_Routine_Gate, params, param_count, 0, body);
}

static MQ_Routine *parse_operation_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Operation);
    MQL_Token name = p_expect_name(p);
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
    return mq_routine(p->arena, tok_str(name), MQ_Routine_Operation, params, param_count, ret, body);
}

/* ======================================================================
 * Circuit Definition Parsing
 * ====================================================================== */

static MQ_Circuit *parse_circuit_def(MQL_Parser *p) {
    p_expect(p, MQL_Tok_Circuit);
    MQL_Token name = p_expect_name(p);
    
    if (p_eat(p, MQL_Tok_LParen)) {
        while (p_peek_nc(p).kind != MQL_Tok_RParen && p_peek_nc(p).kind != MQL_Tok_Eof) {
            p_expect_name(p);
            p_expect(p, MQL_Tok_Colon);
            parse_type(p);
            if (p_eat(p, MQL_Tok_Eq)) parse_expr(p);
            if (!p_eat(p, MQL_Tok_Comma)) break;
        }
        p_expect(p, MQL_Tok_RParen);
    }
    
    p_expect(p, MQL_Tok_LBrace);
    scope_clear(p);
    p->auto_cbits_count = 0;
    
    typedef struct RegNode { MQ_Register r; struct RegNode *next; } RegNode;
    RegNode *reg_head = 0, *reg_tail = 0;
    u32 reg_count = 0;
    u32 total_qubits = 0, total_cbits = 0;
    
    typedef struct ParamNode { string name; MQ_Expr *def; struct ParamNode *next; } ParamNode;
    ParamNode *pn_head = 0, *pn_tail = 0;
    u32 param_count = 0;
    
    while (1) {
        MQL_TokenKind pk = p_peek_nc(p).kind;
        
        if (pk == MQL_Tok_Qubit) {
            p_advance(p);
            MQL_Token rname = p_expect_name(p);
            u32 size = 1;
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token sz = p_expect(p, MQL_Tok_IntLit);
                size = (u32)strtol((char*)sz.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
            }
            p_eat(p, MQL_Tok_Semicolon);
            
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
            MQL_Token rname = p_expect_name(p);
            u32 size = 1;
            if (p_eat(p, MQL_Tok_LBracket)) {
                MQL_Token sz = p_expect(p, MQL_Tok_IntLit);
                size = (u32)strtol((char*)sz.lexeme.str, 0, 10);
                p_expect(p, MQL_Tok_RBracket);
            }
            p_eat(p, MQL_Tok_Semicolon);
            
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
            MQL_Token pname = p_expect_name(p);
            if (p_peek_nc(p).kind == MQL_Tok_Colon) {
                p_advance(p);
                parse_type(p);
            }
            MQ_Expr *def = 0;
            if (p_eat(p, MQL_Tok_Eq)) def = parse_expr(p);
            p_eat(p, MQL_Tok_Semicolon);
            
            ParamNode *pn = arena_alloc_zero(p->arena, sizeof(ParamNode));
            pn->name = tok_str(pname);
            pn->def  = def;
            if (!pn_head) pn_head = pn; else pn_tail->next = pn;
            pn_tail = pn; param_count++;
            continue;
        }
        
        break;
    }
    
    p->auto_cbits_count = total_cbits;
    
    typedef struct SN { MQ_Stmt *s; struct SN *next; } SN;
    SN *shead = 0, *stail = 0;
    u32 scount = 0;
    
    while (p_peek_nc(p).kind != MQL_Tok_RBrace && p_peek_nc(p).kind != MQL_Tok_Eof) {
        MQ_Stmt *s = parse_stmt(p);
        if (!s) continue;
        SN *n = arena_alloc_zero(p->arena, sizeof(SN));
        n->s = s;
        if (!shead) shead = n; else stail->next = n;
        stail = n; scount++;
    }
    p_expect(p, MQL_Tok_RBrace);
    
    for (u32 s_idx = 0; s_idx < p->scope_count; s_idx++) {
        MQL_Symbol *sym = &p->scope[s_idx];
        if (sym->kind == MQL_Sym_Bit) {
            b8 already_in_regs = 0;
            for (RegNode *rn = reg_head; rn; rn = rn->next) {
                if (str_eq(rn->r.name, sym->name)) {
                    already_in_regs = 1;
                    break;
                }
            }
            if (!already_in_regs) {
                MQ_Register r = mq_register_classical(sym->name, sym->size, sym->base_id);
                RegNode *n = arena_alloc_zero(p->arena, sizeof(RegNode));
                n->r = r;
                if (!reg_head) reg_head = n; else reg_tail->next = n;
                reg_tail = n; reg_count++;
                if (sym->base_id + sym->size > total_cbits)
                    total_cbits = sym->base_id + sym->size;
            }
        }
    }
    
    MQ_Register *regs = 0;
    if (reg_count > 0) {
        regs = arena_alloc_array(p->arena, MQ_Register, reg_count);
        RegNode *c = reg_head;
        for (u32 i = 0; i < reg_count; i++, c = c->next) regs[i] = c->r;
    }
    
    MQ_Circuit *circuit = mq_circuit(p->arena, tok_str(name), regs, reg_count, total_qubits, total_cbits);
    
    ParamNode *pnc = pn_head;
    for (u32 i = 0; i < param_count; i++, pnc = pnc->next)
        mq_circuit_add_param(p->arena, circuit, pnc->name, pnc->def);
    
    if (scount > 0) {
        MQ_Stmt **arr = arena_alloc_array(p->arena, MQ_Stmt *, scount);
        SN *c = shead;
        for (u32 i = 0; i < scount; i++, c = c->next) arr[i] = c->s;
        circuit->body = mq_stmt_block(p->arena, arr, scount);
    }
    
    return circuit;
}

/* ======================================================================
 * Program Parser Entry Point
 * ====================================================================== */

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
            p_next(p);
        } else if (t.kind == MQL_Tok_Pragma) {
            p_advance(p);
            if (p_peek_nc(p).kind == MQL_Tok_Ident)     p_advance(p);
            if (p_peek_nc(p).kind == MQL_Tok_StringLit) p_advance(p);
        } else {
            p_advance(p);
            p_error(p, t, "expected 'gate', 'operation', or 'circuit'");
            if (p->had_error) break;
        }
    }
    
    return p->had_error ? 0 : prog;
}

/* ======================================================================
 * MQL Emitter
 * ====================================================================== */

typedef struct EmitCtx {
    FILE             *f;
    M_Arena          *arena;
    int               indent;
    MQ_QubitNameTable qnames;
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
        /* width==0 means open/unknown size — emit qubit[] instead of qubit[0] */
        case MQ_Type_QubitReg: if (t->width) fprintf(ec->f, "qubit[%u]", t->width); else fprintf(ec->f, "qubit[]"); break;
        case MQ_Type_Array:
        emit_type(ec, t->element_type);
        fprintf(ec->f, "[%u]", t->width);
        break;
    }
}

static const char *binop_sym_mql[] = {
    "+", "-", "*", "/", "%", "**",
    "&", "|", "^", "<<", ">>",
    "==", "!=", "<", "<=", ">", ">=",
    "&&", "||"
};

static const char *unop_fn_mql[] = {
    0, 0, 0,
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
        fprintf(ec->f, "%.17g", e->lit.float_val);
        break;
        case MQ_Expr_Symbol:
        case MQ_Expr_Var:
        if (e->name.str) {
            u64 sz = e->name.size;
            const char *n = (const char *)e->name.str;
            if ((sz == 2 && strncmp(n, "pi", 2) == 0) ||
                (sz == 2 && strncmp(n, "PI", 2) == 0) ||
                (sz == 7 && strncmp(n, "math.pi", 7) == 0) ||
                (sz == 5 && strncmp(n, "np.pi", 5) == 0)) {
                fprintf(ec->f, "pi");
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
        if (e->kind == MQ_Expr_Call && e->call.arg_count == 0 &&
            e->call.name.str && e->call.name.size == 2 &&
            strncmp((char *)e->call.name.str, "PI", 2) == 0) {
            fprintf(ec->f, "pi");
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
    "CCX", "CSWAP", 0, 0
};

static void emit_instr(EmitCtx *ec, MQ_Instruction *instr) {
    ec_indent(ec);
    
    switch (instr->type) {
        case MQ_Instr_Gate: {
            for (u8 i = 0; i < instr->gate.control_count; i++) {
                fprintf(ec->f, instr->gate.control_states[i] ? "ctrl" : "ctrl0");
                fprintf(ec->f, "[");
                emit_qubit(ec, instr->gate.controls[i]);
                fprintf(ec->f, "] ");
            }
            
            if (instr->gate.gate == MQ_Gate_Custom) {
                ec_str(ec, instr->gate.custom_name);
            } else if (instr->gate.gate == MQ_Gate_Unitary) {
                fprintf(ec->f, "unitary_gate /* unitary */");
            } else {
                fprintf(ec->f, "%s", gate_name_mql[instr->gate.gate]);
            }
            
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
            
            for (u8 i = 0; i < instr->qubit_count; i++) {
                fprintf(ec->f, " ");
                emit_qubit(ec, instr->qubits[i]);
            }
            
            if (instr->gate.has_classical_cond) {
                fprintf(ec->f, " if c[%u] == %u",
                        instr->gate.classical_bit, instr->gate.classical_val);
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
            fprintf(ec->f, "delay %.17g%s", instr->delay.duration, unit_str[instr->delay.unit]);
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
        
        case MQ_Stmt_Set:
        ec_indent(ec);
        fprintf(ec->f, "set ");
        emit_expr(ec, s->set.lhs);
        if (s->set.augmented) fprintf(ec->f, " %s= ", binop_sym_mql[s->set.aug_op]);
        else                  fprintf(ec->f, " = ");
        emit_expr(ec, s->set.rhs);
        fprintf(ec->f, ";\n");
        break;
        
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
        ec_indent(ec); fprintf(ec->f, "return");
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
        fprintf(ec->f, "// intrinsic %s -> ", r->kind == MQ_Routine_Gate ? "gate" : "operation");
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
    if (r->kind == MQ_Routine_Operation && r->return_type && r->return_type->kind != MQ_Type_Void) {
        fprintf(ec->f, " -> ");
        emit_type(ec, r->return_type);
    }
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
    
    ec->qnames = mq_qubit_names_from_circuit(ec->arena, c);
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
    
    for (u32 i = 0; i < prog->routine_count; i++)
        emit_routine(&ec, prog->routines[i]);
    for (u32 i = 0; i < prog->circuit_count; i++)
        emit_circuit(&ec, prog->circuits[i]);
    
    arena_free(&scratch);
}
