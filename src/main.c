/*
gcc main.c -o ../build/lmda.exe
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "main.h"

#include "printing.h"

#include "isa.h"
#include "instruction_builder.h"
#include "lambdacompiler.h"




#define LMDA_FLAG_FULL   "--sb"
#define LMDA_FLAG_ASM    "--sa"
#define LMDA_FLAG_BIN    "--ab"












int parse_args(int argc, char *argv[], Lambda_Worker* meta) {


    if (argc != 4) {
        printf("Usage: %s <input> <output> <mode>\n\n", argv[0]);
        puts("<input>: Path to the input file");
        puts("<output>: Path to the output file");
        puts("<mode>: Compilation mode");
        puts("\t* " LMDA_FLAG_FULL " : Source -> Binary");
        puts("\t* " LMDA_FLAG_ASM  " : Source -> Assembly");
        puts("\t* " LMDA_FLAG_BIN  " : Assembly -> Binary");
        return 1;
    }

    meta->input_arg = argv[1];
    meta->output_arg = argv[2];
    meta->mode_arg = argv[3];

 



    /*  */ if (!strcmp(meta->mode_arg, LMDA_FLAG_FULL)) {
        meta->compilation_mode = CMP_SB;
    } else if (!strcmp(meta->mode_arg, LMDA_FLAG_ASM)) {
        meta->compilation_mode = CMP_SA;
    } else if (!strcmp(meta->mode_arg, LMDA_FLAG_BIN)) {
        meta->compilation_mode = CMP_AB;
    } else {
        printf("Fatal: Mode \'%s\' not recognized.\n", meta->mode_arg);
        return 1;
    }

    
    meta->input_file = fopen(meta->input_arg, "r");
    if (!meta->input_file) {
        printf("Fatal: Failed to find input file \'%s\'\n", meta->input_arg);
        return 1;
    }

    meta->output_file = fopen(meta->output_arg, meta->compilation_mode == CMP_SA ? "w" : "wb"); //if we are going form source to asm, open in asm mode
    if (!meta->output_file) {
        printf("Fatal: Failed to find output file \'%s\'\n", meta->output_arg);
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    clock_t START_TIME = clock();
    
    int exit = 0;
    Lambda_Worker WRKING_DATA;
    WRKING_DATA.error_out = (char*)malloc(sizeof(char) * ERROR_OUT_CAP);

    if (parse_args(argc, argv, &WRKING_DATA)) {
        exit = 1; goto exit;
    } 
    
    printing_logtime(START_TIME); 
    printf("Compiling \'%s\' to \'%s\'\n", WRKING_DATA.input_arg, WRKING_DATA.output_arg);

    WRKING_DATA.program = LambdaProgram_INIT(1024);

    switch (WRKING_DATA.compilation_mode) {
        case CMP_AB: {
            char instr[5092]; int line = 0;
            while (fgets(instr, sizeof(instr), WRKING_DATA.input_file)) {
                int r = LambdaProgram_APPEND_FROMASM(&WRKING_DATA, instr);
                if (r && r != ERRORCODE_EMPTYLINE) {
                    printing_logtime(START_TIME);
                    printf("ERROR [LINE %d] : %s\n", line, WRKING_DATA.error_out);
                    exit = 1; goto exit;
                }

                line++;
            }
        } break;
        case CMP_SA:
            LMBACOMPILER_INIT(512);



            LMBACOMPILER_TOKENIZE(&WRKING_DATA);
            LMBACOMPILER_PARSE(&WRKING_DATA);
            LMBACOMPILER_ANALYZE(&WRKING_DATA);
            LMBACOMPILER_GENERATE(&WRKING_DATA);

            LMBACOMPILER_FREE();
        break;
    }

    
 








    printing_logtime(START_TIME); printf("Writing instructions to file...\n");
    if (LambdaProgram_WRITE(&WRKING_DATA)) {
        exit = 1; goto exit;
    }
    printing_logtime(START_TIME); printf("Wrote %zu instrsuctions. Exiting\n", WRKING_DATA.program.instr_len);

    LambdaProgram_PRINTBINARY(&WRKING_DATA.program);

    exit:
    
    if (WRKING_DATA.input_file) fclose(WRKING_DATA.input_file);
    if (WRKING_DATA.output_file) fclose(WRKING_DATA.output_file);

    if (WRKING_DATA.error_out) free(WRKING_DATA.error_out);

    LambdaProgram_FREE(&WRKING_DATA.program);

    printing_logtime(START_TIME); printf("Exiting with code %d\n", exit);
    return exit;
}