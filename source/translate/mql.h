#ifndef MQL_H
#define MQL_H

#include <stdio.h>
#include "defines.h"
#include "base/str.h"
#include "base/mem.h"
#include "ir.h"

/* ======================================================================
 * MQL (MetaQuantum Language) Frontend
 *
 * User-friendly, expressive quantum domain-specific language supporting:
 *  - Pseudo-code quantum algorithms (Grover, QFT, Teleportation, Bell pairs)
 *  - Vectorized qubit addressing & slice ranges: q[0..1], q[0, 1], q
 *  - Built-in algorithmic macros: grover_oracle, grover_diffuser, qft, bell_pair
 *  - Vectorized register measurements: measure q -> c[2], measure q -> c
 *  - Cross-compilation to Qiskit, Cirq, Q#, and MQ IR
 * ====================================================================== */

/* ======================================================================
 * Token kinds
 * ====================================================================== */

typedef enum MQL_TokenKind {
    // Special
    MQL_Tok_Eof,
    MQL_Tok_Error,
    MQL_Tok_Comment,
    
    // Literals
    MQL_Tok_Ident,
    MQL_Tok_IntLit,
    MQL_Tok_FloatLit,
    MQL_Tok_StringLit,
    MQL_Tok_TimeLit,
    
    // Language keywords
    MQL_Tok_Gate,
    MQL_Tok_Operation,
    MQL_Tok_Circuit,
    MQL_Tok_Param,
    MQL_Tok_Let,
    MQL_Tok_Set,
    MQL_Tok_Qubit,
    MQL_Tok_Bit,
    MQL_Tok_Measure,
    MQL_Tok_Reset,
    MQL_Tok_Barrier,
    MQL_Tok_Delay,
    MQL_Tok_Adjoint,
    MQL_Tok_Ctrl,
    MQL_Tok_Ctrl0,
    MQL_Tok_If,
    MQL_Tok_Elif,
    MQL_Tok_Else,
    MQL_Tok_For,
    MQL_Tok_In,
    MQL_Tok_While,
    MQL_Tok_Break,
    MQL_Tok_Continue,
    MQL_Tok_Return,
    MQL_Tok_True,
    MQL_Tok_False,
    MQL_Tok_Void,
    MQL_Tok_Bool,
    MQL_Tok_Int,
    MQL_Tok_Float,
    MQL_Tok_Angle,
    MQL_Tok_String,
    
    // Built-in math functions
    MQL_Tok_Sin,
    MQL_Tok_Cos,
    MQL_Tok_Tan,
    MQL_Tok_Asin,
    MQL_Tok_Acos,
    MQL_Tok_Atan,
    MQL_Tok_Sqrt,
    MQL_Tok_Exp,
    MQL_Tok_Log,
    MQL_Tok_Abs,
    MQL_Tok_Pi,
    
    // Built-in gate names & aliases
    MQL_Tok_GateI,
    MQL_Tok_GateH,
    MQL_Tok_GateX,
    MQL_Tok_GateY,
    MQL_Tok_GateZ,
    MQL_Tok_GateS,
    MQL_Tok_GateSdg,
    MQL_Tok_GateT,
    MQL_Tok_GateTdg,
    MQL_Tok_GateP,
    MQL_Tok_GateRX,
    MQL_Tok_GateRY,
    MQL_Tok_GateRZ,
    MQL_Tok_GateU,
    MQL_Tok_GateSWAP,
    MQL_Tok_GateISWAP,
    MQL_Tok_GateRZZ,
    MQL_Tok_GateRXX,
    MQL_Tok_GateRYY,
    MQL_Tok_GateCCX,      // Toffoli
    MQL_Tok_GateCSWAP,    // Fredkin
    MQL_Tok_GateCNOT,     // CX
    MQL_Tok_GateCZ,
    MQL_Tok_GateCP,
    MQL_Tok_GateCRX,
    MQL_Tok_GateCRY,
    MQL_Tok_GateCRZ,
    
    // Built-in algorithmic macros / subroutines
    MQL_Tok_MacroGroverOracle,
    MQL_Tok_MacroGroverDiffuser,
    MQL_Tok_MacroBellPair,
    MQL_Tok_MacroQFT,
    MQL_Tok_MacroIQFT,
    MQL_Tok_MacroTeleport,
    
    // Punctuation & Operators
    MQL_Tok_LParen,       // (
    MQL_Tok_RParen,       // )
    MQL_Tok_LBrace,       // {
    MQL_Tok_RBrace,       // }
    MQL_Tok_LBracket,     // [
    MQL_Tok_RBracket,     // ]
    MQL_Tok_Semicolon,    // ;
    MQL_Tok_Comma,        // ,
    MQL_Tok_Colon,        // :
    MQL_Tok_Dot,          // .
    MQL_Tok_DotDot,       // ..
    MQL_Tok_Star,         // *
    MQL_Tok_StarStar,     // **
    MQL_Tok_Plus,         // +
    MQL_Tok_Minus,        // -
    MQL_Tok_Slash,        // /
    MQL_Tok_Percent,      // %
    MQL_Tok_Tilde,        // ~
    MQL_Tok_Pragma,       // #pragma
    
    // Relational / logical
    MQL_Tok_Eq,           // =
    MQL_Tok_EqEq,         // ==
    MQL_Tok_Bang,         // !
    MQL_Tok_BangEq,       // !=
    MQL_Tok_Lt,           // <
    MQL_Tok_LtEq,         // <=
    MQL_Tok_Gt,           // >
    MQL_Tok_GtEq,         // >=
    MQL_Tok_AmpAmp,       // &&
    MQL_Tok_PipePipe,     // ||
    MQL_Tok_Arrow,        // ->
    
    // Bitwise
    MQL_Tok_Amp,          // &
    MQL_Tok_Pipe,         // |
    MQL_Tok_Caret,        // ^
    MQL_Tok_LtLt,         // <<
    MQL_Tok_GtGt,         // >>
    
    // Augmented assignment
    MQL_Tok_PlusEq,
    MQL_Tok_MinusEq,
    MQL_Tok_StarEq,
    MQL_Tok_SlashEq,
    MQL_Tok_AmpEq,
    MQL_Tok_PipeEq,
    MQL_Tok_CaretEq,
    MQL_Tok_LtLtEq,
    MQL_Tok_GtGtEq,
} MQL_TokenKind;

