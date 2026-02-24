//This is how to shift left without a left shift instruction


// Consider the number 165
// Its hexadecimal representation is 0xA5

// First, take the upper nibble 'A', and load it into the accumulator
LOAD  10

// Then, continouously add to that value to shift it into the upper nibble
// Shifing left by four, multiplying by 16, whatever
STORE R0
ADD R0
STORE R0
ADD R0
STORE R0
ADD R0
STORE R0
ADD R0

//Then perserve the final result in R1
STORE R1

//Load the lower nibble '5' into the accumulator, then add R1 back to it
//Effectively OR-ing the two nibbles together.
LOAD 5
ADD R1

//Now, the number 165, or 0xA5, is in the accumulator.