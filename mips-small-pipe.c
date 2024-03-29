#include "mips-small-pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/************************************************************/
int main(int argc, char *argv[]) {
  short i;
  char line[MAXLINELENGTH];
  state_t state;
  FILE *filePtr;

  if (argc != 2) {
    printf("error: usage: %s <machine-code file>\n", argv[0]);
    return 1;
  }

  memset(&state, 0, sizeof(state_t));

  state.pc = state.cycles = 0;
  state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr =
      state.WBEND.instr = NOPINSTRUCTION; /* nop */

  /* read machine-code file into instruction/data memory (starting at address 0)
   */

  filePtr = fopen(argv[1], "r");
  if (filePtr == NULL) {
    printf("error: can't open file %s\n", argv[1]);
    perror("fopen");
    exit(1);
  }

  for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
       state.numMemory++) {
    if (sscanf(line, "%x", &state.dataMem[state.numMemory]) != 1) {
      printf("error in reading address %d\n", state.numMemory);
      exit(1);
    }
    state.instrMem[state.numMemory] = state.dataMem[state.numMemory];
    printf("memory[%d]=%x\n", state.numMemory, state.dataMem[state.numMemory]);
  }

  printf("%d memory words\n", state.numMemory);

  printf("\tinstruction memory:\n");
  for (i = 0; i < state.numMemory; i++) {
    printf("\t\tinstrMem[ %d ] = ", i);
    printInstruction(state.instrMem[i]);
  }

  run(&state);

  return 0;
}
/************************************************************/

