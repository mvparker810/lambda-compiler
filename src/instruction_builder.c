#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "isa.h"
#include "main.h"
#include "instruction_builder.h"

static int split_ws_inplace(char *s, char *argv[], int cap) {
    int argc = 0;
    char *p = s;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;

        if (argc >= cap) return -1;
        argv[argc++] = p;

        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) { *p = '\0'; p++; }
    }

    return argc;
}


uint8_t LambdaInstr_ToByte(LambdaInstr instr) {
    uint8_t byte = (instr.opcode << 5);
    
    switch (instr.opcode) {
        case OP_JUMP: {
            byte |= (instr.arg2 & 0x03) << 3;
        }
        case OP_ADD:
        case OP_NAND:
        case OP_STORE:
        case OP_MEMREAD:
        case OP_MEMWRITE: 
        case OP_LOAD: 
            byte |= (instr.arg & 0x07);
        break;

        case OP_LSR:
        default:
        break;
    } 

    return byte;
}


int LambdaInstr_Deserialize(Lambda_Worker* WRKING_DATA, const char* instruction, LambdaInstr* out) {
    if (!instruction) return ERRORCODE_FAILURE;
    size_t len = strlen(instruction);
    if (!len) return ERRORCODE_EMPTYLINE;

    while (*instruction && isspace((unsigned char)*instruction)) instruction++;
    if (*instruction == '\0') return ERRORCODE_EMPTYLINE;
    if (*instruction == '#' || *instruction == '/') return ERRORCODE_EMPTYLINE; //allow comments
    printf("Pasring instruction: \"%s\"\n", instruction);

    char buf[32];
    
    if (len >= sizeof(buf)) {
        snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP, "Instruction too long: %zu characters (max %zu)", len, sizeof(buf) - 1);
        return 1;
    }
    memcpy(buf, instruction, len + 1);
    buf[len] = '\0';

    char *argv[4];
    char* s = buf; for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
   
    int argc = split_ws_inplace(buf, argv, 4);
    if (argc < 0) {
        snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP, "Too many tokens | \"%s\"", buf);
        return ERRORCODE_FAILURE;
    }
    if (argc == 0) return ERRORCODE_EMPTYLINE;

    const char *op = argv[0];

    out->opcode = OP_COUNT;
    for (uint8_t i = 0; i < OP_COUNT; i++) {
        if (strcmp(op, opcode_alias[i]) == 0) {
            out->opcode = (LambdaOpCode)i;
            break;
        }
    }
    if (out->opcode == OP_COUNT) {
        snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP, "Unknown opcode '%s' | \"%s\"", op, buf);
        return ERRORCODE_FAILURE;
    }


    if (out->opcode == OP_LSR) { //shifing right is the only instruction that takes no arguments
        if (argc != 1) {
            snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                    "Instruction '%s' takes no arguments | \"%s\"", op, buf);
            return ERRORCODE_FAILURE;
        }
    } else if (out->opcode == OP_JUMP) { //jump is the only instruction that can take either a register or an immediate, but not both
        if (argc != 3) {
            snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                    "Instruction '%s' requires exactly three arguments | \"%s\"", op, buf);
            return ERRORCODE_FAILURE;
        }
    } else {
        if (argc != 2) {
            snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                    "Instruction '%s' requires exactly one argument | \"%s\"", op, buf);
            return ERRORCODE_FAILURE;
        }
    }



    switch (out->opcode) {
        case OP_JUMP: /*fall through to the first case*/ {
            if (strlen(argv[2]) != 1) {
                snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                        "Second instruction '%s' invalid | \"%s\"", op, buf);
                return ERRORCODE_FAILURE;
            }

            char c = tolower(argv[2][0]);

            switch (c) { //todo reoder these prob
                case 'n': out->arg2 = 0; break;
                case 'z': out->arg2 = 1; break;
                case 'c': out->arg2 = 2; break;
                case 'v': out->arg2 = 3; break;
                default:
                    snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                            "Invalid jump condition '%s' | \"%s\"", argv[2], buf);
                    return ERRORCODE_FAILURE;
            }
        }
        case OP_ADD:
        case OP_NAND:
        case OP_STORE:
        case OP_MEMREAD:
        case OP_MEMWRITE: 
        {
            if (argv[1][0] != 'R' && argv[1][0] != 'r') {
                snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                        "Argument for instruction '%s' must be a register (e.g. R0) | \"%s\"", op, buf);
                return ERRORCODE_FAILURE;
            }
            char *reg_num_str = argv[1] + 1;
            char *endptr;
            long reg_num = strtol(reg_num_str, &endptr, 10);
            if (*endptr != '\0' || reg_num < 0 || reg_num > MAX_REGS) {
                snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                        "Invalid register number '%s' for instruction '%s' | \"%s\"", reg_num_str, op, buf);
                return ERRORCODE_FAILURE;
            }
            out->arg = (uint8_t)reg_num;
            break;
        }
        case OP_LOAD: {
            char *endptr;
            long imm_value = strtol(argv[1], &endptr, 10);
            if (*endptr != '\0' || imm_value < 0 || imm_value > 0xF) { //0xF because we only can load nibbles due to the limited architecture
                snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                        "Invalid immediate value '%s' for instruction '%s' | \"%s\"", argv[1], op, buf);
                return ERRORCODE_FAILURE;
            }
            out->arg = (uint8_t)imm_value;
            break;
        }
        case OP_LSR:
            out->arg = 0;
            break;
        
        default:
            snprintf(WRKING_DATA->error_out, ERROR_OUT_CAP,
                    "Unhandled opcode '%s' in deserialization | \"%s\"", op, buf);
            return ERRORCODE_FAILURE;
    } 
    return 0;
}



















