/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef VM_H
#define VM_H

#include <stdint.h>

typedef enum {
    OP_PUSH,  // Push immediate
    OP_POP,   // Pop to register/temp
    OP_LOAD,  // Load from stack offset
    OP_STORE, // Store to stack offset
    OP_ADD,   // Add top two
    OP_SUB,   // Sub top two
    OP_MUL,   // Mul top two
    OP_DIV,   // Div top two
    OP_EQ,    // Equal
    OP_NE,    // Not Equal
    OP_LT,    // Less Than
    OP_GT,    // Greater Than
    OP_PRINT_STR, // Print string (address in arg)
    OP_PRINT_VAR_STR, // Print string (address in top of stack)
    OP_PRINT_INT, // Print int (value in top of stack)
    OP_PRINT_DOUBLE, // Print double (value in top of stack)
    OP_INPUT_INT,    // Input int (address in top of stack)
    OP_INPUT_DOUBLE, // Input double (address in top of stack)
    OP_INPUT_STR,    // Input string (address in top of stack)
    OP_DADD,  // Double Add
    OP_DSUB,  // Double Sub
    OP_DMUL,  // Double Mul
    OP_DDIV,  // Double Div
    OP_PRINT_NL,  // Print newline
    OP_PLACEHOLDER, // Removed PUTC
    OP_JMP,   // Jump to address
    OP_JZ,    // Jump if top of stack is zero
    OP_CALL,  // Call function
    OP_RET,   // Return
    OP_EXIT,  // Exit task
    OP_HALT,  // Stop
    OP_LOAD_ARRAY, // Load vars[base + index]; base in arg, index on stack
    OP_STORE_ARRAY // Store vars[base + index] = value; index then value on stack
} opcode_t;

#define VM_STACK_SIZE 1024
#define VM_MEM_SIZE 8192

#endif
