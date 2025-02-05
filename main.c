#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

void hexdump(uint32_t addr, uint8_t *data, uint32_t len)
{
	int i, j;

	for (i = 0; i < len;) {
		printf("%08x: ", addr + i);
		for (j = 0; j < 16; j++) {
			if (i + j < len) printf("%02x ", data[i + j]);
			else printf("   ");
		}
		printf(" ");
		for (j = 0; j < 16 && i + j < len; j++) {
			if (data[i + j] > 31 && data[i + j] < 127) 
				printf("%c", data[i + j]);
			else printf(".");
		}
		printf("\n");
		i += j;
	}
}

int32_t is_compressed(uint16_t c)
{
	if ((c & 0x3) == 0x3) return 0;
	return 1;
}
/*
	risc-v registers

	- 32 general purpose registers x0 - x31

	#    | compressed enc. |  ABI name | saved by calle_ | desc. 
	-----|-----------------|-----------|-----------------|--------------------
	x0   |     -           |   "zero"  |   -             |   hardwired zero
	x1   |     -           |   "ra  "  |   R             |   return address
	x2   |     -           |   "sp"    |   E             |   stack pointer
	x3   |     -           |   "gp"    |   -             |   global pointer
	x4   |     -           |   "tp"    |   -             |   thread pointer
	x5   |     -           |   "t0"    |   R             |   temp register 0
	x6   |     -           |   "t1"    |   R             |   temp register 1
	x7   |     -           |   "t2"    |   R             |   temp register 2
	x8   |     0           |   "s0/fp" |   E             |   saved reg 0 / frame pointer
	x9   |     1           |   "s1"    |   E             |   saved reg 1
	x10  |     2           |   "a0"    |   R             |   argument 0 / ret value 0
	x11  |     3           |   "a1"    |   R             |   argument 1 / ret value 1
	x12  |     4           |   "a2"    |   R             |   argument 2
	x13  |     5           |   "a3"    |   R             |   argument 3 
	x14  |     6           |   "a4"    |   R             |   argument 4 
	x15  |     7           |   "a5"    |   R             |   argument 5 
	x16  |     -           |   "a6"    |   R             |   argument 6 
	x17  |     -           |   "a7"    |   R             |   argument 7 
	x18  |     -           |   "s2"    |   E             |   saved reg 2 
	x19  |     -           |   "s3"    |   E             |   saved reg 3 
	x20  |     -           |   "s4"    |   E             |   saved reg 4 
	x21  |     -           |   "s5"    |   E             |   saved reg 5 
	x22  |     -           |   "s6"    |   E             |   saved reg 6 
	x23  |     -           |   "s7"    |   E             |   saved reg 7 
	x24  |     -           |   "s8"    |   E             |   saved reg 8 
	x25  |     -           |   "s9"    |   E             |   saved reg 9 
	x26  |     -           |   "s10"   |   E             |   saved reg 10 
	x27  |     -           |   "s11"   |   E             |   saved reg 11 
	x28  |     -           |   "t3"    |   R             |   temp register 3 
	x29  |     -           |   "t4"    |   R             |   temp register 4 
	x30  |     -           |   "t5"    |   R             |   temp register 5 
	x31  |     -           |   "t6"    |   R             |   temp register 6 
	
*/

struct cpu_state{
	uint32_t regs[32];
	uint32_t pc;
	uint8_t *ram;
};

uint32_t read_word(uint32_t addr, struct cpu_state *cs)
{
	uint32_t w;

	if (addr & 0xc0000000) { /* UART registers */
		printf("read from 0x%x\n", addr);
		return 0;
	}
	w = cs->ram[addr & 0xfffffffc];
	w |= cs->ram[(addr & 0xfffffffc) + 1] << 8;
	w |= cs->ram[(addr & 0xfffffffc) + 2] << 16;
	w |= cs->ram[(addr & 0xfffffffc) + 3] << 24;

	return w;
}

