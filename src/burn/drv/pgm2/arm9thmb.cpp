// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz


#include "mame_stuff.h"
#include "arm9.h"
#include "arm9core.h"

/* Shift operations */

static void tg00_0(u32 pc, u32 op) /* Shift left */
{
	u32 rs, rd, rrs;
	s32 offs;

	arm_set_cpsr(GET_CPSR & ~(N_MASK | Z_MASK));

	rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	rrs = GetRegister(rs);
	offs = (op & THUMB_SHIFT_AMT) >> THUMB_SHIFT_AMT_SHIFT;
	if (offs != 0)
	{
		SetRegister(rd, rrs << offs);
		if (rrs & (1 << (31 - (offs - 1))))
		{
			arm_set_cpsr(GET_CPSR | C_MASK);
		}
		else
		{
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	else
	{
		SetRegister(rd, rrs);
	}
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg00_1(u32 pc, u32 op) /* Shift right */
{
	u32 rs, rd, rrs;
	s32 offs;

	rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	rrs = GetRegister(rs);
	offs = (op & THUMB_SHIFT_AMT) >> THUMB_SHIFT_AMT_SHIFT;
	if (offs != 0)
	{
		SetRegister(rd, rrs >> offs);
		if (rrs & (1 << (offs - 1)))
		{
			arm_set_cpsr(GET_CPSR | C_MASK);
		}
		else
		{
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	else
	{
		SetRegister(rd, 0);
		if (rrs & 0x80000000)
		{
			arm_set_cpsr(GET_CPSR | C_MASK);
		}
		else
		{
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

/* Arithmetic */

static void tg01_0(u32 pc, u32 op)
{
	u32 rs, rd, rrs;
	s32 offs;
	/* ASR.. */
	//if (op & THUMB_SHIFT_R) /* Shift right */
	{
		rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
		rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
		rrs = GetRegister(rs);
		offs = (op & THUMB_SHIFT_AMT) >> THUMB_SHIFT_AMT_SHIFT;
		if (offs == 0)
		{
			offs = 32;
		}
		if (offs >= 32)
		{
			if (rrs >> 31)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
			SetRegister(rd, (rrs & 0x80000000) ? 0xFFFFFFFF : 0x00000000);
		}
		else
		{
			if ((rrs >> (offs - 1)) & 1)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
			SetRegister(rd,
							(rrs & 0x80000000)
							? ((0xFFFFFFFF << (32 - offs)) | (rrs >> offs))
							: (rrs >> offs));
		}
		arm_set_cpsr(GET_CPSR & ~(N_MASK | Z_MASK));
		arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
		R15 += 2;
	}
}

static void tg01_10(u32 pc, u32 op)  /* ADD Rd, Rs, Rn */
{
	u32 rn = GetRegister((op & THUMB_ADDSUB_RNIMM) >> THUMB_ADDSUB_RNIMM_SHIFT);
	u32 rs = GetRegister((op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT);
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, rs + rn);
	HandleThumbALUAddFlags(GetRegister(rd), rs, rn);

}

static void tg01_11(u32 pc, u32 op) /* SUB Rd, Rs, Rn */
{
	u32 rn = GetRegister((op & THUMB_ADDSUB_RNIMM) >> THUMB_ADDSUB_RNIMM_SHIFT);
	u32 rs = GetRegister((op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT);
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, rs - rn);
	HandleThumbALUSubFlags(GetRegister(rd), rs, rn);

}

static void tg01_12(u32 pc, u32 op) /* ADD Rd, Rs, #imm */
{
	u32 imm = (op & THUMB_ADDSUB_RNIMM) >> THUMB_ADDSUB_RNIMM_SHIFT;
	u32 rs = GetRegister((op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT);
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, rs + imm);
	HandleThumbALUAddFlags(GetRegister(rd), rs, imm);

}

static void tg01_13(u32 pc, u32 op) /* SUB Rd, Rs, #imm */
{
	u32 imm = (op & THUMB_ADDSUB_RNIMM) >> THUMB_ADDSUB_RNIMM_SHIFT;
	u32 rs = GetRegister((op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT);
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, rs - imm);
	HandleThumbALUSubFlags(GetRegister(rd), rs,imm);

}

/* CMP / MOV */

static void tg02_0(u32 pc, u32 op)
{
	u32 rd = (op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT;
	u32 op2 = (op & THUMB_INSN_IMM);
	SetRegister(rd, op2);
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg02_1(u32 pc, u32 op)
{
	u32 rn = GetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT);
	u32 op2 = op & THUMB_INSN_IMM;
	u32 rd = rn - op2;
	HandleThumbALUSubFlags(rd, rn, op2);
}

/* ADD/SUB immediate */

static void tg03_0(u32 pc, u32 op) /* ADD Rd, #Offset8 */
{
	u32 rn = GetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT);
	u32 op2 = op & THUMB_INSN_IMM;
	u32 rd = rn + op2;
	SetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT, rd);
	HandleThumbALUAddFlags(rd, rn, op2);
}

static void tg03_1(u32 pc, u32 op) /* SUB Rd, #Offset8 */
{
	u32 rn = GetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT);
	u32 op2 = op & THUMB_INSN_IMM;
	u32 rd = rn - op2;
	SetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT, rd);
	HandleThumbALUSubFlags(rd, rn, op2);
}

/* Rd & Rm instructions */

static void tg04_00_00(u32 pc, u32 op) /* AND Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, GetRegister(rd) & GetRegister(rs));
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_01(u32 pc, u32 op) /* EOR Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, GetRegister(rd) ^ GetRegister(rs));
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_02(u32 pc, u32 op) /* LSL Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rrd = GetRegister(rd);
	s32 offs = GetRegister(rs) & 0x000000ff;
	if (offs > 0)
	{
		if (offs < 32)
		{
			SetRegister(rd, rrd << offs);
			if (rrd & (1 << (31 - (offs - 1))))
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
		}
		else if (offs == 32)
		{
			SetRegister(rd, 0);
			if (rrd & 1)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
		}
		else
		{
			SetRegister(rd, 0);
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_03(u32 pc, u32 op) /* LSR Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rrd = GetRegister(rd);
	s32 offs = GetRegister(rs) & 0x000000ff;
	if (offs >  0)
	{
		if (offs < 32)
		{
			SetRegister(rd, rrd >> offs);
			if (rrd & (1 << (offs - 1)))
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
		}
		else if (offs == 32)
		{
			SetRegister(rd, 0);
			if (rrd & 0x80000000)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
		}
		else
		{
			SetRegister(rd, 0);
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_04(u32 pc, u32 op) /* ASR Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rrs = GetRegister(rs)&0xff;
	u32 rrd = GetRegister(rd);
	if (rrs != 0)
	{
		if (rrs >= 32)
		{
			if (rrd >> 31)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
			SetRegister(rd, (GetRegister(rd) & 0x80000000) ? 0xFFFFFFFF : 0x00000000);
		}
		else
		{
			if ((rrd >> (rrs-1)) & 1)
			{
				arm_set_cpsr(GET_CPSR | C_MASK);
			}
			else
			{
				arm_set_cpsr(GET_CPSR & ~C_MASK);
			}
			SetRegister(rd, (rrd & 0x80000000)
							? ((0xFFFFFFFF << (32 - rrs)) | (rrd >> rrs))
							: (rrd >> rrs));
		}
	}
	arm_set_cpsr(GET_CPSR & ~(N_MASK | Z_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_05(u32 pc, u32 op) /* ADC Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 op2 = (GET_CPSR & C_MASK) ? 1 : 0;
	u32 rn = GetRegister(rd) + GetRegister(rs) + op2;
	HandleThumbALUAddFlags(rn, GetRegister(rd), (GetRegister(rs))); // ?
	SetRegister(rd, rn);
}

static void tg04_00_06(u32 pc, u32 op)  /* SBC Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 op2 = (GET_CPSR & C_MASK) ? 0 : 1;
	u32 rn = GetRegister(rd) - GetRegister(rs) - op2;
	HandleThumbALUSubFlags(rn, GetRegister(rd), (GetRegister(rs))); //?
	SetRegister(rd, rn);
}

static void tg04_00_07(u32 pc, u32 op) /* ROR Rd, Rs */
{
	const u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	const u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	const u32 rrd = GetRegister(rd);
	const u32 imm = GetRegister(rs) & 0xff;
	if (imm > 0)
	{
		SetRegister(rd, rotr_32(rrd, imm));	// rotr_32 - circularly shift a 32-bit value right by the specified number of bits (modulo 32)
		if (rrd & (1 << ((imm - 1) & 0x1f)))
		{
			arm_set_cpsr(GET_CPSR | C_MASK);
		}
		else
		{
			arm_set_cpsr(GET_CPSR & ~C_MASK);
		}
	}
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_08(u32 pc, u32 op) /* TST Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd) & GetRegister(rs)));
	R15 += 2;
}

static void tg04_00_09(u32 pc, u32 op) /* NEG Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rrs = GetRegister(rs);
	SetRegister(rd, 0 - rrs);
	HandleThumbALUSubFlags(GetRegister(rd), 0, rrs);
}

static void tg04_00_0a(u32 pc, u32 op) /* CMP Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rn = GetRegister(rd) - GetRegister(rs);
	HandleThumbALUSubFlags(rn, GetRegister(rd), GetRegister(rs));
}

static void tg04_00_0b(u32 pc, u32 op) /* CMN Rd, Rs - check flags, add dasm */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rn = GetRegister(rd) + GetRegister(rs);
	HandleThumbALUAddFlags(rn, GetRegister(rd), GetRegister(rs));
}

static void tg04_00_0c(u32 pc, u32 op) /* ORR Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, GetRegister(rd) | GetRegister(rs));
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_0d(u32 pc, u32 op) /* MUL Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 rn = GetRegister(rd) * GetRegister(rs);
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	SetRegister(rd, rn);
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(rn));
	R15 += 2;
}

static void tg04_00_0e(u32 pc, u32 op) /* BIC Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, GetRegister(rd) & (~GetRegister(rs)));
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

static void tg04_00_0f(u32 pc, u32 op) /* MVN Rd, Rs */
{
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	u32 op2 = GetRegister(rs);
	SetRegister(rd, ~op2);
	arm_set_cpsr(GET_CPSR & ~(Z_MASK | N_MASK));
	arm_set_cpsr(GET_CPSR | HandleALUNZFlags(GetRegister(rd)));
	R15 += 2;
}

/* ADD Rd, Rs group */

static void tg04_01_00(u32 pc, u32 op)
{
	//fatalerror("%08x: G4-1-0 Undefined Thumb instruction: %04x %x\n", pc, op, (op & THUMB_HIREG_H) >> THUMB_HIREG_H_SHIFT);
}

static void tg04_01_01(u32 pc, u32 op) /* ADD Rd, HRs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd, GetRegister(rd) + GetRegister(rs+8));
	// emulate the effects of pre-fetch
	if (rs == 7)
	{
		SetRegister(rd, GetRegister(rd) + 4);
	}

	R15 += 2;
}

static void tg04_01_02(u32 pc, u32 op) /* ADD HRd, Rs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd+8, GetRegister(rd+8) + GetRegister(rs));
	if (rd == 7)
	{
		R15 += 2;
	}

	R15 += 2;
}

static void tg04_01_03(u32 pc, u32 op) /* Add HRd, HRs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd+8, GetRegister(rd+8) + GetRegister(rs+8));
	// emulate the effects of pre-fetch
	if (rs == 7)
	{
		SetRegister(rd+8, GetRegister(rd+8) + 4);
	}
	if (rd == 7)
	{
		R15 += 2;
	}

	R15 += 2;
}

static void tg04_01_10(u32 pc, u32 op)  /* CMP Rd, Rs */
{
	u32 rs = GetRegister(((op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT));
	u32 rd = GetRegister(op & THUMB_HIREG_RD);
	u32 rn = rd - rs;
	HandleThumbALUSubFlags(rn, rd, rs);
}

static void tg04_01_11(u32 pc, u32 op) /* CMP Rd, Hs */
{
	u32 rs = GetRegister(((op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT) + 8);
	u32 rd = GetRegister(op & THUMB_HIREG_RD);
	u32 rn = rd - rs;
	HandleThumbALUSubFlags(rn, rd, rs);
}

static void tg04_01_12(u32 pc, u32 op) /* CMP Hd, Rs */
{
	u32 rs = GetRegister(((op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT));
	u32 rd = GetRegister((op & THUMB_HIREG_RD) + 8);
	u32 rn = rd - rs;
	HandleThumbALUSubFlags(rn, rd, rs);
}

static void tg04_01_13(u32 pc, u32 op) /* CMP Hd, Hs */
{
	u32 rs = GetRegister(((op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT) + 8);
	u32 rd = GetRegister((op & THUMB_HIREG_RD) + 8);
	u32 rn = rd - rs;
	HandleThumbALUSubFlags(rn, rd, rs);
}

/* MOV group */

// "The action of H1 = 0, H2 = 0 for Op = 00 (ADD), Op = 01 (CMP) and Op = 10 (MOV) is undefined, and should not be used."
static void tg04_01_20(u32 pc, u32 op) /* MOV Rd, Rs (undefined) */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd, GetRegister(rs));
	R15 += 2;
}

static void tg04_01_21(u32 pc, u32 op) /* MOV Rd, Hs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd, GetRegister(rs + 8));
	if (rs == 7)
	{
		SetRegister(rd, GetRegister(rd) + 4);
	}
	R15 += 2;
}

static void tg04_01_22(u32 pc, u32 op) /* MOV Hd, Rs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	SetRegister(rd + 8, GetRegister(rs));
	if (rd != 7)
	{
		R15 += 2;
	}
	else
	{
		R15 &= ~1;
	}
}

static void tg04_01_23(u32 pc, u32 op) /* MOV Hd, Hs */
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 rd = op & THUMB_HIREG_RD;
	if (rs == 7)
	{
		SetRegister(rd + 8, GetRegister(rs+8)+4);
	}
	else
	{
		SetRegister(rd + 8, GetRegister(rs+8));
	}
	if (rd != 7)
	{
		R15 += 2;
	}
	else
	{
		R15 &= ~1;
	}
}

static void tg04_01_30(u32 pc, u32 op)
{
	u32 rd = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 addr = GetRegister(rd);
	if (addr & 1)
	{
		addr &= ~1;
	}
	else
	{
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		if (addr & 2)
		{
			addr += 2;
		}
	}
	R15 = addr;
}

static void tg04_01_31(u32 pc, u32 op)
{
	u32 rs = (op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT;
	u32 addr = GetRegister(rs+8);
	if (rs == 7)
	{
		addr += 2;
	}
	if (addr & 1)
	{
		addr &= ~1;
	}
	else
	{
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		if (addr & 2)
		{
			addr += 2;
		}
	}
	R15 = addr;
}

/* BLX */

static void tg04_01_32(u32 pc, u32 op)
{
	u32 addr = GetRegister((op & THUMB_HIREG_RS) >> THUMB_HIREG_RS_SHIFT);
	SetRegister(14, (R15 + 2) | 1);

	// are we also switching to ARM mode?
	if (!(addr & 1))
	{
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		if (addr & 2)
		{
			addr += 2;
		}
	}
	else
	{
		addr &= ~1;
	}

	R15 = addr;
}

static void tg04_01_33(u32 pc, u32 op)
{
	//fatalerror("%08x: G4-3 Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg04_0203(u32 pc, u32 op)
{
	u32 readword = READ32((R15 & ~2) + 4 + ((op & THUMB_INSN_IMM) << 2));
	SetRegister((op & THUMB_INSN_IMM_RD) >> THUMB_INSN_IMM_RD_SHIFT, readword);
	R15 += 2;
}

/* LDR* STR* group */

static void tg05_0(u32 pc, u32 op)  /* STR Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	WRITE32(addr, GetRegister(rd));
	R15 += 2;
}

static void tg05_1(u32 pc, u32 op)  /* STRH Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	WRITE16(addr, GetRegister(rd));
	R15 += 2;
}

static void tg05_2(u32 pc, u32 op)  /* STRB Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	WRITE8(addr, GetRegister(rd));
	R15 += 2;
}

static void tg05_3(u32 pc, u32 op)  /* LDRSB Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	u32 op2 = READ8(addr);
	SetRegister(rd, sext(op2, 8));
	R15 += 2;
}

static void tg05_4(u32 pc, u32 op)  /* LDR Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	u32 op2 = READ32(addr);
	SetRegister(rd, op2);
	R15 += 2;
}

static void tg05_5(u32 pc, u32 op)  /* LDRH Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	u32 op2 = READ16(addr);
	SetRegister(rd, op2);
	R15 += 2;
}

static void tg05_6(u32 pc, u32 op)  /* LDRB Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	u32 op2 = READ8(addr);
	SetRegister(rd, op2);
	R15 += 2;
}

static void tg05_7(u32 pc, u32 op)  /* LDRSH Rd, [Rn, Rm] */
{
	u32 rm = (op & THUMB_GROUP5_RM) >> THUMB_GROUP5_RM_SHIFT;
	u32 rn = (op & THUMB_GROUP5_RN) >> THUMB_GROUP5_RN_SHIFT;
	u32 rd = (op & THUMB_GROUP5_RD) >> THUMB_GROUP5_RD_SHIFT;
	u32 addr = GetRegister(rn) + GetRegister(rm);
	s32 op2 = (s32)(s16)(u16)READ16(addr & ~1);
	if ((addr & 1) && arm_archRev < 5)
		op2 >>= 8;
	SetRegister(rd, op2);
	R15 += 2;
}

/* Word Store w/ Immediate Offset */

static void tg06_0(u32 pc, u32 op) /* Store */
{
	u32 rn = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = op & THUMB_ADDSUB_RD;
	s32 offs = ((op & THUMB_LSOP_OFFS) >> THUMB_LSOP_OFFS_SHIFT) << 2;
	WRITE32(GetRegister(rn) + offs, GetRegister(rd));
	R15 += 2;
}

static void tg06_1(u32 pc, u32 op) /* Load */
{
	u32 rn = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = op & THUMB_ADDSUB_RD;
	s32 offs = ((op & THUMB_LSOP_OFFS) >> THUMB_LSOP_OFFS_SHIFT) << 2;
	SetRegister(rd, READ32(GetRegister(rn) + offs)); // fix
	R15 += 2;
}

/* Byte Store w/ Immeidate Offset */

static void tg07_0(u32 pc, u32 op) /* Store */
{
	u32 rn = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = op & THUMB_ADDSUB_RD;
	s32 offs = (op & THUMB_LSOP_OFFS) >> THUMB_LSOP_OFFS_SHIFT;
	WRITE8(GetRegister(rn) + offs, GetRegister(rd));
	R15 += 2;
}

static void tg07_1(u32 pc, u32 op)  /* Load */
{
	u32 rn = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = op & THUMB_ADDSUB_RD;
	s32 offs = (op & THUMB_LSOP_OFFS) >> THUMB_LSOP_OFFS_SHIFT;
	SetRegister(rd, READ8(GetRegister(rn) + offs));
	R15 += 2;
}

/* Load/Store Halfword */

static void tg08_0(u32 pc, u32 op) /* Store */
{
	u32 imm = (op & THUMB_HALFOP_OFFS) >> THUMB_HALFOP_OFFS_SHIFT;
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	WRITE16(GetRegister(rs) + (imm << 1), GetRegister(rd));
	R15 += 2;
}

static void tg08_1(u32 pc, u32 op) /* Load */
{
	u32 imm = (op & THUMB_HALFOP_OFFS) >> THUMB_HALFOP_OFFS_SHIFT;
	u32 rs = (op & THUMB_ADDSUB_RS) >> THUMB_ADDSUB_RS_SHIFT;
	u32 rd = (op & THUMB_ADDSUB_RD) >> THUMB_ADDSUB_RD_SHIFT;
	SetRegister(rd, READ16(GetRegister(rs) + (imm << 1)));
	R15 += 2;
}

/* Stack-Relative Load/Store */

static void tg09_0(u32 pc, u32 op) /* Store */
{
	u32 rd = (op & THUMB_STACKOP_RD) >> THUMB_STACKOP_RD_SHIFT;
	s32 offs = (u8)(op & THUMB_INSN_IMM);
	WRITE32(GetRegister(13) + ((u32)offs << 2), GetRegister(rd));
	R15 += 2;
}

static void tg09_1(u32 pc, u32 op) /* Load */
{
	u32 rd = (op & THUMB_STACKOP_RD) >> THUMB_STACKOP_RD_SHIFT;
	s32 offs = (u8)(op & THUMB_INSN_IMM);
	u32 readword = READ32((GetRegister(13) + ((u32)offs << 2)) & ~3);
	SetRegister(rd, readword);
	R15 += 2;
}

/* Get relative address */

static void tg0a_0(u32 pc, u32 op)  /* ADD Rd, PC, #nn */
{
	u32 rd = (op & THUMB_RELADDR_RD) >> THUMB_RELADDR_RD_SHIFT;
	s32 offs = (u8)(op & THUMB_INSN_IMM) << 2;
	SetRegister(rd, ((R15 + 4) & ~2) + offs);
	R15 += 2;
}

static void tg0a_1(u32 pc, u32 op) /* ADD Rd, SP, #nn */
{
	u32 rd = (op & THUMB_RELADDR_RD) >> THUMB_RELADDR_RD_SHIFT;
	s32 offs = (u8)(op & THUMB_INSN_IMM) << 2;
	SetRegister(rd, GetRegister(13) + offs);
	R15 += 2;
}

/* Stack-Related Opcodes */

static void tg0b_0(u32 pc, u32 op) /* ADD SP, #imm */
{
	u32 addr = (op & THUMB_INSN_IMM);
	addr &= ~THUMB_INSN_IMM_S;
	SetRegister(13, GetRegister(13) + ((op & THUMB_INSN_IMM_S) ? -(addr << 2) : (addr << 2)));
	R15 += 2;
}

static void tg0b_1(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_2(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_3(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_4(u32 pc, u32 op) /* PUSH {Rlist} */
{
	for (s32 offs = 7; offs >= 0; offs--)
	{
		if (op & (1 << offs))
		{
			SetRegister(13, GetRegister(13) - 4);
			WRITE32(GetRegister(13), GetRegister(offs));
		}
	}
	R15 += 2;
}

static void tg0b_5(u32 pc, u32 op) /* PUSH {Rlist}{LR} */
{
	SetRegister(13, GetRegister(13) - 4);
	WRITE32(GetRegister(13), GetRegister(14));
	for (s32 offs = 7; offs >= 0; offs--)
	{
		if (op & (1 << offs))
		{
			SetRegister(13, GetRegister(13) - 4);
			WRITE32(GetRegister(13), GetRegister(offs));
		}
	}
	R15 += 2;
}

static void tg0b_6(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_7(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_8(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_9(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_a(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_b(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_c(u32 pc, u32 op) /* POP {Rlist} */
{
	for (s32 offs = 0; offs < 8; offs++)
	{
		if (op & (1 << offs))
		{
			SetRegister(offs, READ32(GetRegister(13) & ~3));
			SetRegister(13, GetRegister(13) + 4);
		}
	}
	R15 += 2;
}

static void tg0b_d(u32 pc, u32 op) /* POP {Rlist}{PC} */
{
	for (s32 offs = 0; offs < 8; offs++)
	{
		if (op & (1 << offs))
		{
			SetRegister(offs, READ32(GetRegister(13) & ~3));
			SetRegister(13, GetRegister(13) + 4);
		}
	}
	u32 addr = READ32(GetRegister(13) & ~3);
	if (arm_archRev < 5)
	{
		R15 = addr & ~1;
	}
	else
	{
		if (addr & 1)
		{
			addr &= ~1;
		}
		else
		{
			arm_set_cpsr(GET_CPSR & ~T_MASK);
			if (addr & 2)
			{
				addr += 2;
			}
		}

		R15 = addr;
	}
	SetRegister(13, GetRegister(13) + 4);
}

static void tg0b_e(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

static void tg0b_f(u32 pc, u32 op)
{
	//fatalerror("%08x: Gb Undefined Thumb instruction: %04x\n", pc, op);
}

/* Multiple Load/Store */

// "The address should normally be a word aligned quantity and non-word aligned addresses do not affect the instruction."
// "However, the bottom 2 bits of the address will appear on A[1:0] and might be interpreted by the memory system."

// Endrift says LDMIA/STMIA ignore the low 2 bits and GBA Test Suite assumes it.

static void tg0c_0(u32 pc, u32 op) /* Store */
{
	u32 rd = (op & THUMB_MULTLS_BASE) >> THUMB_MULTLS_BASE_SHIFT;
	u32 ld_st_address = GetRegister(rd);
	for (s32 offs = 0; offs < 8; offs++)
	{
		if (op & (1 << offs))
		{
			WRITE32(ld_st_address & ~3, GetRegister(offs));
			ld_st_address += 4;
		}
	}
	SetRegister(rd, ld_st_address);
	R15 += 2;
}

static void tg0c_1(u32 pc, u32 op) /* Load */
{
	u32 rd = (op & THUMB_MULTLS_BASE) >> THUMB_MULTLS_BASE_SHIFT;
	int rd_in_list = op & (1 << rd);
	u32 ld_st_address = GetRegister(rd);
	for (s32 offs = 0; offs < 8; offs++)
	{
		if (op & (1 << offs))
		{
			SetRegister(offs, READ32(ld_st_address & ~3));
			ld_st_address += 4;
		}
	}
	if (!rd_in_list)
	{
		SetRegister(rd, ld_st_address);
	}
	R15 += 2;
}

/* Conditional Branch */

static void tg0d_0(u32 pc, u32 op) // COND_EQ:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (Z_IS_SET(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}

}

static void tg0d_1(u32 pc, u32 op) // COND_NE:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (Z_IS_CLEAR(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_2(u32 pc, u32 op) // COND_CS:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (C_IS_SET(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_3(u32 pc, u32 op) // COND_CC:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (C_IS_CLEAR(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_4(u32 pc, u32 op) // COND_MI:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (N_IS_SET(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_5(u32 pc, u32 op) // COND_PL:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (N_IS_CLEAR(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_6(u32 pc, u32 op) // COND_VS:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (V_IS_SET(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_7(u32 pc, u32 op) // COND_VC:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (V_IS_CLEAR(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_8(u32 pc, u32 op) // COND_HI:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (C_IS_SET(GET_CPSR) && Z_IS_CLEAR(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_9(u32 pc, u32 op) // COND_LS:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (C_IS_CLEAR(GET_CPSR) || Z_IS_SET(GET_CPSR))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_a(u32 pc, u32 op) // COND_GE:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (!(GET_CPSR & N_MASK) == !(GET_CPSR & V_MASK))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_b(u32 pc, u32 op) // COND_LT:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (!(GET_CPSR & N_MASK) != !(GET_CPSR & V_MASK))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_c(u32 pc, u32 op) // COND_GT:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (Z_IS_CLEAR(GET_CPSR) && !(GET_CPSR & N_MASK) == !(GET_CPSR & V_MASK))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_d(u32 pc, u32 op) // COND_LE:
{
	s32 offs = (s8)(op & THUMB_INSN_IMM);
	if (Z_IS_SET(GET_CPSR) || !(GET_CPSR & N_MASK) != !(GET_CPSR & V_MASK))
	{
		R15 += 4 + (offs << 1);
	}
	else
	{
		R15 += 2;
	}
}

static void tg0d_e(u32 pc, u32 op) // COND_AL:
{
	//fatalerror("%08x: Undefined Thumb instruction: %04x (ARM9 reserved)\n", pc, op);
}

static void tg0d_f(u32 pc, u32 op) // COND_NV:   // SWI (this is sort of a "hole" in the opcode encoding)
{
	arm_pendingSwi = 1;
	arm_update_irq_state();
	arm_check_irq_state();
}

/* B #offs */

static void tg0e_0(u32 pc, u32 op)
{
	s32 offs = sext((op & THUMB_BRANCH_OFFS) << 1, 12);
	R15 += 4 + offs;
}

static void tg0e_1(u32 pc, u32 op)
{
	/* BLX (LO) */

	u32 addr = GetRegister(14);
	addr += (op & THUMB_BLOP_OFFS) << 1;
	addr &= 0xfffffffc;
	SetRegister(14, (R15 + 2) | 1);
	R15 = addr;
	arm_set_cpsr(GET_CPSR & ~T_MASK);
}


static void tg0f_0(u32 pc, u32 op)
{
	/* BL (HI) */

	u32 addr = sext((op & THUMB_BLOP_OFFS) << 12, 23);
	addr += R15 + 4;
	SetRegister(14, addr);
	R15 += 2;
}

static void tg0f_1(u32 pc, u32 op) /* BL */
{
	/* BL (LO) */

	u32 addr = GetRegister(14) & ~1;
	addr += (op & THUMB_BLOP_OFFS) << 1;
	SetRegister(14, (R15 + 2) | 1);
	R15 = addr;
	//R15 += 2;
}


// this is our master dispatch jump table for THUMB mode, representing [(INSN & 0xffc0) >> 6] bits of the 16-bit decoded instruction
const armthumb_ophandler thumb_handler[0x40*0x10] =
{
// #define THUMB_SHIFT_R       ((u16)0x0800)
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_0,     &tg00_0,     &tg00_0,     &tg00_0,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
	&tg00_1,     &tg00_1,     &tg00_1,     &tg00_1,
// #define THUMB_INSN_ADDSUB   ((u16)0x0800)   // #define THUMB_ADDSUB_TYPE   ((u16)0x0600)
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_0,     &tg01_0,     &tg01_0,     &tg01_0,
	&tg01_10,    &tg01_10,    &tg01_10,    &tg01_10,
	&tg01_10,    &tg01_10,    &tg01_10,    &tg01_10,
	&tg01_11,    &tg01_11,    &tg01_11,    &tg01_11,
	&tg01_11,    &tg01_11,    &tg01_11,    &tg01_11,
	&tg01_12,    &tg01_12,    &tg01_12,    &tg01_12,
	&tg01_12,    &tg01_12,    &tg01_12,    &tg01_12,
	&tg01_13,    &tg01_13,    &tg01_13,    &tg01_13,
	&tg01_13,    &tg01_13,    &tg01_13,    &tg01_13,
// #define THUMB_INSN_CMP      ((u16)0x0800)
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_0,     &tg02_0,     &tg02_0,     &tg02_0,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
	&tg02_1,     &tg02_1,     &tg02_1,     &tg02_1,
// #define THUMB_INSN_SUB      ((u16)0x0800)
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_0,     &tg03_0,     &tg03_0,     &tg03_0,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
	&tg03_1,     &tg03_1,     &tg03_1,     &tg03_1,
//#define THUMB_GROUP4_TYPE   ((u16)0x0c00)  //#define THUMB_ALUOP_TYPE    ((u16)0x03c0)  // #define THUMB_HIREG_OP      ((u16)0x0300)  // #define THUMB_HIREG_H       ((u16)0x00c0)
	&tg04_00_00, &tg04_00_01, &tg04_00_02, &tg04_00_03,
	&tg04_00_04, &tg04_00_05, &tg04_00_06, &tg04_00_07,
	&tg04_00_08, &tg04_00_09, &tg04_00_0a, &tg04_00_0b,
	&tg04_00_0c, &tg04_00_0d, &tg04_00_0e, &tg04_00_0f,
	&tg04_01_00, &tg04_01_01, &tg04_01_02, &tg04_01_03,
	&tg04_01_10, &tg04_01_11, &tg04_01_12, &tg04_01_13,
	&tg04_01_20, &tg04_01_21, &tg04_01_22, &tg04_01_23,
	&tg04_01_30, &tg04_01_31, &tg04_01_32, &tg04_01_33,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
	&tg04_0203,  &tg04_0203,  &tg04_0203,  &tg04_0203,
//#define THUMB_GROUP5_TYPE   ((u16)0x0e00)
	&tg05_0,     &tg05_0,     &tg05_0,     &tg05_0,
	&tg05_0,     &tg05_0,     &tg05_0,     &tg05_0,
	&tg05_1,     &tg05_1,     &tg05_1,     &tg05_1,
	&tg05_1,     &tg05_1,     &tg05_1,     &tg05_1,
	&tg05_2,     &tg05_2,     &tg05_2,     &tg05_2,
	&tg05_2,     &tg05_2,     &tg05_2,     &tg05_2,
	&tg05_3,     &tg05_3,     &tg05_3,     &tg05_3,
	&tg05_3,     &tg05_3,     &tg05_3,     &tg05_3,
	&tg05_4,     &tg05_4,     &tg05_4,     &tg05_4,
	&tg05_4,     &tg05_4,     &tg05_4,     &tg05_4,
	&tg05_5,     &tg05_5,     &tg05_5,     &tg05_5,
	&tg05_5,     &tg05_5,     &tg05_5,     &tg05_5,
	&tg05_6,     &tg05_6,     &tg05_6,     &tg05_6,
	&tg05_6,     &tg05_6,     &tg05_6,     &tg05_6,
	&tg05_7,     &tg05_7,     &tg05_7,     &tg05_7,
	&tg05_7,     &tg05_7,     &tg05_7,     &tg05_7,
//#define THUMB_LSOP_L        ((u16)0x0800)
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_0,     &tg06_0,     &tg06_0,     &tg06_0,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
	&tg06_1,     &tg06_1,     &tg06_1,     &tg06_1,
//#define THUMB_LSOP_L        ((u16)0x0800)
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_0,     &tg07_0,     &tg07_0,     &tg07_0,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
	&tg07_1,     &tg07_1,     &tg07_1,     &tg07_1,
// #define THUMB_HALFOP_L      ((u16)0x0800)
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_0,     &tg08_0,     &tg08_0,     &tg08_0,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
	&tg08_1,     &tg08_1,     &tg08_1,     &tg08_1,
// #define THUMB_STACKOP_L     ((u16)0x0800)
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_0,     &tg09_0,     &tg09_0,     &tg09_0,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
	&tg09_1,     &tg09_1,     &tg09_1,     &tg09_1,
// #define THUMB_RELADDR_SP    ((u16)0x0800)
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_0,     &tg0a_0,     &tg0a_0,     &tg0a_0,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
	&tg0a_1,     &tg0a_1,     &tg0a_1,     &tg0a_1,
// #define THUMB_STACKOP_TYPE  ((u16)0x0f00)
	&tg0b_0,     &tg0b_0,     &tg0b_0,     &tg0b_0,
	&tg0b_1,     &tg0b_1,     &tg0b_1,     &tg0b_1,
	&tg0b_2,     &tg0b_2,     &tg0b_2,     &tg0b_2,
	&tg0b_3,     &tg0b_3,     &tg0b_3,     &tg0b_3,
	&tg0b_4,     &tg0b_4,     &tg0b_4,     &tg0b_4,
	&tg0b_5,     &tg0b_5,     &tg0b_5,     &tg0b_5,
	&tg0b_6,     &tg0b_6,     &tg0b_6,     &tg0b_6,
	&tg0b_7,     &tg0b_7,     &tg0b_7,     &tg0b_7,
	&tg0b_8,     &tg0b_8,     &tg0b_8,     &tg0b_8,
	&tg0b_9,     &tg0b_9,     &tg0b_9,     &tg0b_9,
	&tg0b_a,     &tg0b_a,     &tg0b_a,     &tg0b_a,
	&tg0b_b,     &tg0b_b,     &tg0b_b,     &tg0b_b,
	&tg0b_c,     &tg0b_c,     &tg0b_c,     &tg0b_c,
	&tg0b_d,     &tg0b_d,     &tg0b_d,     &tg0b_d,
	&tg0b_e,     &tg0b_e,     &tg0b_e,     &tg0b_e,
	&tg0b_f,     &tg0b_f,     &tg0b_f,     &tg0b_f,
// #define THUMB_MULTLS        ((u16)0x0800)
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_0,     &tg0c_0,     &tg0c_0,     &tg0c_0,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
	&tg0c_1,     &tg0c_1,     &tg0c_1,     &tg0c_1,
// #define THUMB_COND_TYPE     ((u16)0x0f00)
	&tg0d_0,     &tg0d_0,     &tg0d_0,     &tg0d_0,
	&tg0d_1,     &tg0d_1,     &tg0d_1,     &tg0d_1,
	&tg0d_2,     &tg0d_2,     &tg0d_2,     &tg0d_2,
	&tg0d_3,     &tg0d_3,     &tg0d_3,     &tg0d_3,
	&tg0d_4,     &tg0d_4,     &tg0d_4,     &tg0d_4,
	&tg0d_5,     &tg0d_5,     &tg0d_5,     &tg0d_5,
	&tg0d_6,     &tg0d_6,     &tg0d_6,     &tg0d_6,
	&tg0d_7,     &tg0d_7,     &tg0d_7,     &tg0d_7,
	&tg0d_8,     &tg0d_8,     &tg0d_8,     &tg0d_8,
	&tg0d_9,     &tg0d_9,     &tg0d_9,     &tg0d_9,
	&tg0d_a,     &tg0d_a,     &tg0d_a,     &tg0d_a,
	&tg0d_b,     &tg0d_b,     &tg0d_b,     &tg0d_b,
	&tg0d_c,     &tg0d_c,     &tg0d_c,     &tg0d_c,
	&tg0d_d,     &tg0d_d,     &tg0d_d,     &tg0d_d,
	&tg0d_e,     &tg0d_e,     &tg0d_e,     &tg0d_e,
	&tg0d_f,     &tg0d_f,     &tg0d_f,     &tg0d_f,
// #define THUMB_BLOP_LO       ((u16)0x0800)
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_0,     &tg0e_0,     &tg0e_0,     &tg0e_0,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
	&tg0e_1,     &tg0e_1,     &tg0e_1,     &tg0e_1,
// #define THUMB_BLOP_LO       ((u16)0x0800)
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_0,     &tg0f_0,     &tg0f_0,     &tg0f_0,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
	&tg0f_1,     &tg0f_1,     &tg0f_1,     &tg0f_1,
};
