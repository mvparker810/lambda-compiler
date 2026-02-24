#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


#include "isa.h"
#include "main.h"
//typedef uint8_t MachineCodeInstr;



LambdaProgram LambdaProgram_INIT(size_t init_cap);
int LambdaProgram_FREE(LambdaProgram* program);
int LambdaProgram_APPEND(Lambda_Worker* WRKING_DATA, LambdaInstr instr);
int LambdaProgram_APPEND_FROMASM(Lambda_Worker* WRKING_DATA, const char* instr);
int LambdaProgram_WRITE(Lambda_Worker* WRKING_DATA);
int LambdaProgram_PRINTBINARY(LambdaProgram* program);


#define ERRORCODE_SUCCESS 0
#define ERRORCODE_FAILURE 1
#define ERRORCODE_EMPTYLINE 2