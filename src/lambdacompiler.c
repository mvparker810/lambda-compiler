
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "lambdacompiler.h"
#include "instruction_builder.h"

typedef enum TokenType {
    TOK_EOF,
    TOK_INVALID,

    // Keywords
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_CONTINUE,
    TOK_BREAK,

    // Type keywords
    TOK_INT8,

    // Delimiters
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_COMMA,
    TOK_SEMI,

    // Arithmetic
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,

    // Assignment / comparison
    TOK_ASSIGN,      // =
    TOK_EQ,          // ==
    TOK_NEQ,         // !=
    TOK_LT, TOK_LTE, // < <=
    TOK_GT, TOK_GTE, // > >=

    // Literals / identifiers
    TOK_NUMBER,
    TOK_IDENTIFIER,
} TokenType;

typedef struct Token {
    TokenType type;
    union {
        struct {
            char* str;      // strdup'd, must be freed
        } identifier;
        struct {
            uint64_t n;
        } immediate;
    } data;
    uint64_t row, col;
} Token;

// -----------------------------------------------------------------------
// AST
// -----------------------------------------------------------------------

typedef enum NodeType {
    // Expressions
    NODE_NUMBER,        // 42
    NODE_IDENTIFIER,    // x
    NODE_BINOP,         // a + b, a = b, a == b, ...
    NODE_CALL,          // foo(a, b)

    // Statements
    NODE_BLOCK,         // { stmt; stmt; ... }
    NODE_VAR_DECL,      // int8 x = expr;
    NODE_EXPR_STMT,     // expr;
    NODE_RETURN,        // return expr;
    NODE_BREAK,         // break;
    NODE_CONTINUE,      // continue;
    NODE_IF,            // if (cond) then [else alt]
    NODE_WHILE,         // while (cond) body
    NODE_FOR,           // for (init; cond; post) body

    // Top-level
    NODE_FUNC_DEF,      // int8 foo(int8 a) { ... }
} NodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    ASTNode**   nodes;
    size_t      len;
    size_t      cap;
} NodeList;

struct ASTNode {
    NodeType type;
    union {
        // NODE_NUMBER
        struct { uint64_t value; } number;

        // NODE_IDENTIFIER
        struct { char* name; } identifier;  // strdup'd

        // NODE_BINOP  (also covers assignment: TOK_ASSIGN)
        struct { TokenType op; ASTNode* left; ASTNode* right; } binop;

        // NODE_CALL
        struct { char* name; NodeList args; } call;  // name strdup'd

        // NODE_BLOCK
        struct { NodeList stmts; } block;

        // NODE_VAR_DECL   int8 x = expr;
        struct { char* name; ASTNode* init; } var_decl;  // name strdup'd, init may be NULL

        // NODE_EXPR_STMT / NODE_RETURN  (expr may be NULL for bare return)
        struct { ASTNode* expr; } expr_stmt;

        // NODE_IF
        struct { ASTNode* cond; ASTNode* then_block; ASTNode* else_block; } if_stmt;  // else_block may be NULL

        // NODE_WHILE
        struct { ASTNode* cond; ASTNode* body; } while_stmt;

        // NODE_FOR
        struct { ASTNode* init; ASTNode* cond; ASTNode* post; ASTNode* body; } for_stmt;

        // NODE_FUNC_DEF
        struct {
            char*    name;      // strdup'd
            NodeList params;    // each param is a NODE_VAR_DECL (init == NULL)
            ASTNode* body;      // NODE_BLOCK
        } func_def;
    } data;
};

static ASTNode* ast_node_alloc(NodeType type) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = type;
    return n;
}

static void nodelist_push(NodeList* list, ASTNode* node) {
    if (list->len >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 4;
        list->nodes = realloc(list->nodes, sizeof(ASTNode*) * list->cap);
    }
    list->nodes[list->len++] = node;
}

// Forward declaration — ast_node_free is recursive
static void ast_node_free(ASTNode* node);

static void nodelist_free(NodeList* list) {
    for (size_t i = 0; i < list->len; i++) ast_node_free(list->nodes[i]);
    free(list->nodes);
    list->nodes = NULL;
    list->len = list->cap = 0;
}

