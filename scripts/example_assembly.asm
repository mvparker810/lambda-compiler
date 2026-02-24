//Program initialization

//zero all regs
LOAD 0
STORE R0
STORE R1
STORE R2
STORE R3
STORE R4
STORE R5
STORE R6
STORE R7

//zero the stack pointerr

// Memory is done weird
// R7 is the special "Memory Address Register Upper Byte". TODO shorten this name
// WRITE R0 <=> MEMORY[R7::R0] = ~A
// READ  R0 <=> ~A = MEMORY[R7::R0]
LOAD 0
STORE R7

STORE R0
WRITE R0
//MEM[0] = 0

LOAD 1
STORE R0
LOAD 0
WRITE R0

//MEM[1] = 0