/************************************************************/
void run(Pstate state) {
  state_t new;
  memset(&new, 0, sizeof(state_t));

  while (1) {
    printState(state);

    /* copy everything so all we have to do is make changes.
       (this is primarily for the memory and reg arrays) */
    memcpy(&new, state, sizeof(state_t));

    new.cycles++;
    new.pc += 4;  
    /* --------------------- IF stage --------------------- */
    /*fetch instruction*/
    new.IFID.instr = state->instrMem[state->pc/4]; 
    new.IFID.pcPlus1 = new.pc;

    /*predict not taken*/
    if(opcode(new.IFID.instr) == 0x4 && offset(new.IFID.instr) < 0) {
      new.pc += offset(new.IFID.instr); 
    }
    
    /* --------------------- ID stage --------------------- */
    /*get the registers for instruction*/
    new.IDEX.readRegA = state->reg[field_r1(state->IFID.instr)];
    new.IDEX.readRegB = state->reg[field_r2(state->IFID.instr)];
    /*if its a load, store, or branch instruction get the offset*/
    if(opcode(state->IFID.instr) == 0x23 || opcode(state->IFID.instr) == 0x2B || 
    opcode(state->IFID.instr) == 0x4) {
      new.IDEX.offset = offset(state->IFID.instr);
    }
    /*if its an addi instruction get its offset*/
    else if(opcode(state->IFID.instr) == 0x8) {
      new.IDEX.offset = convertNum(field_imm(state->IFID.instr)); 
    }
    /*otherwise get the immediate*/
    else {
      new.IDEX.offset = field_imm(state->IFID.instr);
    }
    
    /*checks for the special case of a data hazard*/
    if(opcode(state->IDEX.instr) == 0x23) {
      if(opcode(state->IFID.instr) == 0x0) {
        if(field_r2(state->IDEX.instr) == field_r1(state->IFID.instr) ||
            field_r2(state->IDEX.instr) == field_r2(state->IFID.instr)) {
            new.IFID.instr = state->IFID.instr; 
            new.IFID.pcPlus1 = state->IFID.pcPlus1; 
            new.IDEX.readRegA = 0; 
            new.IDEX.readRegB = 0; 
            new.IDEX.pcPlus1 = 0; 
            new.IDEX.offset = 32; 
            new.IDEX.instr = 0x00000020; 
            new.pc -= 4; 
        }
        else {
            new.IDEX.pcPlus1 = state->IFID.pcPlus1; 
            new.IDEX.instr = state->IFID.instr;
        }
      }
      else if(opcode(state->IFID.instr) == 0x23 || opcode(state->IFID.instr) == 0x4 
      || opcode(state->IFID.instr) == 0x8) {
        if(field_r2(state->IDEX.instr) == field_r1(state->IFID.instr)) {
            new.IFID.instr = state->IFID.instr; 
            new.IFID.pcPlus1 = state->IFID.pcPlus1; 
            new.IDEX.readRegA = 0; 
            new.IDEX.readRegB = 0; 
            new.IDEX.pcPlus1 = 0; 
            new.IDEX.offset = 32; 
            new.IDEX.instr = 0x00000020; 
            new.pc -= 4;
        }
        else {
            new.IDEX.pcPlus1 = state->IFID.pcPlus1; 
            new.IDEX.instr = state->IFID.instr;
        }
      }
      else {
        new.IDEX.pcPlus1 = state->IFID.pcPlus1; 
        new.IDEX.instr = state->IFID.instr;
      }
    }
    else {
      new.IDEX.pcPlus1 = state->IFID.pcPlus1; 
      new.IDEX.instr = state->IFID.instr;
    }

    
    /* --------------------- EX stage --------------------- */
    /*check if forwarding is required*/
    if(opcode(state->WBEND.instr) == 0x0) {
      if(field_r1(state->IDEX.instr) != 0 && 
      field_r1(state->IDEX.instr) == field_r3(state->WBEND.instr)) {
        state->IDEX.readRegA = state->WBEND.writeData; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      field_r2(state->IDEX.instr) == field_r3(state->WBEND.instr)) {
        state->IDEX.readRegB = state->WBEND.writeData; 
      }
    }
    else if(opcode(state->WBEND.instr) == 0x23 || 
    opcode(state->WBEND.instr) == 0x8 || opcode(state->WBEND.instr) == 0x4) {
      if(field_r1(state->IDEX.instr) != 0 && 
      field_r1(state->IDEX.instr) == field_r2(state->WBEND.instr)) {
        state->IDEX.readRegA = state->WBEND.writeData; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      opcode(state->IDEX.instr) != opcode(state->WBEND.instr) 
      && field_r2(state->IDEX.instr) == field_r2(state->WBEND.instr)) {
        state->IDEX.readRegB = state->WBEND.writeData; 
      }
    }
    else if(opcode(state->WBEND.instr) == 0x2B) {
      if(field_r2(state->IDEX.instr) != 0 && 
      field_r2(state->IDEX.instr) == field_r2(state->WBEND.instr)) {
        state->IDEX.readRegB = state->WBEND.writeData; 
      }
    }

    if(opcode(state->MEMWB.instr) == 0x0) {
      if(field_r1(state->IDEX.instr) != 0 && 
      field_r1(state->IDEX.instr) == field_r3(state->MEMWB.instr)) {
        state->IDEX.readRegA = state->MEMWB.writeData; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      field_r2(state->IDEX.instr) == field_r3(state->MEMWB.instr) 
      && opcode(state->IDEX.instr) != 0x8) {
        state->IDEX.readRegB = state->MEMWB.writeData; 
      }
    }
    else if(opcode(state->MEMWB.instr) == 0x23 || 
    opcode(state->MEMWB.instr) == 0x8 || opcode(state->MEMWB.instr) == 0x4) {
      if(field_r1(state->IDEX.instr) != 0 && 
      field_r1(state->IDEX.instr) == field_r2(state->MEMWB.instr)) {
        state->IDEX.readRegA = state->MEMWB.writeData; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      opcode(state->IDEX.instr) != opcode(state->MEMWB.instr) 
      && field_r2(state->IDEX.instr) == field_r2(state->MEMWB.instr)) {
        state->IDEX.readRegB = state->MEMWB.writeData; 
      }
    }

    if(opcode(state->EXMEM.instr) == 0x0) {
      if(field_r1(state->IDEX.instr) != 0 && 
      field_r1(state->IDEX.instr) == field_r3(state->EXMEM.instr)) {
        state->IDEX.readRegA = state->EXMEM.aluResult; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      field_r2(state->IDEX.instr) == field_r3(state->EXMEM.instr) && 
      opcode(state->IDEX.instr) != 0x23) {
        state->IDEX.readRegB = state->EXMEM.aluResult; 
      }
    }
    else if(opcode(state->EXMEM.instr) == 0x23 || 
    opcode(state->EXMEM.instr) == 0x8 || opcode(state->EXMEM.instr) == 0x4) {
      if(field_r1(state->IDEX.instr) != 0 && field_r1(state->IDEX.instr) == field_r2(state->EXMEM.instr)) {
        state->IDEX.readRegA = state->EXMEM.aluResult; 
      }
      if(field_r2(state->IDEX.instr) != 0 && 
      opcode(state->IDEX.instr) != opcode(state->EXMEM.instr) && 
      field_r2(state->IDEX.instr) == field_r2(state->EXMEM.instr) && 
      opcode(state->IDEX.instr) != 0x8) {
        state->IDEX.readRegB = state->EXMEM.aluResult; 
      }
    }

    /*perform the actual execution for the instruction*/
    if(opcode(state->IDEX.instr) == 0x0) {
      if(field_r3(state->IDEX.instr) == 0) {
        new.EXMEM.aluResult = 0; 
      }
      else if(func(state->IDEX.instr) == 0x20){
        new.EXMEM.aluResult = state->IDEX.readRegA + state->IDEX.readRegB;
      }
      else if(func(state->IDEX.instr) == 0x22) {
        new.EXMEM.aluResult = state->IDEX.readRegA - state->IDEX.readRegB;
      }
      else if(func(state->IDEX.instr) == 0x4) {
        new.EXMEM.aluResult = state->IDEX.readRegA << state->IDEX.readRegB;
      }
      else if(func(state->IDEX.instr) == 0x6) {
        new.EXMEM.aluResult = state->IDEX.readRegA >> state->IDEX.readRegB;
      }
      else if(func(state->IDEX.instr) == 0x24) {
        new.EXMEM.aluResult = state->IDEX.readRegA & state->IDEX.readRegB;
      }
      else if(func(state->IDEX.instr) == 0x25) {
        new.EXMEM.aluResult = state->IDEX.readRegA | state->IDEX.readRegB;
      }
       
      new.EXMEM.readRegB = state->IDEX.readRegB; 
    }
    else if(opcode(state->IDEX.instr) == 0x23 || 
            opcode(state->IDEX.instr) == 0x8 ||
            opcode(state->IDEX.instr) == 0x2B) {
      if(opcode(state->IDEX.instr) == 0x8 && field_r2(state->IDEX.instr) == 0) {
        new.EXMEM.aluResult = 0; 
        new.EXMEM.readRegB = 0; 
      }
      else {
        new.EXMEM.aluResult = state->IDEX.readRegA + state->IDEX.offset; 
        new.EXMEM.readRegB = state->IDEX.readRegB;
      }
      
       
    }
    /*checks the actual direction of the branch instruction*/
    else if(opcode(state->IDEX.instr) == 0x4) {
      new.EXMEM.aluResult = state->IDEX.pcPlus1 + (state->IDEX.offset); 
      new.EXMEM.readRegB = state->IDEX.readRegB; 
      if((state->IDEX.readRegA == 0 && state->IDEX.offset > 0) ||
         (state->IDEX.readRegA != 0 && state->IDEX.offset < 0)) {
          new.pc = state->IDEX.offset + state->IDEX.pcPlus1; 
          new.IFID.instr = 0x00000020; 
          new.IDEX.instr = 0x00000020;  
          new.IDEX.pcPlus1 = 0; 
          new.IFID.pcPlus1 = 0; 
          new.IDEX.readRegA = 0; 
          new.IDEX.readRegB = 0; 
          new.IDEX.offset = 32; 
      }
    }
    /*for the halt instruction*/
    else if(opcode(state->IDEX.instr) == 0x3F) {
      new.EXMEM.aluResult = 0; 
      new.EXMEM.readRegB = 0; 
    }


    new.EXMEM.instr = state->IDEX.instr; 
    /* --------------------- MEM stage --------------------- */
    /*performs the MEM stage actions required for a load*/
    if(opcode(state->EXMEM.instr) == 0x23) {
      if(field_r2(state->EXMEM.instr) == 0) {
        new.MEMWB.writeData = 0; 
      }
      else {
        new.MEMWB.writeData = state->dataMem[state->EXMEM.aluResult/4];
      }
    }
    /*performs the MEM stage actions required for a store*/
    else if(opcode(state->EXMEM.instr) == 0x2B) {
      new.dataMem[state->EXMEM.aluResult/4] = state->EXMEM.readRegB; 
      new.MEMWB.writeData = state->EXMEM.readRegB; 
    }
    /*performs the MEM stage actions required for the other instructions*/
    else {
      if(opcode(state->EXMEM.instr) == 0x8 && field_r2(state->EXMEM.instr) == 0) {
        new.MEMWB.writeData = 0; 
      }
      else if((opcode(state->EXMEM.instr) == 0x0) 
      && field_r3(state->EXMEM.instr) == 0) {
        new.MEMWB.writeData = 0; 
      }
      else {
        new.MEMWB.writeData = state->EXMEM.aluResult;
      }
    }
    new.MEMWB.instr = state->EXMEM.instr; 
    /* --------------------- WB stage --------------------- */

    /*checks if the instruction is a halt instruction*/
    if(opcode(state->MEMWB.instr) == 0x3F) {
      printf("machine halted\n"); 
      printf("total of %d cycles executed\n", state->cycles); 
      break; 
    }
    /*otherwise does what is necessary for a specific instruction in the WB stage*/
    if(opcode(state->MEMWB.instr) == 0x23 || opcode(state->MEMWB.instr) == 0x8) {
      new.reg[field_r2(state->MEMWB.instr)] = state->MEMWB.writeData;
    }
    else if(opcode(state->MEMWB.instr) == 0x0) {
      new.reg[field_r3(state->MEMWB.instr)] = state->MEMWB.writeData; 
    }

    new.WBEND.writeData = state->MEMWB.writeData;

    new.WBEND.instr = state->MEMWB.instr; 
    /* --------------------- end stage --------------------- */
  
    /* transfer new state into current state */
    memcpy(state, &new, sizeof(state_t));
  }
}
/************************************************************/