static void ast_node_free(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case NODE_IDENTIFIER:  free(node->data.identifier.name);  break;
        case NODE_BINOP:       ast_node_free(node->data.binop.left);
                               ast_node_free(node->data.binop.right); break;
        case NODE_CALL:        free(node->data.call.name);
                               nodelist_free(&node->data.call.args); break;
        case NODE_BLOCK:       nodelist_free(&node->data.block.stmts); break;
        case NODE_VAR_DECL:    free(node->data.var_decl.name);
                               ast_node_free(node->data.var_decl.init); break;
        case NODE_EXPR_STMT:
        case NODE_RETURN:      ast_node_free(node->data.expr_stmt.expr); break;
        case NODE_IF:          ast_node_free(node->data.if_stmt.cond);
                               ast_node_free(node->data.if_stmt.then_block);
                               ast_node_free(node->data.if_stmt.else_block); break;
        case NODE_WHILE:       ast_node_free(node->data.while_stmt.cond);
                               ast_node_free(node->data.while_stmt.body); break;
        case NODE_FOR:         ast_node_free(node->data.for_stmt.init);
                               ast_node_free(node->data.for_stmt.cond);
                               ast_node_free(node->data.for_stmt.post);
                               ast_node_free(node->data.for_stmt.body); break;
        case NODE_FUNC_DEF:    free(node->data.func_def.name);
                               nodelist_free(&node->data.func_def.params);
                               ast_node_free(node->data.func_def.body); break;
        default: break;
    }
    free(node);
}

// -----------------------------------------------------------------------

static struct LAMBDA_COMPILER {
    struct {
        Token* tokens;
        size_t token_len;
        size_t token_cap;
    } lexer;
    struct {
        NodeList top_level;  // list of top-level NODE_FUNC_DEF nodes
    } parser;
} LAMBDA_COMPILER;


int LMBACOMPILER_TOKENIZE_PushToken(Token tok) {
    if (LAMBDA_COMPILER.lexer.token_len >= LAMBDA_COMPILER.lexer.token_cap) {
        LAMBDA_COMPILER.lexer.token_cap *= 2;
        LAMBDA_COMPILER.lexer.tokens = realloc(LAMBDA_COMPILER.lexer.tokens, sizeof(Token) * LAMBDA_COMPILER.lexer.token_cap);
    }
    LAMBDA_COMPILER.lexer.tokens[LAMBDA_COMPILER.lexer.token_len++] = tok;
    return 0;
}

// Returns the keyword token type for `word`, or TOK_INVALID if not a keyword.
static TokenType keyword_lookup(const char* word) {
    if (!strcmp(word, "if"))       return TOK_IF;
    if (!strcmp(word, "else"))     return TOK_ELSE;
    if (!strcmp(word, "while"))    return TOK_WHILE;
    if (!strcmp(word, "for"))      return TOK_FOR;
    if (!strcmp(word, "return"))   return TOK_RETURN;
    if (!strcmp(word, "continue")) return TOK_CONTINUE;
    if (!strcmp(word, "break"))    return TOK_BREAK;
    if (!strcmp(word, "int8"))     return TOK_INT8;
    return TOK_INVALID;
}

