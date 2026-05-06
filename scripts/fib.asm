// R1 = fib(n)
//   R0 = prev
//   R1 = curr
//   R2 = counter (N-1)
//   R3 = next
//   R4 = loop top address
//   R5 = 0xFF (-1 for decrement via ADDing)
//   R6 = exit address
//   R7 = 0 (upper byte for jumping)

//effective range N = 13

// R5 = 0xFF
LOAD 0   
STORE R5 
NAND R5    
STORE R5 

// set upper jump byte. dont touch r7
LOAD 0          
STORE R7     

// 0 (prev) ---
LOAD 0         
STORE R0        

// 1 (curr) ---
LOAD 1       
STORE R1       

// R2 = (N-1)  (todo macros))
LOAD 12         
STORE R2    

// R4 = 38 = 0x26 (top addr) (todo pls do labels)
LOAD 2          
STORE R4        
ADD R4          
STORE R4        
ADD R4          
STORE R4       
ADD R4         
STORE R4        
ADD R4          
STORE R3       
LOAD 6          
ADD R3          
STORE R4        

// R6 = 54 = 0x36 (exit addr)
LOAD 3          
STORE R6        
ADD R6          
STORE R6        
ADD R6          
STORE R6        
ADD R6          
STORE R6        
ADD R6          
STORE R3        
LOAD 6          
ADD R3          
STORE R6        

// next = prev + curr
LOAD 0          
ADD R0          
ADD R1          // ACC = prev + curr
STORE R3        // R3 = next

// prev = curr
LOAD 0          //
ADD R1          //
STORE R0        // R0 = curr

// curr = next
LOAD 0          
ADD R3          
STORE R1        // R1 = next

// counter
LOAD 0         
ADD R2          
ADD R5          // ACC = R2 - 1,    C always set
STORE R2        // R2  = R2 - 1,     Z=1 if now zero

JUMP R6 Z // exit if c = 0
JUMP R4 C //do loop again

// R1 = fib(10) = 55