void write_word(uint32_t addr, uint32_t data, struct cpu_state *cs)
{
	if (addr & 0xc0000000) { /* UART registers */
		printf("write 0x%x to 0x%x\n", data, addr);
		return;
	}
	if (addr > 65532) {
		printf("addr = 0x%x, data = 0x%x\n", addr, data);
		while(1);
	}
	cs->ram[addr & 0xfffffffc] = (data >> 0) & 0xff;
	cs->ram[(addr & 0xfffffffc) + 1] = (data >> 8) & 0xff;
	cs->ram[(addr & 0xfffffffc) + 2] = (data >> 16) & 0xff;
	cs->ram[(addr & 0xfffffffc) + 3] = (data >> 24) & 0xff;

}


void decode_compressed_cmd(uint16_t cmd, struct cpu_state *cs)
{
	uint32_t rd;
	uint32_t rs1, rs2;
	int32_t imm;

	if (cs->regs[0] != 0) {
		printf("*** zero overwritten\n");
		cs->regs[0] = 0;
	}

	/* ADDI */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x0) {
		rd = (cmd >> 7) & 0x1f;
		imm = (cmd >> 2) & 0x1f;
		if (imm & (1 << 4)) imm |= 0xfffffff0;
		printf("addi x%d, x%d, %d\n", rd, rd, imm);
		cs->regs[rd] += imm;
		goto exit1;
	}
	/* ANDI */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x4 && ((cmd >> 10) & 0x3) == 2) {
		rd = 8 + ((cmd >> 7) & 0x7);
		imm = (cmd >> 2) & 0x1f;
		imm |= (cmd & (1 << 12)) ? (1 << 5) : 0;
		if (imm & (1 << 4)) imm |= 0xfffffff0;
		printf("andi x%d, x%d, %d\n", rd, rd, imm);
		cs->regs[rd] = cs->regs[rd] & imm;
		goto exit1;
	}
	/* ADDI16SP */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x3) {
		rd = ((cmd >> 7) & 0x1f);
		if (rd == 0 || rd == 2) {
			imm = (cmd & (1 << 2)) ? (1 << 5) : 0;
			imm |= (cmd & (1 << 3)) ? (1 << 7) : 0;
			imm |= (cmd & (1 << 4)) ? (1 << 8) : 0;
			imm |= (cmd & (1 << 5)) ? (1 << 6) : 0;
			imm |= (cmd & (1 << 6)) ? (1 << 4) : 0;
			imm |= (cmd & (1 << 12)) ? (1 << 9) : 0;
			if (imm & (1 << 9)) imm |= 0xfffffc00;
			printf("addi sp, sp, %d\n", imm);
			cs->regs[2] += imm;
			goto exit1;
		}
	}
	/* ADDI14SPN */
	if ((cmd & 0x03) == 0x00 && ((cmd >> 13) & 0x07) == 0x0) {
		rd = 8 + ((cmd >> 2) & 0x7);
		imm = ((cmd >> 7) & 0xf) << 6;
		imm |= (cmd & (1 << 12)) ? (1 << 5) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 2) : 0;
		if (imm & (1 << 9)) imm |= 0xfffffc00;
		printf("addi x%d, sp, %d\n", rd, imm);
		cs->regs[rd] += cs->regs[2] + imm;
		goto exit1;
	}
	/* MV */
	if ((cmd & 0x03) == 0x02 && ((cmd >> 12) & 0x0f) == 0x8) {
		rd = (cmd >> 7) & 0x1f;
		rs2 = (cmd >> 2) & 0x1f;
		if (rd != 0 && rs2 != 0) {
			printf("mv x%d, x%d\n", rd, rs2);
			cs->regs[rd] = cs->regs[rs2];
			goto exit1;
		}
	}
	/* ADD */
	if ((cmd & 0x03) == 0x02 && ((cmd >> 12) & 0x0f) == 0x9) {
		rd = (cmd >> 7) & 0x1f;
		rs2 = (cmd >> 2) & 0x1f;
		if (rd != 0 && rs2 != 0) {
			printf("add x%d, x%d\n", rd, rs2);
			cs->regs[rd] += cs->regs[rs2];
			goto exit1;
		}
	}
	/* J */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x5) {
		imm = (cmd & (1 << 2)) ? (1 << 5) : 0;
		imm |= (cmd & (1 << 3)) ? (1 << 1) : 0;
		imm |= (cmd & (1 << 4)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 7) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 6) : 0;
		imm |= (cmd & (1 << 8)) ? (1 << 10) : 0;
		imm |= (cmd & (1 << 9)) ? (1 << 8) : 0;
		imm |= (cmd & (1 << 10)) ? (1 << 9) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 12)) ? (1 << 11) : 0;
		//imm = (cmd >> 2) & 0x7ff;
		if (imm & (1 << 11)) imm |= 0xfffff000;
		printf("J 0x%x (imm = %d)\n", cs->pc + imm, imm);
		cs->pc += imm;
		goto exit2;
	}
	/* JALR (RET  = jalr x0, x1, 0) */
	if ((cmd & 0x03) == 0x02 && ((cmd >> 12) & 0x0f) == 0x8) {
		rs1 = (cmd >> 7) & 0x1f;
		rs2 = (cmd >> 2) & 0x1f;
		if (rs2 == 0) {
			printf("ret  (0x%x)\n", cs->regs[rs1]);
			cs->pc = cs->regs[rs1];
			goto exit2;
		}
	}
	/* JAL */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x1) {
		imm = (cmd & (1 << 2)) ? (1 << 5) : 0;
		imm |= (cmd & (1 << 3)) ? (1 << 1) : 0;
		imm |= (cmd & (1 << 4)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 7) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 6) : 0;
		imm |= (cmd & (1 << 8)) ? (1 << 10) : 0;
		imm |= (cmd & (1 << 9)) ? (1 << 8) : 0;
		imm |= (cmd & (1 << 10)) ? (1 << 9) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 12)) ? (1 << 11) : 0;
		if (imm & (1 << 11)) imm |= 0xfffff800;
		cs->regs[1] = cs->pc + 2;		
		cs->pc += imm;
		printf("jal, imm = %d\n", imm);
		goto exit2;
	}
	/* BEQZ */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x6) {
		rs2 = 8 + ((cmd >> 7) & 0x7);
		imm = (cmd & (1 << 2)) ? (1 << 5) : 0;
		imm |= (cmd & (1 << 3)) ? (1 << 1) : 0;
		imm |= (cmd & (1 << 4)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 6) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 7) : 0;
		imm |= (cmd & (1 << 10)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 12)) ? (1 << 8) : 0;
		if (imm & (1 << 8)) imm |= 0xfffffe00;
		printf("beqz x%d (%d), 0x%x (imm = %d)\n", rs2, cs->regs[rs2], cs->pc + imm, imm);
		if (cs->regs[rs2] == 0) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* BNEZ */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x7) {
		rs1 = 8 + ((cmd >> 7) & 0x7);
		imm = (cmd & (1 << 2)) ? (1 << 5) : 0;
		imm |= (cmd & (1 << 3)) ? (1 << 1) : 0;
		imm |= (cmd & (1 << 4)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 6) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 7) : 0;
		imm |= (cmd & (1 << 10)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 12)) ? (1 << 8) : 0;
		if (imm & (1 << 8)) imm |= 0xfffff800;
		//printf("imm = %d\n", imm);
		printf("bnez %d, 0x%x (imm = %d)\n", cs->pc + imm, imm);
		if (cs->regs[rs1] != 0) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* LI */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x2) {
		rd = (cmd >> 7) & 0x1f;
		imm = (cmd >> 2) & 0x1f;
		if (imm & (1 << 4)) imm |= 0xfffffff0;
		cs->regs[rd] = imm;
		printf("li x%d, %d\n", rd, imm);
		goto exit1;
	}
	/* LUI */
	if ((cmd & 0x03) == 0x01 && ((cmd >> 13) & 0x07) == 0x3) {
		rd = (cmd >> 7) & 0x1f;
		imm = ((cmd >> 2) & 0x1f) << 12;
		imm |= (cmd & (1 << 12)) ? (1 << 17) : 0;
		if (imm & (1 << 17)) imm |= 0xfffc0000;
		cs->regs[rd] = imm;
		printf("lui x%d, %d\n", rd, imm);
		goto exit1;
	}
	/* LW */
	if ((cmd & 0x03) == 0x00 && ((cmd >> 13) & 0x07) == 0x2) {
		rd = 8 + ((cmd >> 2) & 0x7);
		rs1 = 8 + ((cmd >> 7) & 0x7);
		imm = (cmd & (1 << 5)) ? (1 << 6) : 0;
		imm |= (cmd & (1 << 6)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 10)) ? (1 << 3) : 0;
		imm |= (cmd & (1 << 11)) ? (1 << 4) : 0;
		imm |= (cmd & (1 << 12)) ? (1 << 5) : 0;
		if (imm & (1 << 6)) imm |= 0xffffffc0;
		
		//printf("rd = %d, rs1 = %d (0x%x), imm = %d\n", rd, rs1, cs->regs[rs1], imm);
		cs->regs[rd] = read_word(cs->regs[rs1] + imm, cs);
		printf("lw x%d, %d(x%d) (addr = 0x%x)\n", rd, imm, rs1, cs->regs[rs1] + imm);
		//printf("read 0x%x from addr 0x%x\n", cs->regs[rd], cs->regs[rs1] + imm);
		goto exit1;
	}
	/* SW */
	if ((cmd & 0x03) == 0x00 && ((cmd >> 13) & 0x07) == 0x6) {
		rs2 = 8 + ((cmd >> 2) & 0x7);
		rs1 = 8 + ((cmd >> 7) & 0x7);
		imm = ((cmd >> 10) & 0x7) << 3;
		imm |= (cmd & (1 << 6)) ? (1 << 2) : 0;
		imm |= (cmd & (1 << 5)) ? (1 << 6) : 0;
		if (imm & (1 << 6)) imm |= 0xffffffc0;
		//printf("store 0x%x to addr 0x%x\n", cs->regs[rs2], cs->regs[rs1] + imm);
		printf("sw x%d, %d(x%d) (addr = 0x%x)\n", rs2, imm, rs1, cs->regs[rs1] + imm);
		write_word(cs->regs[rs1] + imm, cs->regs[rs2], cs);

		goto exit1;
	}
	/* SWSP */
	if ((cmd & 0x03) == 0x02 && ((cmd >> 13) & 0x07) == 0x6) {
		rs1 = (cmd >> 2) & 0x1f;
		imm = ((cmd >> 7) & 0x3) << 6;
		imm |= ((cmd >> 9) & 0xf) << 2;
		cs->ram[cs->regs[2] + imm + 0] = cs->regs[rs1] & 0xff;
		cs->ram[cs->regs[2] + imm + 1] = (cs->regs[rs1] >> 8) & 0xff;
		cs->ram[cs->regs[2] + imm + 2] = (cs->regs[rs1] >> 16) & 0xff;
		cs->ram[cs->regs[2] + imm + 3] = (cs->regs[rs1] >> 24) & 0xff;
		printf("sw reg %d, %d(sp)\n", rs1, imm);
		goto exit1;
	}
	printf("invalid compressed instruction\n");
	printf("PC: 0x%x, opcode: 0x%x\n", cs->pc, cmd);
	printf(" op: %d\n", (cmd & 0x3));
	printf(" funct3: %d\n", ((cmd >> 13) & 0x7));
	printf(" funct4: %d\n", ((cmd >> 12) & 0xf));
	
	while(1) sleep(1);