/************************************************************/
int opcode(int instruction) { return (instruction >> OP_SHIFT) & OP_MASK; }
/************************************************************/

/************************************************************/
int func(int instruction) { return (instruction & FUNC_MASK); }
/************************************************************/

/************************************************************/
int field_r1(int instruction) { return (instruction >> R1_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r2(int instruction) { return (instruction >> R2_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r3(int instruction) { return (instruction >> R3_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_imm(int instruction) { return (instruction & IMMEDIATE_MASK); }
/************************************************************/

/************************************************************/
int offset(int instruction) {
  /* only used for lw, sw, beqz */
  return convertNum(field_imm(instruction));
}
/************************************************************/

/************************************************************/
int convertNum(int num) {
  /* convert a 16 bit number into a 32-bit Sun number */
  if (num & 0x8000) {
    num -= 65536;
  }
  return (num);
}
/************************************************************/

/************************************************************/
void printState(Pstate state) {
  short i;
  printf("@@@\nstate before cycle %d starts\n", state->cycles);
  printf("\tpc %d\n", state->pc);

  printf("\tdata memory:\n");
  for (i = 0; i < state->numMemory; i++) {
    printf("\t\tdataMem[ %d ] %d\n", i, state->dataMem[i]);
  }
  printf("\tregisters:\n");
  for (i = 0; i < NUMREGS; i++) {
    printf("\t\treg[ %d ] %d\n", i, state->reg[i]);
  }
  printf("\tIFID:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IFID.instr);
  printf("\t\tpcPlus1 %d\n", state->IFID.pcPlus1);
  printf("\tIDEX:\n");
  printf("\t\tinstruction ");
  printInstruction(state->IDEX.instr);
  printf("\t\tpcPlus1 %d\n", state->IDEX.pcPlus1);
  printf("\t\treadRegA %d\n", state->IDEX.readRegA);
  printf("\t\treadRegB %d\n", state->IDEX.readRegB);
  printf("\t\toffset %d\n", state->IDEX.offset);
  printf("\tEXMEM:\n");
  printf("\t\tinstruction ");
  printInstruction(state->EXMEM.instr);
  printf("\t\taluResult %d\n", state->EXMEM.aluResult);
  printf("\t\treadRegB %d\n", state->EXMEM.readRegB);
  printf("\tMEMWB:\n");
  printf("\t\tinstruction ");
  printInstruction(state->MEMWB.instr);
  printf("\t\twriteData %d\n", state->MEMWB.writeData);
  printf("\tWBEND:\n");
  printf("\t\tinstruction ");
  printInstruction(state->WBEND.instr);
  printf("\t\twriteData %d\n", state->WBEND.writeData);
}
/************************************************************/

/************************************************************/
void printInstruction(int instr) {

  if (opcode(instr) == REG_REG_OP) {

    if (func(instr) == ADD_FUNC) {
      print_rtype(instr, "add");
    } else if (func(instr) == SLL_FUNC) {
      print_rtype(instr, "sll");
    } else if (func(instr) == SRL_FUNC) {
      print_rtype(instr, "srl");
    } else if (func(instr) == SUB_FUNC) {
      print_rtype(instr, "sub");
    } else if (func(instr) == AND_FUNC) {
      print_rtype(instr, "and");
    } else if (func(instr) == OR_FUNC) {
      print_rtype(instr, "or");
    } else {
      printf("data: %d\n", instr);
    }

  } else if (opcode(instr) == ADDI_OP) {
    print_itype(instr, "addi");
  } else if (opcode(instr) == LW_OP) {
    print_itype(instr, "lw");
  } else if (opcode(instr) == SW_OP) {
    print_itype(instr, "sw");
  } else if (opcode(instr) == BEQZ_OP) {
    print_itype(instr, "beqz");
  } else if (opcode(instr) == HALT_OP) {
    printf("halt\n");
  } else {
    printf("data: %d\n", instr);
  }
}
/************************************************************/

/************************************************************/
void print_rtype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r3(instr), field_r1(instr),
         field_r2(instr));
}
/************************************************************/

/************************************************************/
void print_itype(int instr, const char *name) {
  printf("%s %d %d %d\n", name, field_r2(instr), field_r1(instr),
         offset(instr));
}
/************************************************************/