void LMBACOMPILER_TOKENIZE(Lambda_Worker* WRKING_DATA) {
    FILE* f = WRKING_DATA->input_file;
    uint64_t row = 1, col = 1;

    int c = fgetc(f);

    while (c != EOF) {

        // --- Skip whitespace ---
        if (c == ' ' || c == '\t' || c == '\r') {
            col++;
            c = fgetc(f);
            continue;
        }
        if (c == '\n') {
            row++; col = 1;
            c = fgetc(f);
            continue;
        }

        // --- Skip line comments (// ...) ---
        if (c == '/') {
            int next = fgetc(f);
            if (next == '/') {
                while (c != '\n' && c != EOF) c = fgetc(f);
                continue;
            }
            // Not a comment — emit TOK_SLASH and put `next` back
            Token tok = { .type = TOK_SLASH, .row = row, .col = col };
            LMBACOMPILER_TOKENIZE_PushToken(tok);
            col++;
            ungetc(next, f);
            c = fgetc(f);
            continue;
        }

        // --- Single-char tokens ---
        {
            TokenType t = TOK_INVALID;
            switch (c) {
                case '(': t = TOK_LPAREN; break;
                case ')': t = TOK_RPAREN; break;
                case '{': t = TOK_LBRACE; break;
                case '}': t = TOK_RBRACE; break;
                case ',': t = TOK_COMMA;  break;
                case ';': t = TOK_SEMI;   break;
                case '+': t = TOK_PLUS;   break;
                case '-': t = TOK_MINUS;  break;
                case '*': t = TOK_STAR;   break;
            }
            if (t != TOK_INVALID) {
                Token tok = { .type = t, .row = row, .col = col };
                LMBACOMPILER_TOKENIZE_PushToken(tok);
                col++;
                c = fgetc(f);
                continue;
            }
        }

        // --- One-or-two char tokens: = == != < <= > >= ---
        if (c == '=' || c == '!' || c == '<' || c == '>') {
            uint64_t tok_col = col;
            int next = fgetc(f);
            TokenType t;
            if      (c == '=' && next == '=') { t = TOK_EQ;  col += 2; }
            else if (c == '!' && next == '=') { t = TOK_NEQ; col += 2; }
            else if (c == '<' && next == '=') { t = TOK_LTE; col += 2; }
            else if (c == '>' && next == '=') { t = TOK_GTE; col += 2; }
            else {
                if      (c == '=') t = TOK_ASSIGN;
                else if (c == '<') t = TOK_LT;
                else               t = TOK_GT;
                col++;
                ungetc(next, f);
            }
            Token tok = { .type = t, .row = row, .col = tok_col };
            LMBACOMPILER_TOKENIZE_PushToken(tok);
            c = fgetc(f);
            continue;
        }

        // --- Number literal ---
        if (isdigit(c)) {
            uint64_t tok_col = col;
            uint64_t value = 0;
            while (isdigit(c)) {
                value = value * 10 + (uint64_t)(c - '0');
                col++;
                c = fgetc(f);
            }
            Token tok = { .type = TOK_NUMBER, .row = row, .col = tok_col };
            tok.data.immediate.n = value;
            LMBACOMPILER_TOKENIZE_PushToken(tok);
            continue;
        }

        // --- Identifier or keyword ---
        if (isalpha(c) || c == '_') {
            uint64_t tok_col = col;
            char buf[256];
            int len = 0;
            while ((isalnum(c) || c == '_') && len < 255) {
                buf[len++] = (char)c;
                col++;
                c = fgetc(f);
            }
            buf[len] = '\0';

            TokenType kw = keyword_lookup(buf);
            Token tok = { .row = row, .col = tok_col };
            if (kw != TOK_INVALID) {
                tok.type = kw;
            } else {
                tok.type = TOK_IDENTIFIER;
                tok.data.identifier.str = strdup(buf);
            }
            LMBACOMPILER_TOKENIZE_PushToken(tok);
            continue;
        }

        // --- Unknown character ---
        {
            Token tok = { .type = TOK_INVALID, .row = row, .col = col };
            LMBACOMPILER_TOKENIZE_PushToken(tok);
            col++;
            c = fgetc(f);
        }
    }

    // EOF sentinel
    Token eof = { .type = TOK_EOF, .row = row, .col = col };
    LMBACOMPILER_TOKENIZE_PushToken(eof);
}







// -----------------------------------------------------------------------
// Parser
// -----------------------------------------------------------------------

// Parser cursor — walks the token array produced by the lexer
static struct {
    size_t pos;
} PARSER;

static Token* p_peek(void) {
    return &LAMBDA_COMPILER.lexer.tokens[PARSER.pos];
}

static Token* p_advance(void) {
    Token* t = &LAMBDA_COMPILER.lexer.tokens[PARSER.pos];
    if (t->type != TOK_EOF) PARSER.pos++;
    return t;
}

// Consume next token only if it matches `type`. Returns it or NULL.
static Token* p_expect(TokenType type) {
    if (p_peek()->type == type) return p_advance();
    return NULL;
}

static bool p_is_type_keyword(TokenType t) {
    return t == TOK_INT8;
}

// Forward declarations
static ASTNode* parse_expr(void);
static ASTNode* parse_stmt(void);
static ASTNode* parse_block(void);

// --- Expressions ---
// Precedence (low to high):
//   assignment  =
//   equality    == !=
//   relational  < <= > >=
//   additive    + -
//   multiplicative * /
//   unary       (future)
//   primary     number, identifier, call, (expr)

static ASTNode* parse_primary(void) {
    Token* t = p_peek();

    // Number literal
    if (t->type == TOK_NUMBER) {
        p_advance();
        ASTNode* n = ast_node_alloc(NODE_NUMBER);
        n->data.number.value = t->data.immediate.n;
        return n;
    }

    // Identifier or function call
    if (t->type == TOK_IDENTIFIER) {
        p_advance();
        // function call: name(args...)
        if (p_peek()->type == TOK_LPAREN) {
            p_advance(); // consume '('
            ASTNode* n = ast_node_alloc(NODE_CALL);
            n->data.call.name = strdup(t->data.identifier.str);
            while (p_peek()->type != TOK_RPAREN && p_peek()->type != TOK_EOF) {
                nodelist_push(&n->data.call.args, parse_expr());
                if (!p_expect(TOK_COMMA)) break;
            }
            p_expect(TOK_RPAREN);
            return n;
        }
        // plain identifier
        ASTNode* n = ast_node_alloc(NODE_IDENTIFIER);
        n->data.identifier.name = strdup(t->data.identifier.str);
        return n;
    }

    // Parenthesised expression
    if (t->type == TOK_LPAREN) {
        p_advance();
        ASTNode* inner = parse_expr();
        p_expect(TOK_RPAREN);
        return inner;
    }

    // Unexpected token — return NULL (caller handles error)
    return NULL;
}