LambdaProgram LambdaProgram_INIT(size_t init_cap) {
    LambdaProgram program;
    program.instr_len = 0;
    program.instr_capacity = init_cap;
    program.instructions = malloc(sizeof(LambdaInstr) * init_cap);
    return program;
}

int LambdaProgram_FREE(LambdaProgram* program) {
    free(program->instructions);
    program->instructions = NULL;
    program->instr_len = 0;
    program->instr_capacity = 0;
    return 0;
}



int LambdaProgram_APPEND(Lambda_Worker* WRKING_DATA, LambdaInstr instr) {
    LambdaProgram* program = &WRKING_DATA->program;
    if (program->instr_len >= program->instr_capacity) {
        program->instr_capacity *= 2;
        program->instructions = realloc(program->instructions, sizeof(LambdaInstr) * program->instr_capacity);
    }
    program->instructions[program->instr_len++] = instr;
    return 0;
}

int LambdaProgram_APPEND_FROMASM(Lambda_Worker* WRKING_DATA, const char* instr) {



    LambdaInstr parsed_instr;
    int r = LambdaInstr_Deserialize(WRKING_DATA, instr, &parsed_instr);
    if (r) return r;

    return LambdaProgram_APPEND(WRKING_DATA, parsed_instr);
}


int LambdaProgram_WRITE(Lambda_Worker* WRKING_DATA) {
    LambdaProgram* program = &WRKING_DATA->program;
    FILE* file = WRKING_DATA->output_file;
    bool assembly = WRKING_DATA->compilation_mode == CMP_SA;

    printf("TODO: write file using \'LambdaInstr_Serialize\' ");

    char buf[32];
     
    for (size_t i = 0; i < program->instr_len; i++) {
        /*
        LambdaInstr_Serialize(program->instructions[i], assembly, buf, sizeof(buf));

        fprintf(file, "%s", buf);

        LambdaInstr instr = program->instructions[i];
        if (assembly) {
            //LSR is the only instruction that does not take an arg
            //LOAD is the only instruction that takes an immediate
            //All others take register args 
                 if (instr.opcode == OP_LSR)    fprintf(file, "%s\n",       opcode_alias[instr.opcode]);
            else if (instr.opcode == OP_LOAD)   fprintf(file, "%s %d\n",    opcode_alias[instr.opcode], instr.arg);
            else                                fprintf(file, "%s R%d\n",   opcode_alias[instr.opcode], instr.arg);
        } else {
            uint8_t byte = (instr.opcode << 4) | (instr.arg & 0x0F);
            fputc(byte, file);
        }
        */
    }

    return 0;
}

int LambdaProgram_PRINTBINARY(LambdaProgram* program) {
    printf("\n");
    for (size_t i = 0; i < program->instr_len; i++) {
        uint8_t mc_instr = LambdaInstr_ToByte(program->instructions[i]);
        printf("00%02X\n", mc_instr);
    }
    printf("\n");
    return 0;
}