/* ======================================================================
 * Token & Lexer
 * ====================================================================== */

typedef struct MQL_Token {
    MQL_TokenKind kind;
    string        lexeme;
    u32           line;
    u32           col;
} MQL_Token;

typedef struct MQL_Lexer {
    string    src;
    u64       pos;
    u32       line;
    u32       col;
    MQL_Token peeked;
    b8        has_peeked;
} MQL_Lexer;

void        mql_lexer_init(MQL_Lexer *lex, string src);
MQL_Token   mql_next(MQL_Lexer *lex);
MQL_Token   mql_peek(MQL_Lexer *lex);
const char *mql_token_kind_str(MQL_TokenKind kind);

/* ======================================================================
 * Symbol Table & Parser
 * ====================================================================== */

typedef struct MQL_Error {
    string            message;
    u32               line;
    u32               col;
    struct MQL_Error *next;
} MQL_Error;

typedef enum MQL_SymKind {
    MQL_Sym_Qubit,
    MQL_Sym_Bit,
} MQL_SymKind;

typedef struct MQL_Symbol {
    string      name;
    MQL_SymKind kind;
    u32         base_id;
    u32         size;
} MQL_Symbol;

#define MQL_SCOPE_MAX 128

typedef struct MQL_Parser {
    MQL_Lexer  lex;
    M_Arena   *arena;
    MQL_Error *errors;
    MQL_Error *errors_tail;
    b8         had_error;
    
    // Symbol table for current circuit/routine scope
    MQL_Symbol scope[MQL_SCOPE_MAX];
    u32        scope_count;
    
    // Tracking active circuit/routine for implicit auto-declarations
    u32        auto_cbits_count;
} MQL_Parser;

void        mql_parser_init(MQL_Parser *p, M_Arena *arena, string src);
MQ_Program *mql_parse_program(MQL_Parser *p);

/* ======================================================================
 * Emitter
 * ====================================================================== */

void        mql_emit(FILE *out, MQ_Program *prog);

#endif // MQL_H