static ASTNode* parse_multiplicative(void) {
    ASTNode* left = parse_primary();
    while (p_peek()->type == TOK_STAR || p_peek()->type == TOK_SLASH) {
        Token* op = p_advance();
        ASTNode* right = parse_primary();
        ASTNode* n = ast_node_alloc(NODE_BINOP);
        n->data.binop.op    = op->type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static ASTNode* parse_additive(void) {
    ASTNode* left = parse_multiplicative();
    while (p_peek()->type == TOK_PLUS || p_peek()->type == TOK_MINUS) {
        Token* op = p_advance();
        ASTNode* right = parse_multiplicative();
        ASTNode* n = ast_node_alloc(NODE_BINOP);
        n->data.binop.op    = op->type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static ASTNode* parse_relational(void) {
    ASTNode* left = parse_additive();
    TokenType t = p_peek()->type;
    while (t == TOK_LT || t == TOK_LTE || t == TOK_GT || t == TOK_GTE) {
        Token* op = p_advance();
        ASTNode* right = parse_additive();
        ASTNode* n = ast_node_alloc(NODE_BINOP);
        n->data.binop.op    = op->type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
        t = p_peek()->type;
    }
    return left;
}

static ASTNode* parse_equality(void) {
    ASTNode* left = parse_relational();
    while (p_peek()->type == TOK_EQ || p_peek()->type == TOK_NEQ) {
        Token* op = p_advance();
        ASTNode* right = parse_relational();
        ASTNode* n = ast_node_alloc(NODE_BINOP);
        n->data.binop.op    = op->type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static ASTNode* parse_assignment(void) {
    ASTNode* left = parse_equality();
    if (p_peek()->type == TOK_ASSIGN) {
        Token* op = p_advance();
        ASTNode* right = parse_assignment(); // right-associative
        ASTNode* n = ast_node_alloc(NODE_BINOP);
        n->data.binop.op    = op->type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        return n;
    }
    return left;
}

static ASTNode* parse_expr(void) {
    return parse_assignment();
}

// --- Statements ---

static ASTNode* parse_block(void) {
    p_expect(TOK_LBRACE);
    ASTNode* n = ast_node_alloc(NODE_BLOCK);
    while (p_peek()->type != TOK_RBRACE && p_peek()->type != TOK_EOF) {
        nodelist_push(&n->data.block.stmts, parse_stmt());
    }
    p_expect(TOK_RBRACE);
    return n;
}

static ASTNode* parse_stmt(void) {
    Token* t = p_peek();

    // Block
    if (t->type == TOK_LBRACE) {
        return parse_block();
    }

    // Variable declaration: int8 name [= expr] ;
    if (p_is_type_keyword(t->type)) {
        p_advance(); // consume type
        Token* name_tok = p_expect(TOK_IDENTIFIER);
        ASTNode* n = ast_node_alloc(NODE_VAR_DECL);
        n->data.var_decl.name = name_tok ? strdup(name_tok->data.identifier.str) : strdup("");
        if (p_expect(TOK_ASSIGN)) {
            n->data.var_decl.init = parse_expr();
        }
        p_expect(TOK_SEMI);
        return n;
    }

    // if ( cond ) block [ else block ]
    if (t->type == TOK_IF) {
        p_advance();
        p_expect(TOK_LPAREN);
        ASTNode* cond = parse_expr();
        p_expect(TOK_RPAREN);
        ASTNode* then_block = parse_block();
        ASTNode* else_block = NULL;
        if (p_peek()->type == TOK_ELSE) {
            p_advance();
            else_block = parse_block();
        }
        ASTNode* n = ast_node_alloc(NODE_IF);
        n->data.if_stmt.cond       = cond;
        n->data.if_stmt.then_block = then_block;
        n->data.if_stmt.else_block = else_block;
        return n;
    }

    // while ( cond ) block
    if (t->type == TOK_WHILE) {
        p_advance();
        p_expect(TOK_LPAREN);
        ASTNode* cond = parse_expr();
        p_expect(TOK_RPAREN);
        ASTNode* body = parse_block();
        ASTNode* n = ast_node_alloc(NODE_WHILE);
        n->data.while_stmt.cond = cond;
        n->data.while_stmt.body = body;
        return n;
    }

    // for ( init ; cond ; post ) block
    if (t->type == TOK_FOR) {
        p_advance();
        p_expect(TOK_LPAREN);
        // init: either a var decl or an expression statement (or empty)
        ASTNode* init = NULL;
        if (p_is_type_keyword(p_peek()->type)) {
            // var decl without trailing ';' handling — parse_stmt consumes it
            init = parse_stmt();
        } else if (p_peek()->type != TOK_SEMI) {
            init = parse_expr();
            p_expect(TOK_SEMI);
        } else {
            p_advance(); // empty init
        }
        ASTNode* cond = NULL;
        if (p_peek()->type != TOK_SEMI) cond = parse_expr();
        p_expect(TOK_SEMI);
        ASTNode* post = NULL;
        if (p_peek()->type != TOK_RPAREN) post = parse_expr();
        p_expect(TOK_RPAREN);
        ASTNode* body = parse_block();
        ASTNode* n = ast_node_alloc(NODE_FOR);
        n->data.for_stmt.init = init;
        n->data.for_stmt.cond = cond;
        n->data.for_stmt.post = post;
        n->data.for_stmt.body = body;
        return n;
    }

    // return [expr] ;
    if (t->type == TOK_RETURN) {
        p_advance();
        ASTNode* n = ast_node_alloc(NODE_RETURN);
        if (p_peek()->type != TOK_SEMI) n->data.expr_stmt.expr = parse_expr();
        p_expect(TOK_SEMI);
        return n;
    }

    // break ;
    if (t->type == TOK_BREAK) {
        p_advance();
        p_expect(TOK_SEMI);
        return ast_node_alloc(NODE_BREAK);
    }

    // continue ;
    if (t->type == TOK_CONTINUE) {
        p_advance();
        p_expect(TOK_SEMI);
        return ast_node_alloc(NODE_CONTINUE);
    }

    // expression statement: expr ;
    ASTNode* n = ast_node_alloc(NODE_EXPR_STMT);
    n->data.expr_stmt.expr = parse_expr();
    p_expect(TOK_SEMI);
    return n;
}

// --- Top-level: function definition ---
// int8 name ( [int8 param, ...] ) block

static ASTNode* parse_func_def(void) {
    p_advance(); // consume return type keyword
    Token* name_tok = p_expect(TOK_IDENTIFIER);
    ASTNode* n = ast_node_alloc(NODE_FUNC_DEF);
    n->data.func_def.name = name_tok ? strdup(name_tok->data.identifier.str) : strdup("");
    p_expect(TOK_LPAREN);
    while (p_peek()->type != TOK_RPAREN && p_peek()->type != TOK_EOF) {
        p_advance(); // consume param type keyword
        Token* pname = p_expect(TOK_IDENTIFIER);
        ASTNode* param = ast_node_alloc(NODE_VAR_DECL);
        param->data.var_decl.name = pname ? strdup(pname->data.identifier.str) : strdup("");
        param->data.var_decl.init = NULL;
        nodelist_push(&n->data.func_def.params, param);
        if (!p_expect(TOK_COMMA)) break;
    }
    p_expect(TOK_RPAREN);
    n->data.func_def.body = parse_block();
    return n;
}

//Parse into AST.
void LMBACOMPILER_PARSE(Lambda_Worker* WRKING_DATA) {
    PARSER.pos = 0;
    while (p_peek()->type != TOK_EOF) {
        if (p_is_type_keyword(p_peek()->type)) {
            nodelist_push(&LAMBDA_COMPILER.parser.top_level, parse_func_def());
        } else {
            // unexpected token at top level — skip it
            p_advance();
        }
    }
}


// -----------------------------------------------------------------------
// Semantic analysis
// -----------------------------------------------------------------------
// Checks:
//   1. Undefined variables (use before declaration)
//   2. Undeclared functions (call to unknown name)
//   3. Number literals that overflow int8 (> 255)

// --- Symbol table (flat scope stack) ---

#define SYMTABLE_MAX 128

typedef struct {
    const char* names[SYMTABLE_MAX];
    size_t      count;
} SymTable;

static void sym_push(SymTable* s, const char* name) {
    if (s->count < SYMTABLE_MAX) s->names[s->count++] = name;
}

static void sym_pop_to(SymTable* s, size_t mark) {
    s->count = mark;
}

static bool sym_has(SymTable* s, const char* name) {
    for (size_t i = 0; i < s->count; i++)
        if (!strcmp(s->names[i], name)) return true;
    return false;
}

// --- Analyzer state ---

static struct {
    Lambda_Worker* worker;
    SymTable       funcs;   // known function names
    SymTable       vars;    // variables in scope
    int            errors;
} ANALYZER;

static void ana_error(const char* msg) {
    printf("ANALYZE ERROR: %s\n", msg);
    ANALYZER.errors++;
}

static void ana_warn(const char* msg) {
    printf("ANALYZE WARN:  %s\n", msg);
}

// Forward declaration
static void ana_node(ASTNode* node);

static void ana_expr(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case NODE_NUMBER:
            if (node->data.number.value > 255) {
                char buf[64];
                snprintf(buf, sizeof(buf), "literal %llu exceeds int8 range (0-255)",
                         (unsigned long long)node->data.number.value);
                ana_warn(buf);
            }
            break;

        case NODE_IDENTIFIER: {
            if (!sym_has(&ANALYZER.vars, node->data.identifier.name)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "undefined variable '%s'", node->data.identifier.name);
                ana_error(buf);
            }
            break;
        }

        case NODE_BINOP:
            ana_expr(node->data.binop.left);
            ana_expr(node->data.binop.right);
            break;

        case NODE_CALL: {
            if (!sym_has(&ANALYZER.funcs, node->data.call.name)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "call to undeclared function '%s'", node->data.call.name);
                ana_error(buf);
            }
            for (size_t i = 0; i < node->data.call.args.len; i++)
                ana_expr(node->data.call.args.nodes[i]);
            break;
        }

        default: break;
    }
}

static void ana_stmt(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case NODE_BLOCK: {
            size_t mark = ANALYZER.vars.count;
            for (size_t i = 0; i < node->data.block.stmts.len; i++)
                ana_node(node->data.block.stmts.nodes[i]);
            sym_pop_to(&ANALYZER.vars, mark); // variables declared in this block go out of scope
            break;
        }

        case NODE_VAR_DECL:
            if (node->data.var_decl.init) ana_expr(node->data.var_decl.init);
            sym_push(&ANALYZER.vars, node->data.var_decl.name);
            break;

        case NODE_EXPR_STMT:
            ana_expr(node->data.expr_stmt.expr);
            break;

        case NODE_RETURN:
            ana_expr(node->data.expr_stmt.expr);
            break;

        case NODE_IF:
            ana_expr(node->data.if_stmt.cond);
            ana_stmt(node->data.if_stmt.then_block);
            ana_stmt(node->data.if_stmt.else_block);
            break;

        case NODE_WHILE:
            ana_expr(node->data.while_stmt.cond);
            ana_stmt(node->data.while_stmt.body);
            break;

        case NODE_FOR: {
            size_t mark = ANALYZER.vars.count;
            ana_node(node->data.for_stmt.init);  // may declare a var
            ana_expr(node->data.for_stmt.cond);
            ana_expr(node->data.for_stmt.post);
            ana_stmt(node->data.for_stmt.body);
            sym_pop_to(&ANALYZER.vars, mark);
            break;
        }

        case NODE_BREAK:
        case NODE_CONTINUE:
            break;

        default: break;
    }
}

static void ana_node(ASTNode* node) {
    if (!node) return;
    // Statements that can appear as for-init
    if (node->type == NODE_VAR_DECL || node->type == NODE_EXPR_STMT)
        ana_stmt(node);
    else
        ana_stmt(node);
}

//Semantic analysis
void LMBACOMPILER_ANALYZE(Lambda_Worker* WRKING_DATA) {
    ANALYZER.worker = WRKING_DATA;
    ANALYZER.funcs.count = 0;
    ANALYZER.vars.count  = 0;
    ANALYZER.errors      = 0;

    NodeList* top = &LAMBDA_COMPILER.parser.top_level;

    // First pass: register all function names so forward calls work
    for (size_t i = 0; i < top->len; i++) {
        ASTNode* fn = top->nodes[i];
        if (fn->type == NODE_FUNC_DEF)
            sym_push(&ANALYZER.funcs, fn->data.func_def.name);
    }

    // Second pass: analyze each function body
    for (size_t i = 0; i < top->len; i++) {
        ASTNode* fn = top->nodes[i];
        if (fn->type != NODE_FUNC_DEF) continue;

        // Parameters are visible inside the function body
        size_t mark = ANALYZER.vars.count;
        for (size_t p = 0; p < fn->data.func_def.params.len; p++)
            sym_push(&ANALYZER.vars, fn->data.func_def.params.nodes[p]->data.var_decl.name);

        ana_stmt(fn->data.func_def.body);

        sym_pop_to(&ANALYZER.vars, mark);
    }

    if (ANALYZER.errors) {
        printf("ANALYZE: %d error(s) found.\n", ANALYZER.errors);
    }
}

// -----------------------------------------------------------------------
// Code generation
// -----------------------------------------------------------------------
// Strategy:
//   - Variables are mapped to R0-R4 (R4 also used as scratch)
//   - R5 = MAR_HI (memory address upper byte), R6 = SP_lo, R7 = SP_hi
//   - The accumulator (A) is the implicit working register for all ops
//   - LOAD only accepts nibbles (0-15); values 16-255 are loaded in two halves
//   - Subtraction, division, comparison, function calls: stubbed with asm comments
//   - JUMP targets: emitted as "JUMP ?? <cond>" placeholders for manual filling

#define CODEGEN_MAX_VARS 5  // R0-R4 (R4 doubles as scratch)

static struct {
    Lambda_Worker* worker;
    const char*    var_names[CODEGEN_MAX_VARS];
    int            var_count;
    int            errors;
} CODEGEN;

static void cgen_emit(const char* asm_line) {
    LambdaProgram_APPEND_FROMASM(CODEGEN.worker, asm_line);
}

// Emit a comment into stdout (assembly output doesn't support comments via APPEND_FROMASM,
// so we just print them so they're visible during development)
static void cgen_note(const char* note) {
    fprintf(CODEGEN.worker->output_file, "// %s\n", note);
}

// Returns register index (0-6) for a variable name, or -1
static int cgen_var_reg(const char* name) {
    for (int i = 0; i < CODEGEN.var_count; i++)
        if (!strcmp(CODEGEN.var_names[i], name)) return i;
    return -1;
}

// Allocate the next free register for a new variable. Returns reg index or -1.
static int cgen_var_alloc(const char* name) {
    if (CODEGEN.var_count >= CODEGEN_MAX_VARS) {
        printf("CODEGEN ERROR: too many variables (max %d)\n", CODEGEN_MAX_VARS);
        CODEGEN.errors++;
        return -1;
    }
    CODEGEN.var_names[CODEGEN.var_count] = name;
    return CODEGEN.var_count++;
}

// Load an 8-bit immediate into the accumulator.
// If value <= 15: LOAD value
// If value <= 255: LOAD hi; (shift left 4 via ADD+LSR tricks) — stubbed, emits a note
static void cgen_load_imm(uint64_t value) {
    char buf[32];
    if (value <= 15) {
        snprintf(buf, sizeof(buf), "LOAD %llu", (unsigned long long)value);
        cgen_emit(buf);
    } else {
        // Loading values > 15 requires two nibbles. Stubbed for now.
        cgen_note("TODO: load 8-bit immediate (>15) — needs nibble shift sequence");
        // Emit the low nibble as a placeholder so something is there
        snprintf(buf, sizeof(buf), "LOAD %llu", (unsigned long long)(value & 0xF));
        cgen_emit(buf);
    }
}

// Evaluate an expression, leaving the result in the accumulator.
static void cgen_expr(ASTNode* node);

static void cgen_expr(ASTNode* node) {
    if (!node) return;
    char buf[64];

    switch (node->type) {

        case NODE_NUMBER:
            cgen_load_imm(node->data.number.value);
            break;

        case NODE_IDENTIFIER: {
            int reg = cgen_var_reg(node->data.identifier.name);
            if (reg < 0) { cgen_note("ERROR: unknown variable"); return; }
            // ADD Rx adds Rx into accumulator. To move Rx -> A cleanly:
            // LOAD 0 ; ADD Rx
            cgen_emit("LOAD 0");
            snprintf(buf, sizeof(buf), "ADD R%d", reg);
            cgen_emit(buf);
            break;
        }

        case NODE_BINOP: {
            TokenType op = node->data.binop.op;

            if (op == TOK_ASSIGN) {
                // Evaluate RHS into accumulator, then store into LHS register
                cgen_expr(node->data.binop.right);
                if (node->data.binop.left && node->data.binop.left->type == NODE_IDENTIFIER) {
                    int reg = cgen_var_reg(node->data.binop.left->data.identifier.name);
                    if (reg >= 0) {
                        snprintf(buf, sizeof(buf), "STORE R%d", reg);
                        cgen_emit(buf);
                    }
                }
                break;
            }

            if (op == TOK_PLUS) {
                // left + right: eval left -> store scratch (R4), eval right, ADD R4
                cgen_expr(node->data.binop.left);
                cgen_emit("STORE R4");  // scratch
                cgen_expr(node->data.binop.right);
                cgen_emit("ADD R4");
                break;
            }

            // All other ops stubbed
            cgen_note("TODO: unsupported binary op — only + and = implemented");
            cgen_expr(node->data.binop.left);
            break;
        }

        case NODE_CALL:
            cgen_note("TODO: function call not yet implemented");
            break;

        default:
            cgen_note("TODO: unhandled expression type");
            break;
    }
}

static void cgen_stmt(ASTNode* node);

static void cgen_stmt(ASTNode* node) {
    if (!node) return;
    char buf[64];

    switch (node->type) {

        case NODE_BLOCK:
            for (size_t i = 0; i < node->data.block.stmts.len; i++)
                cgen_stmt(node->data.block.stmts.nodes[i]);
            break;

        case NODE_VAR_DECL: {
            int reg = cgen_var_alloc(node->data.var_decl.name);
            if (reg < 0) break;
            if (node->data.var_decl.init) {
                cgen_expr(node->data.var_decl.init);
                snprintf(buf, sizeof(buf), "STORE R%d", reg);
                cgen_emit(buf);
            } else {
                // Zero-initialise
                cgen_emit("LOAD 0");
                snprintf(buf, sizeof(buf), "STORE R%d", reg);
                cgen_emit(buf);
            }
            break;
        }

        case NODE_EXPR_STMT:
            cgen_expr(node->data.expr_stmt.expr);
            break;

        case NODE_RETURN:
            if (node->data.expr_stmt.expr) cgen_expr(node->data.expr_stmt.expr);
            cgen_note("return — result in accumulator");
            break;

        case NODE_IF:
            // Evaluate condition
            cgen_expr(node->data.if_stmt.cond);
            // Emit a JUMP placeholder — user fills in target and condition flag
            cgen_note("JUMP placeholder: branch if condition false to skip then-block");
            fprintf(CODEGEN.worker->output_file, "// JUMP ?? Z\n");
            cgen_stmt(node->data.if_stmt.then_block);
            if (node->data.if_stmt.else_block) {
                cgen_note("JUMP placeholder: skip else-block");
                fprintf(CODEGEN.worker->output_file, "// JUMP ?? N\n");
                cgen_stmt(node->data.if_stmt.else_block);
            }
            break;

        case NODE_WHILE:
            cgen_note("while loop start");
            cgen_expr(node->data.while_stmt.cond);
            cgen_note("JUMP placeholder: exit loop if condition false");
            fprintf(CODEGEN.worker->output_file, "// JUMP ?? Z\n");
            cgen_stmt(node->data.while_stmt.body);
            cgen_note("JUMP placeholder: back to loop start");
            fprintf(CODEGEN.worker->output_file, "// JUMP ?? N\n");
            break;

        case NODE_FOR:
            cgen_note("for loop");
            if (node->data.for_stmt.init) cgen_stmt(node->data.for_stmt.init);
            cgen_expr(node->data.for_stmt.cond);
            cgen_note("JUMP placeholder: exit loop if condition false");
            fprintf(CODEGEN.worker->output_file, "// JUMP ?? Z\n");
            cgen_stmt(node->data.for_stmt.body);
            if (node->data.for_stmt.post) cgen_expr(node->data.for_stmt.post);
            cgen_note("JUMP placeholder: back to loop start");
            fprintf(CODEGEN.worker->output_file, "// JUMP ?? N\n");
            break;

        case NODE_BREAK:
        case NODE_CONTINUE:
            cgen_note("TODO: break/continue — fill in JUMP target manually");
            break;

        default: break;
    }
}

//make bin code
void LMBACOMPILER_GENERATE(Lambda_Worker* WRKING_DATA) {
    CODEGEN.worker    = WRKING_DATA;
    CODEGEN.var_count = 0;
    CODEGEN.errors    = 0;

    NodeList* top = &LAMBDA_COMPILER.parser.top_level;
    if (top->len == 0) {
        printf("CODEGEN: no functions to generate.\n");
        return;
    }

    // Generate code for the first function only
    ASTNode* fn = top->nodes[0];
    if (fn->type != NODE_FUNC_DEF) return;

    fprintf(WRKING_DATA->output_file, "// --- function: %s ---\n", fn->data.func_def.name);

    // Assign registers to parameters
    for (size_t i = 0; i < fn->data.func_def.params.len; i++)
        cgen_var_alloc(fn->data.func_def.params.nodes[i]->data.var_decl.name);

    cgen_stmt(fn->data.func_def.body);
}


void LMBACOMPILER_INIT(size_t lexer_init) {
    LAMBDA_COMPILER.lexer.token_cap = lexer_init;
    LAMBDA_COMPILER.lexer.token_len = 0;
    LAMBDA_COMPILER.lexer.tokens = malloc(sizeof(Token) * LAMBDA_COMPILER.lexer.token_cap);
}

void LMBACOMPILER_FREE() {
    // Free strdup'd identifier strings in tokens
    for (size_t i = 0; i < LAMBDA_COMPILER.lexer.token_len; i++) {
        if (LAMBDA_COMPILER.lexer.tokens[i].type == TOK_IDENTIFIER) {
            free(LAMBDA_COMPILER.lexer.tokens[i].data.identifier.str);
        }
    }
    free(LAMBDA_COMPILER.lexer.tokens);
    LAMBDA_COMPILER.lexer.token_cap = 0;
    LAMBDA_COMPILER.lexer.token_len = 0;

    // Free AST
    nodelist_free(&LAMBDA_COMPILER.parser.top_level);
}
