#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum LambdaOpCode {
    OP_NAND = 0,
    OP_ADD = 1,
    OP_LSR = 2,
    OP_LOAD = 3,
    OP_STORE = 4,
    OP_MEMREAD = 5,
    OP_MEMWRITE = 6,
    OP_JUMP = 7,
    OP_COUNT
} LambdaOpCode;

static const char opcode_alias[OP_COUNT][8] = {
    [OP_NAND]       = "NAND",
    [OP_ADD]        = "ADD",
    [OP_LSR]        = "LSR",
    [OP_LOAD]       = "LOAD",
    [OP_STORE]      = "STORE",
    [OP_MEMREAD]    = "READ",
    [OP_MEMWRITE]   = "WRITE",
    [OP_JUMP]       = "JUMP"
};

typedef enum Lamdba_CompilationMode{
    CMP_SB,
    CMP_SA,
    CMP_AB
} Lamdba_CompilationMode;

typedef struct LambdaInstr {
    LambdaOpCode opcode;
    uint8_t arg;
    uint8_t arg2;
} LambdaInstr;

typedef struct LambdaProgram {
    LambdaInstr* instructions;
    size_t instr_len;
    size_t instr_capacity;
} LambdaProgram;



typedef struct Lambda_Worker {
    const char* input_arg;
    const char* output_arg;
    const char* mode_arg;

    FILE* input_file;
    FILE* output_file;
    Lamdba_CompilationMode compilation_mode;

    LambdaProgram program;

    char* error_out;
} Lambda_Worker;

#define ERROR_OUT_CAP 1024