exit1:
	cs->pc += 2;
	return;
exit2:
	return;
	
}

#define OPCODE_LUI   0x37
#define OPCODE_AUIPC 0x17
#define OPCODE_ADDI  0x13
#define OPCODE_B     0x63
#define OPCODE_JAL   0x6f
#define OPCODE_SW    0x23
#define OPCODE_XOR   0x33
#define OPCODE_ADD   0x33
#define OPCODE_L     0x03
 
//19a50513          	addi	a0,a0,410 # 0x1e0

void decode_cmd(uint32_t cmd, struct cpu_state *cs)
{
	uint32_t rd;
	uint32_t rs1;
	uint32_t rs2;
	int32_t imm;

	if (cs->regs[0] != 0) {
		printf("zero overwritten\n");
		cs->regs[0] = 0;
	}
	/* LUI */
	if ((cmd & 0x7f) == OPCODE_LUI) {
		rd = (cmd >> 7) & 0x1f;
		cs->regs[rd] = cmd & 0xfffff000;
		printf("lui x%d, %d\n", rd, cs->regs[rd]);
		goto exit1;
	}
	/* LBU */
	if ((cmd & 0x7f) == OPCODE_L && ((cmd >> 12) & 0x7) == 0x4) {
		imm = (cmd >> 20) & 0xfff;
		rs1 = (cmd >> 15) & 0x1f;
		rd = (cmd >> 7) & 0x1f;
		printf("lbu x%d, %d(x%d) 0x%x\n", rd, imm, rs1, cs->regs[rs1] + imm);
		cs->regs[rd] = cs->ram[cs->regs[rs1] + imm];;
		goto exit1;
	}
	/* AUIPC */
	if ((cmd & 0x7f) == OPCODE_AUIPC) {
		rd = (cmd >> 7) & 0x1f;
		cs->regs[rd] = cs->pc + (cmd & 0xfffff000);
		printf("auipc x%d, %d\n", rd, cmd & 0xfffff000);
		goto exit1;
	}
	/* ADDI */
	if ((cmd & 0x7f) == OPCODE_ADDI && ((cmd >> 12) & 0x7) == 0) {
		rd = (cmd >> 7) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		imm = (cmd >> 20) & 0xfff;
		if (imm & (1 << 11)) imm |= 0xfffff000;
		printf("addi x%d, x%d, %d\n", rd, rs1, imm);
		cs->regs[rd] = cs->regs[rs1] + imm;
		printf("result = 0x%x\n", cs->regs[rd]);
		goto exit1;
	}
	/* ANDI */
	if ((cmd & 0x7f) == OPCODE_ADDI && ((cmd >> 12) & 0x7) == 7) {
		rd = (cmd >> 7) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		imm = (cmd >> 20) & 0xfff;
		if (imm & (1 << 11)) imm |= 0xfffff000;
		printf("andi x%d, x%d, %d\n", rd, rs1, imm);
		cs->regs[rd] = cs->regs[rs1] & imm;
		goto exit1;
	}
	/* XOR */
	if ((cmd & 0x7f) == OPCODE_XOR && ((cmd >> 25) & 0x7f) == 0 && ((cmd >> 12) & 0x7) == 4) {
		rd = (cmd >> 7) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		printf("xor x%d, x%d, x%d\n", rd, rs1, rs2);
		cs->regs[rd] = cs->regs[rs1] ^ cs->regs[rs2];
		goto exit1;
	}
	/* ADD */
	if ((cmd & 0x7f) == OPCODE_ADD && ((cmd >> 25) & 0x7f) == 0 && ((cmd >> 12) & 0x7) == 0) {
		rd = (cmd >> 7) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		printf("add x%d, x%d, x%d\n", rd, rs1, rs2);
		cs->regs[rd] = cs->regs[rs1] + cs->regs[rs2];
		goto exit1;
	}
	/* SUB */
	if ((cmd & 0x7f) == OPCODE_ADD && ((cmd >> 25) & 0x7f) == 0x20 && ((cmd >> 12) & 0x7) == 0) {
		rd = (cmd >> 7) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		cs->regs[rd] = cs->regs[rs1] - cs->regs[rs2];
		printf("sub x%d, x%d, x%d (res = %d)\n", rd, rs1, rs2, cs->regs[rd]);
		goto exit1;
	}
	/* SW */
	if ((cmd & 0x7f) == OPCODE_SW && ((cmd >> 12) & 0x7) == 2) {
		rs2 = (cmd >> 20) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		imm = (cmd >> 7) & 0x1f;
		imm |= ((cmd >> 25) & 0x7f) << 5;
		if (imm & (1 << 11)) imm |= 0xfffff000;
		printf("sw x%d, %d(x%d) (addr = 0x%x)\n", rs2, imm, rs1, cs->regs[rs1] + imm);	
		write_word(cs->regs[rs1] + imm, cs->regs[rs2], cs);
		goto exit1;
	}
	/* LW */
	if ((cmd & 0x7f) == OPCODE_L && ((cmd >> 12) & 0x7) == 2) {
		//rs2 = (cmd >> 20) & 0x1f;
		rs1 = (cmd >> 15) & 0x1f;
		rd = (cmd >> 7) & 0x1f;
		imm = ((cmd >> 20) & 0xfff);
		if (imm & (1 << 11)) imm |= 0xfffff000;
		cs->regs[rd] = read_word(cs->regs[rs1] + imm, cs);
		printf("lw x%d, %d(x%d) (addr = 0x%x)\n", rd, imm, rs1, cs->regs[rs1] + imm);	
		goto exit1;
	}
	/* BNE */
	if ((cmd & 0x7f) == OPCODE_B && ((cmd >> 12) & 0x7) == 1) {
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		imm = ((cmd >> 8) & 0xf) << 1;
		imm |= ((cmd >> 24) & 0x3f) << 5;
		imm |= (cmd & (1 << 7)) ? (1 << 11) : 0;
		imm |= (cmd & (1 << 31)) ? (1 << 12) : 0;
		if (imm & (1 << 12)) imm |= 0xfffff000;
		
		printf("bge x%d, x%d, 0x%x\n", rs1, rs2, cs->pc + imm);
		
		if (cs->regs[rs1] != (int32_t)cs->regs[rs2]) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* BGE */
	if ((cmd & 0x7f) == OPCODE_B && ((cmd >> 12) & 0x7) == 5) {
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		imm = ((cmd >> 25) & 0x3f) << 5;
		imm |= (cmd & (1 << 31)) ? (1 << 12) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 11) : 0;
		imm |= ((cmd >> 8) & 0xf) << 1;
		if (imm & (1 << 12)) imm |= 0xfffff000;
		
		//printf("BGE rs1 = %d, rs2 = %d, imm = 0x%x, new 0x%x\n", rs1, rs2, imm,
		//	cs->pc + imm);
		printf("bge x%d, x%d, 0x%x\n", rs1, rs2, cs->pc + imm);
		if (cs->regs[rs1] >= (int32_t)cs->regs[rs2]) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* BGEU */
	if ((cmd & 0x7f) == OPCODE_B && ((cmd >> 12) & 0x7) == 7) {
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		imm = ((cmd >> 25) & 0x3f) << 5;
		imm |= (cmd & (1 << 31)) ? (1 << 12) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 11) : 0;
		imm |= ((cmd >> 8) & 0xf) << 1;
		if (imm & (1 << 12)) imm |= 0xfffff000;
		
		//printf("BGEU rs1 = %d, rs2 = %d, imm = 0x%x, new 0x%x\n", rs1, rs2, imm,
		//	cs->pc + imm);
		printf("bgeu x%d, x%d, 0x%x\n", rs1, rs2, cs->pc + imm);
		if (cs->regs[rs1] >= cs->regs[rs2]) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* BLT */
	if ((cmd & 0x7f) == OPCODE_B && ((cmd >> 12) & 0x7) == 4) {
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		imm = ((cmd >> 25) & 0x3f) << 5;
		imm |= (cmd & (1 << 31)) ? (1 << 12) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 11) : 0;
		imm |= ((cmd >> 8) & 0xf) << 1;
		if (imm & (1 << 12)) imm |= 0xfffff000;
		
		printf("blt x%d, x%d, 0x%x\n", rs1, rs2, cs->pc + imm);
//		printf("    rs1 = %d (%d), rs2 = %d (%d), imm = 0x%x, new 0x%x\n", 
//			rs1, cs->regs[rs1], rs2, cs->regs[rs2], imm,
//			cs->pc + imm);
//		printf("    if %d < %d jump\n", cs->regs[rs1], cs->regs[rs2]);
		if ((int32_t)cs->regs[rs1] < (int32_t)cs->regs[rs2]) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* BLTU */
	if ((cmd & 0x7f) == OPCODE_B && ((cmd >> 12) & 0x7) == 6) {
		rs1 = (cmd >> 15) & 0x1f;
		rs2 = (cmd >> 20) & 0x1f;
		imm = ((cmd >> 25) & 0x3f) << 5;
		imm |= (cmd & (1 << 31)) ? (1 << 12) : 0;
		imm |= (cmd & (1 << 7)) ? (1 << 11) : 0;
		imm |= ((cmd >> 8) & 0xf) << 1;
		if (imm & (1 << 12)) imm |= 0xfffff000;
		
		printf("bltu x%d, x%d, 0x%x\n", rs1, rs2, cs->pc + imm);
//		printf("    rs1 = %d (%u), rs2 = %d (%u), imm = 0x%x, new 0x%x\n", 
//			rs1, cs->regs[rs1], rs2, cs->regs[rs2], imm,
//			cs->pc + imm);
//		printf("    if %d < %d jump\n", cs->regs[rs1], cs->regs[rs2]);
		if (cs->regs[rs1] < cs->regs[rs2]) {
			cs->pc += imm;
			goto exit2;
		}
		goto exit1;
	}
	/* JAL */
	//if ((cmd & 0x7f) == OPCODE_JAL && ((cmd >> 12) & 0x7) == 5) {
	if ((cmd & 0x7f) == OPCODE_JAL) {
		rd = (cmd >> 7) & 0x1f;
		imm = ((cmd >> 21) & 0x3ff) << 1;
		imm |= (cmd & (1 << 31)) ? (1 << 20) : 0;
		imm |= (cmd & (1 << 20)) ? (1 << 11) : 0;
		imm |= ((cmd >> 12) & 0xff) << 12;
		if (imm & (1 << 20)) imm |= 0xfff00000;
		//printf("rd = %d, imm = 0x%x, new 0x%x\n", rd, imm,
		//	cs->pc + imm);
		printf("jal x%d, 0x%x\n", rd, cs->pc + imm);
		cs->regs[rd] = cs->pc + 4;
		cs->pc += imm;	
		goto exit2;
	}
	printf("invalid instruction\n");
	while(1) sleep(1);

exit1:
	cs->pc += 4;
	return;
exit2:
	return;
}

void decode_loop(struct cpu_state *cs)
{
	int ret;
	uint32_t cmd;
	uint32_t compressed;
	uint16_t *p = (uint16_t*)cs->ram;

	while(1) {
		if (cs->regs[0]) {
			printf("non-zero x0 value 0x%x\n", cs->regs[0]);
			break;
		}
		if (cs->pc & 0x1) {
			printf("invalid PC value 0x%x\n", cs->pc);
			break;
		}
		cmd = p[cs->pc / 2];			
		compressed = is_compressed(cmd);
		if (!compressed) {
			cmd |= (p[(cs->pc + 2) / 2]) << 16; 
			printf("0x%04x: %08x    ", cs->pc, cmd);
			decode_cmd(cmd, cs);
		}
		else {
			printf("0x%04x: %04x        ", cs->pc, cmd);
			decode_compressed_cmd(cmd, cs);
		}
//		printf("x12 = 0x%x\n", cs->regs[12]);
//		printf("x13 = 0x%x\n", cs->regs[13]);
//		printf("x14 = 0x%x\n", cs->regs[14]);
//		printf("x17 = 0x%x\n", cs->regs[17]);
	}
}

int main(int argc, char *argv[])
{
	int ret;
	FILE *h;
	uint32_t size;
	uint8_t *buf;
	struct cpu_state cs;

	printf("risc-v emulator\n");

	if (argc < 2) goto error;
	h = fopen(argv[1], "r");
	if (h == NULL) goto error;
	fseek(h, 0, SEEK_END);
	size = ftell(h);
	fseek(h, 0, SEEK_SET);
	buf = (uint8_t*)malloc(size);
	if (!buf) goto error;
	fread(buf, 1, size, h);
	fclose(h);
	
	memset(&cs, 0, sizeof(struct cpu_state));
	cs.ram = buf;

	decode_loop(&cs);


	return 0;

error:
	printf("usage: %s <.bin>\n", argv[0]);
	return 1;
	
}

