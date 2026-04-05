// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz


#include "mame_stuff.h"
#include "arm9.h"
#include "arm9core.h"

void arm_switch_mode(u32 cpsr_mode_val)
{
	u32 cspr = arm_reg[eCPSR] & ~MODE_FLAG;
	arm_set_cpsr(cspr | cpsr_mode_val);
}

static inline s64 saturate_qbit_overflow(s64 res)
{
	if (res > 2147483647)   // INT32_MAX
	{   // overflow high? saturate and set Q
		res = 2147483647;
		arm_set_cpsr(GET_CPSR | Q_MASK);
	}
	else if (res < (-2147483647-1)) // INT32_MIN
	{   // overflow low? saturate and set Q
		res = (-2147483647-1);
		arm_set_cpsr(GET_CPSR | Q_MASK);
	}

	return res;
}


/* Decodes an Op2-style shifted-register form.  If @carry@ is non-zero the
 * shifter carry output will manifest itself as @*carry == 0@ for carry clear
 * and @*carry != 0@ for carry set.

   SJE: Rules:
   IF RC = 256, Result = no shift.
   LSL   0   = Result = RM, Carry = Old Contents of CPSR C Bit
   LSL(0,31) = Result shifted, least significant bit is in carry out
   LSL  32   = Result of 0, Carry = Bit 0 of RM
   LSL >32   = Result of 0, Carry out 0
   LSR   0   = LSR 32 (see below)
   LSR  32   = Result of 0, Carry = Bit 31 of RM
   LSR >32   = Result of 0, Carry out 0
   ASR >=32  = ENTIRE Result = bit 31 of RM
   ROR  32   = Result = RM, Carry = Bit 31 of RM
   ROR >32   = Same result as ROR n-32 until amount in range of 1-32 then follow rules
*/

static u32 decodeShift(u32 insn, u32 *pCarry)
{
	u32 k  = (insn & INSN_OP2_SHIFT) >> INSN_OP2_SHIFT_SHIFT;  // Bits 11-7
	u32 rm = GetRegister(insn & INSN_OP2_RM);
	u32 t  = (insn & INSN_OP2_SHIFT_TYPE) >> INSN_OP2_SHIFT_TYPE_SHIFT;

	if ((insn & INSN_OP2_RM) == 0xf) {
		// "If a register is used to specify the shift amount the PC will be 12 bytes ahead." (instead of 8)
		rm += t & 1 ? 12 : 8;
	}

	/* All shift types ending in 1 are Rk, not #k */
	if (t & 1)
	{
		// Keep only the bottom 8 bits for a Register Shift
		k = GetRegister(k >> 1) & 0xff;

		if (k == 0) /* Register shift by 0 is a no-op */
		{
			if (pCarry)
				*pCarry = GET_CPSR & C_MASK;
			return rm;
		}
	}
	/* Decode the shift type and perform the shift */
	switch (t >> 1)
	{
	case 0:                     /* LSL */
		// LSL  32   = Result of 0, Carry = Bit 0 of RM
		// LSL >32   = Result of 0, Carry out 0
		if (k >= 32)
		{
			if (pCarry)
				*pCarry = (k == 32) ? rm & 1 : 0;
			return 0;
		}
		else
		{
			if (pCarry)
			{
			// LSL      0   = Result = RM, Carry = Old Contents of CPSR C Bit
			// LSL (0,31)   = Result shifted, least significant bit is in carry out
			*pCarry = k ? (rm & (1 << (32 - k))) : (GET_CPSR & C_MASK);
			}
			return k ? (rm << k) : rm;
		}

	case 1:                         /* LSR */
		if (k == 0 || k == 32)
		{
			if (pCarry)
				*pCarry = rm & SIGN_BIT;
			return 0;
		}
		else if (k > 32)
		{
			if (pCarry)
				*pCarry = 0;
			return 0;
		}
		else
		{
			if (pCarry)
				*pCarry = (rm & (1 << (k - 1)));
			return rm >> k;
		}

	case 2:                     /* ASR */
		if (k == 0 || k > 32)
			k = 32;

		if (pCarry)
			*pCarry = (rm & (1 << (k - 1)));
		if (k >= 32)
			return rm & SIGN_BIT ? 0xffffffffu : 0;
		else
		{
			if (rm & SIGN_BIT)
				return (rm >> k) | (0xffffffffu << (32 - k));
			else
				return rm >> k;
		}

	case 3:                     /* ROR and RRX */
		if (k)
		{
			k &= 31;
			if (k)
			{
				if (pCarry)
					*pCarry = rm & (1 << (k - 1));
				return rotr_32(rm, k);
			}
			else
			{
				if (pCarry)
					*pCarry = rm & SIGN_BIT;
				return rm;
			}
		}
		else
		{
			/* RRX */
			if (pCarry)
				*pCarry = (rm & 1);
			return (rm >> 1) | ((GET_CPSR & C_MASK) << 2);
		}
	}

	//LOGMASKED(LOG_OPS, "%08x: Decodeshift error\n", R15);
	return 0;
} /* decodeShift */


static int loadInc(u32 pat, u32 rbv, u32 s, int mode)
{
	int i, result;
	u32 data;

	result = 0;
	rbv &= ~3;
	for (i = 0; i < 16; i++)
	{
		if ((pat >> i) & 1)
		{
			data = READ32(rbv += 4);
			if (arm_pendingAbtD) // "Overwriting of registers stops when the abort happens."
				return result;
			if (i == 15)
			{
				if (s) /* Pull full contents from stack */
					SetModeRegister(mode, 15, data);
				else if (MODE32) /* Pull only address, preserve mode & status flags */
					SetModeRegister(mode, 15, data);
				else
				{
					SetModeRegister(mode, 15, (GetModeRegister(mode, 15) & ~0x03FFFFFC) | (data & 0x03FFFFFC));
				}
			}
			else
			{
				SetModeRegister(mode, i, data);
			}
			result++;
		}
	}
	return result;
}


static int loadDec(u32 pat, u32 rbv, u32 s, int mode)
{
	int i, result;
	u32 data;

	result = 0;
	rbv &= ~3;
	for (i = 15; i >= 0; i--)
	{
		if ((pat >> i) & 1)
		{
			data = READ32(rbv -= 4);
			if (arm_pendingAbtD) // "Overwriting of registers stops when the abort happens."
				return result;
			if (i == 15)
			{
				if (s) /* Pull full contents from stack */
					SetModeRegister(mode, 15, data);
				else if (MODE32) /* Pull only address, preserve mode & status flags */
					SetModeRegister(mode, 15, data);
				else
				{
					SetModeRegister(mode, 15, (GetModeRegister(mode, 15) & ~0x03FFFFFC) | (data & 0x03FFFFFC));
				}
			}
			else
			{
				SetModeRegister(mode, i, data);
			}
			result++;
		}
	}
	return result;
}


static int storeInc(u32 pat, u32 rbv, int mode)
{
	int i, result;

	result = 0;
	for (i = 0; i < 16; i++)
	{
		if ((pat >> i) & 1)
		{
			WRITE32(rbv += 4, GetModeRegister(mode, i));
			if (arm_pendingAbtD)
				return result;
			result++;
		}
	}
	return result;
} /* storeInc */


static int storeDec(u32 pat, u32 rbv, int mode)
{
	// pre-count the # of registers being stored
	int const result = population_count_32(pat & 0x0000ffff);
	int actual_result = 0;

	// adjust starting address
	rbv -= (result << 2);

	for (int i = 0; i <= 15; i++)
	{
		if ((pat >> i) & 1)
		{
			WRITE32(rbv, GetModeRegister(mode, i));
			if (arm_pendingAbtD)
				return actual_result;
			rbv += 4;
			actual_result++;
		}
	}
	return result;
} /* storeDec */


/***************************************************************************
 *                            OPCODE HANDLING
 ***************************************************************************/

// Co-Processor Data Operation
static void HandleCoProcDO(u32 insn)
{
	// This instruction simply instructs the co-processor to do something, no data is returned to ARM7 core
	arm_do_callback(0);    // simply pass entire opcode to callback - since data format is actually dependent on co-proc implementation
}

// Co-Processor Register Transfer - To/From Arm to Co-Proc
static void HandleCoProcRT(u32 insn)
{
	/* xxxx 1110 oooL nnnn dddd cccc ppp1 mmmm */

	// Load (MRC) data from Co-Proc to ARM7 register
	if (insn & 0x00100000)       // Bit 20 = Load or Store
	{
		u32 res = arm946es_rt_r_callback(insn);   // RT Read handler must parse opcode & return appropriate result
		if (!arm_pendingUnd)
		{
			SetRegister((insn >> 12) & 0xf, res);
		}
	}
	// Store (MCR) data from ARM7 to Co-Proc register
	else
	{
		arm946es_rt_w_callback(insn, GetRegister((insn >> 12) & 0xf));
	}
}

/* Data Transfer - To/From Arm to Co-Proc
   Loading or Storing, the co-proc function is responsible to read/write from the base register supplied + offset
   8 bit immediate value Base Offset address is << 2 to get the actual #

  issues - #1 - the co-proc function, needs direct access to memory reads or writes (ie, so we must send a pointer to a func)
         - #2 - the co-proc may adjust the base address (especially if it reads more than 1 word), so a pointer to the register must be used
                but the old value of the register must be restored if write back is not set..
         - #3 - when post incrementing is used, it's up to the co-proc func. to add the offset, since the transfer
                address supplied in that case, is simply the base. I suppose this is irrelevant if write back not set
                but if co-proc reads multiple address, it must handle the offset adjustment itself.
*/
// todo: test with valid instructions
static void HandleCoProcDT(u32 insn)
{
	u32 rn = (insn >> 16) & 0xf;
	u32 rnv = GetRegister(rn);		// Get Address Value stored from Rn
	u32 ornv = rnv;               // Keep value of Rn
	u32 off = (insn & 0xff) << 2;	// Offset is << 2 according to manual
	u32 *prn = &arm_reg[rn];      // Pointer to our register, so it can be changed in the callback

	// Pre-Increment base address (IF POST INCREMENT - CALL BACK FUNCTION MUST DO IT)
	if ((insn & 0x1000000) && off)
	{
		// Up - Down bit
		if (insn & 0x800000)
			rnv += off;
		else
			rnv -= off;
	}

	// Load (LDC) data from ARM7 memory to Co-Proc memory
	if (insn & 0x00100000)
	{
		arm_dt_r_callback(insn, prn);
	}
	// Store (STC) data from Co-Proc to ARM7 memory
	else
	{
		arm_dt_w_callback(insn, prn);
	}

	if (arm_pendingUnd != 0) return;

	// If writeback not used - ensure the original value of RN is restored in case co-proc callback changed value
	if ((insn & 0x200000) == 0)
		SetRegister(rn, ornv);
}

static void HandleBranch(u32 insn, bool h_bit)
{
	u32 off = (insn & INSN_BRANCH) << 2;
	if (h_bit)
	{
		// H goes to bit1
		off |= (insn & 0x01000000) >> 23;
	}

	/* Save PC into LR if this is a branch with link or a BLX */
	if ((insn & INSN_BL) || ((arm_archRev >= 5) && ((insn & 0xfe000000) == 0xfa000000)))
	{
		SetRegister(14, R15 + 4);
	}

	/* Sign-extend the 24-bit offset in our calculations */
	if (off & 0x2000000u)
	{
		if (MODE32)
			R15 -= ((~(off | 0xfc000000u)) + 1) - 8;
		else
			R15 = ((R15 - (((~(off | 0xfc000000u)) + 1) - 8)) & 0x03FFFFFC) | (R15 & ~0x03FFFFFC);
	}
	else
	{
		if (MODE32)
			R15 += off + 8;
		else
			R15 = ((R15 + (off + 8)) & 0x03FFFFFC) | (R15 & ~0x03FFFFFC);
	}
}

static void HandleMemSingle(u32 insn)
{
	u32 rn, rnv, off, rd, rnv_old = 0;

	/* Fetch the offset */
	if (insn & INSN_I)
	{
		/* Register Shift */
		off = decodeShift(insn, nullptr);
	}
	else
	{
		/* Immediate Value */
		off = insn & INSN_SDT_IMM;
	}

	/* Calculate Rn, accounting for PC */
	rn = (insn & INSN_RN) >> INSN_RN_SHIFT;

	if (insn & INSN_SDT_P)
	{
		/* Pre-indexed addressing */
		if (insn & INSN_SDT_U)
		{
			if ((MODE32) || (rn != eR15))
				rnv = (GetRegister(rn) + off);
			else
				rnv = (GET_PC + off);
		}
		else
		{
			if ((MODE32) || (rn != eR15))
				rnv = (GetRegister(rn) - off);
			else
				rnv = (GET_PC - off);
		}

		if (insn & INSN_SDT_W)
		{
			rnv_old = GetRegister(rn);
			SetRegister(rn, rnv);

	// check writeback???
		}
		else if (rn == eR15)
		{
			rnv = rnv + 8;
		}
	}
	else
	{
		/* Post-indexed addressing */
		if (rn == eR15)
		{
			if (MODE32)
				rnv = R15 + 8;
			else
				rnv = GET_PC + 8;
		}
		else
		{
			rnv = GetRegister(rn);
		}
	}

	/* Do the transfer */
	rd = (insn & INSN_RD) >> INSN_RD_SHIFT;
	if (insn & INSN_SDT_L)
	{
		/* Load */
		if (insn & INSN_SDT_B)
		{
			u32 data = READ8(rnv);
			if (!arm_pendingAbtD)
			{
				SetRegister(rd, data);
			}
		}
		else
		{
			u32 data = READ32(rnv);
			if (!arm_pendingAbtD)
			{
				if (rd == eR15)
				{
					if (MODE32)
						R15 = data - 4;
					else
						R15 = (R15 & ~0x03FFFFFC) /* N Z C V I F M1 M0 */ | ((data - 4) & 0x03FFFFFC);
					// LDR, PC takes 2S + 2N + 1I (5 total cycles)
					arm_icount -= 2;
					if ((data & 1) && arm_archRev >= 5)
					{
						arm_set_cpsr(GET_CPSR | T_MASK);
						R15--;
					}
				}
				else
				{
					SetRegister(rd, data);
				}
			}
		}
	}
	else
	{
		/* Store */
		if (insn & INSN_SDT_B)
		{
			WRITE8(rnv, (u8) GetRegister(rd) & 0xffu);
		}
		else
		{
			//WRITE32(rnv, rd == eR15 ? R15 + 8 : GetRegister(rd));
			WRITE32(rnv, rd == eR15 ? R15 + 8 + 4 : GetRegister(rd)); // manual says STR rd = PC, +12
		}
		// Store takes only 2 N Cycles, so add + 1
		arm_icount += 1;
	}

	if (arm_pendingAbtD)
	{
		if ((insn & INSN_SDT_P) && (insn & INSN_SDT_W))
		{
			SetRegister(rn, rnv_old);
		}
	}
	else
	{
	/* Do post-indexing writeback */
	if (!(insn & INSN_SDT_P)/* && (insn & INSN_SDT_W)*/)
	{
		if (insn & INSN_SDT_U)
		{
			/* Writeback is applied in pipeline, before value is read from mem,
			    so writeback is effectively ignored */
			if (rd == rn) {
				SetRegister(rn, GetRegister(rd));
				// todo: check for offs... ?
			}
			else {
				//if ((insn & INSN_SDT_W) != 0)
				//	LOGMASKED(LOG_OPS, "%08x:  RegisterWritebackIncrement %d %d %d\n", R15, (insn & INSN_SDT_P) != 0, (insn & INSN_SDT_W) != 0, (insn & INSN_SDT_U) != 0);

				SetRegister(rn, (rnv + off));
			}
		}
		else
		{
			/* Writeback is applied in pipeline, before value is read from mem,
			    so writeback is effectively ignored */
			if (rd == rn) {
				SetRegister(rn, GetRegister(rd));
			}
			else {
				SetRegister(rn, (rnv - off));

				//if ((insn & INSN_SDT_W) != 0)
				//	LOGMASKED(LOG_OPS, "%08x:  RegisterWritebackDecrement %d %d %d\n", R15, (insn & INSN_SDT_P) != 0, (insn & INSN_SDT_W) != 0, (insn & INSN_SDT_U) != 0);
			}
		}
	}
}

//  arm_check_irq_state();

} /* HandleMemSingle */

static void HandleHalfWordDT(u32 insn)
{
	u32 rn, rnv, off, rd, rnv_old = 0;

	// Immediate or Register Offset?
	if (insn & 0x400000) {               // Bit 22 - 1 = immediate, 0 = register
		// imm. value in high nibble (bits 8-11) and lo nibble (bit 0-3)
		off = (((insn >> 8) & 0x0f) << 4) | (insn & 0x0f);
	}
	else {
		// register
		off = GetRegister(insn & 0x0f);
	}

	/* Calculate Rn, accounting for PC */
	rn = (insn & INSN_RN) >> INSN_RN_SHIFT;

	if (insn & INSN_SDT_P)
	{
		/* Pre-indexed addressing */
		if (insn & INSN_SDT_U)
		{
			rnv = (GetRegister(rn) + off);
		}
		else
		{
			rnv = (GetRegister(rn) - off);
		}

		if (insn & INSN_SDT_W)
		{
			rnv_old = GetRegister(rn);
			SetRegister(rn, rnv);

		// check writeback???
		}
		else if (rn == eR15)
		{
			rnv = (rnv) + 8;
		}
	}
	else
	{
		/* Post-indexed addressing */
		if (rn == eR15)
		{
			rnv = R15 + 8;
		}
		else
		{
			rnv = GetRegister(rn);
		}
	}

	/* Do the transfer */
	rd = (insn & INSN_RD) >> INSN_RD_SHIFT;

	/* Load */
	if (insn & INSN_SDT_L)
	{
		// Signed?
		if (insn & 0x40)
		{
			u32 newval;

			// Signed Half Word?
			if (insn & 0x20) {
				s32 data = (s32)(s16)(u16)READ16(rnv & ~1);
				if ((rnv & 1) && arm_archRev < 5)
					data >>= 8;
				newval = (u32)data;
			}
			// Signed Byte
			else {
				u8 databyte;
				u32 signbyte;
				databyte = READ8(rnv) & 0xff;
				signbyte = (databyte & 0x80) ? 0xffffff : 0;
				newval = (u32)(signbyte << 8)|databyte;
			}

			if (!arm_pendingAbtD)
			{
			// PC?
			if (rd == eR15)
			{
				R15 = newval + 8;
				// LDR(H,SH,SB) PC takes 2S + 2N + 1I (5 total cycles)
				arm_icount -= 2;

			}
			else
			{
				SetRegister(rd, newval);
				R15 += 4;
			}

			}
			else
			{
				R15 += 4;
			}

		}
		// Unsigned Half Word
		else
		{
			u32 newval = READ16(rnv);

			if (!arm_pendingAbtD)
			{
				if (rd == eR15)
				{
					R15 = newval + 8;
					// extra cycles for LDR(H,SH,SB) PC (5 total cycles)
					arm_icount -= 2;
				}
				else
				{
					SetRegister(rd, newval);
					R15 += 4;
				}
			}
			else
			{
				R15 += 4;
			}

		}


	}
	/* Store or ARMv5+ dword insns */
	else
	{
		if ((insn & 0x60) == 0x40)  // LDRD
		{
			SetRegister(rd, READ32(rnv));
			SetRegister(rd+1, READ32(rnv+4));
				R15 += 4;
		}
		else if ((insn & 0x60) == 0x60) // STRD
		{
			WRITE32(rnv, GetRegister(rd));
			WRITE32(rnv+4, GetRegister(rd+1));
			R15 += 4;
		}
		else
		{
			// WRITE16(rnv, rd == eR15 ? R15 + 8 : GetRegister(rd));
			WRITE16(rnv, rd == eR15 ? R15 + 8 + 4 : GetRegister(rd)); // manual says STR RD=PC, +12 of address

// if R15 is not increased then e.g. "STRH R10, [R15,#$10]" will be executed over and over again
#if 0
			if (rn != eR15)
#endif
			R15 += 4;

			// STRH takes 2 cycles, so we add + 1
			arm_icount += 1;
		}
	}

	if (arm_pendingAbtD)
	{
		if ((insn & INSN_SDT_P) && (insn & INSN_SDT_W))
		{
			SetRegister(rn, rnv_old);
		}
	}
	else
	{
	// SJE: No idea if this writeback code works or makes sense here..

	/* Do post-indexing writeback */
	if (!(insn & INSN_SDT_P)/* && (insn & INSN_SDT_W)*/)
	{
		if (insn & INSN_SDT_U)
		{
			/* Writeback is applied in pipeline, before value is read from mem,
			    so writeback is effectively ignored */
			if (rd == rn) {
				SetRegister(rn, GetRegister(rd));
				// todo: check for offs... ?
			}
			else {
				//if ((insn & INSN_SDT_W) != 0)
				//	LOGMASKED(LOG_OPS, "%08x:  RegisterWritebackIncrement %d %d %d\n", R15, (insn & INSN_SDT_P) != 0, (insn & INSN_SDT_W) != 0, (insn & INSN_SDT_U) != 0);

				SetRegister(rn, (rnv + off));
			}
		}
		else
		{
			/* Writeback is applied in pipeline, before value is read from mem,
			    so writeback is effectively ignored */
			if (rd == rn) {
				SetRegister(rn, GetRegister(rd));
			}
			else {
				SetRegister(rn, (rnv - off));

				//if ((insn & INSN_SDT_W) != 0)
				//	LOGMASKED(LOG_OPS, "%08x:  RegisterWritebackDecrement %d %d %d\n", R15, (insn & INSN_SDT_P) != 0, (insn & INSN_SDT_W) != 0, (insn & INSN_SDT_U) != 0);
			}
		}
	}

	}

}

static void HandleSwap(u32 insn)
{
	u32 rn, rm, rd, tmp;

	rn = GetRegister((insn >> 16) & 0xf);  // reg. w/read address
	rm = GetRegister(insn & 0xf);          // reg. w/write address
	rd = (insn >> 12) & 0xf;                // dest reg

	// can be byte or word
	if (insn & 0x400000)
	{
		tmp = READ8(rn);
		if (!arm_pendingAbtD)
		{
			WRITE8(rn, rm);
			SetRegister(rd, tmp);
		}
	}
	else
	{
		tmp = READ32(rn);
		if (!arm_pendingAbtD)
		{
			WRITE32(rn, rm);
			SetRegister(rd, tmp);
		}
	}

	R15 += 4;
	// Instruction takes 1S+2N+1I cycles - so we subtract one more..
	arm_icount -= 1;
}

static void HandlePSRTransfer(u32 insn)
{
	int reg = (insn & 0x400000) ? SPSR : eCPSR; // Either CPSR or SPSR
	u32 newval, val;
	int oldmode = GET_CPSR & MODE_FLAG;

	// get old value of CPSR/SPSR
	newval = GetRegister(reg);

	// MSR (bit 21 set) - Copy value to CPSR/SPSR
	if ((insn & 0x00200000))
	{
		// Immediate Value?
		if (insn & INSN_I) {
			// Value can be specified for a Right Rotate, 2x the value specified.
			int by = (insn & INSN_OP2_ROTATE) >> INSN_OP2_ROTATE_SHIFT;
			if (by)
				val = rotr_32(insn & INSN_OP2_IMM, by << 1);
			else
				val = insn & INSN_OP2_IMM;
		}
		// Value from Register
		else
		{
			val = GetRegister(insn & 0x0f);
		}

		// apply field code bits
		if (reg == eCPSR)
		{
			if (oldmode != eARM7_MODE_USER)
			{
				if (insn & 0x00010000)
				{
					newval = (newval & 0xffffff00) | (val & 0x000000ff);
				}
				if (insn & 0x00020000)
				{
					newval = (newval & 0xffff00ff) | (val & 0x0000ff00);
				}
				if (insn & 0x00040000)
				{
					newval = (newval & 0xff00ffff) | (val & 0x00ff0000);
				}
			}

			// status flags can be modified regardless of mode
			if (insn & 0x00080000)
			{
				// TODO for non ARMv5E mask should be 0xf0000000 (ie mask Q bit)
				newval = (newval & 0x00ffffff) | (val & 0xf8000000);
			}
		}
		else    // SPSR has stricter requirements
		{
			if (((GET_CPSR & 0x1f) > 0x10) && ((GET_CPSR & 0x1f) < 0x1f))
			{
				if (insn & 0x00010000)
				{
					newval = (newval & 0xffffff00) | (val & 0xff);
				}
				if (insn & 0x00020000)
				{
					newval = (newval & 0xffff00ff) | (val & 0xff00);
				}
				if (insn & 0x00040000)
				{
					newval = (newval & 0xff00ffff) | (val & 0xff0000);
				}
				if (insn & 0x00080000)
				{
					// TODO for non ARMv5E mask should be 0xf0000000 (ie mask Q bit)
					newval = (newval & 0x00ffffff) | (val & 0xf8000000);
				}
			}
		}

#if 0
		// force valid mode
		newval |= 0x10;
#endif

		// Update the Register
		if (reg == eCPSR)
		{
			arm_set_cpsr(newval);
		}
		else
			SetRegister(reg, newval);

		// Switch to new mode if changed
		if ((newval & MODE_FLAG) != oldmode)
			arm_switch_mode(GET_MODE);

	}
	// MRS (bit 21 clear) - Copy CPSR or SPSR to specified Register
	else
	{
		SetRegister((insn >> 12)& 0x0f, GetRegister(reg));
	}
}

static void HandleALU(u32 insn)
{
	u32 op2, sc = 0, rd, rn, opcode;
	u32 by, rdn;

	// Normal Data Processing : 1S
	// Data Processing with register specified shift : 1S + 1I
	// Data Processing with PC written : 2S + 1N
	// Data Processing with register specified shift and PC written : 2S + 1N + 1I

	opcode = (insn & INSN_OPCODE) >> INSN_OPCODE_SHIFT;

	rd = 0;
	rn = 0;

	/* --------------*/
	/* Construct Op2 */
	/* --------------*/

	/* Immediate constant */
	if (insn & INSN_I)
	{
		by = (insn & INSN_OP2_ROTATE) >> INSN_OP2_ROTATE_SHIFT;
		if (by)
		{
			op2 = rotr_32(insn & INSN_OP2_IMM, by << 1);
			sc = op2 & SIGN_BIT;
		}
		else
		{
			op2 = insn & INSN_OP2;      // SJE: Shouldn't this be INSN_OP2_IMM?
			sc = GET_CPSR & C_MASK;
		}
	}
	/* Op2 = Register Value */
	else
	{
		op2 = decodeShift(insn, (insn & INSN_S) ? &sc : nullptr);

		// LD TODO sc will always be 0 if this applies
		if (!(insn & INSN_S))
			sc = 0;

		// extra cycle (register specified shift)
		arm_icount -= 1;
	}

	// LD TODO this comment is wrong
	/* Calculate Rn to account for pipelining */
	if ((opcode & 0xd) != 0xd) /* No Rn in MOV */
	{
		if ((rn = (insn & INSN_RN) >> INSN_RN_SHIFT) == eR15)
		{
			if (MODE32)
				rn = R15 + 8;
			else
				rn = GET_PC + 8;
		}
		else
		{
			rn = GetRegister(rn);
		}
	}

	/* Perform the operation */

	switch (opcode)
	{
	/* Arithmetic operations */
	case OPCODE_SBC:
		rd = (rn - op2 - (GET_CPSR & C_MASK ? 0 : 1));
		HandleALUSubFlags(rd, rn, op2);
		break;
	case OPCODE_CMP:
	case OPCODE_SUB:
		rd = (rn - op2);
		HandleALUSubFlags(rd, rn, op2);
		break;
	case OPCODE_RSC:
		rd = (op2 - rn - (GET_CPSR & C_MASK ? 0 : 1));
		HandleALUSubFlags(rd, op2, rn);
		break;
	case OPCODE_RSB:
		rd = (op2 - rn);
		HandleALUSubFlags(rd, op2, rn);
		break;
	case OPCODE_ADC:
		rd = (rn + op2 + ((GET_CPSR & C_MASK) >> C_BIT));
		HandleALUAddFlags(rd, rn, op2);
		break;
	case OPCODE_CMN:
	case OPCODE_ADD:
		rd = (rn + op2);
		HandleALUAddFlags(rd, rn, op2);
		break;

	/* Logical operations */
	case OPCODE_AND:
	case OPCODE_TST:
		rd = rn & op2;
		HandleALULogicalFlags(rd, sc);
		break;
	case OPCODE_BIC:
		rd = rn & ~op2;
		HandleALULogicalFlags(rd, sc);
		break;
	case OPCODE_TEQ:
	case OPCODE_EOR:
		rd = rn ^ op2;
		HandleALULogicalFlags(rd, sc);
		break;
	case OPCODE_ORR:
		rd = rn | op2;
		HandleALULogicalFlags(rd, sc);
		break;
	case OPCODE_MOV:
		rd = op2;
		HandleALULogicalFlags(rd, sc);
		break;
	case OPCODE_MVN:
		rd = (~op2);
		HandleALULogicalFlags(rd, sc);
		break;
	}

	/* Put the result in its register if not one of the test only opcodes (TST,TEQ,CMP,CMN) */
	rdn = (insn & INSN_RD) >> INSN_RD_SHIFT;
	if ((opcode & 0xc) != 0x8)
	{
		// If Rd = R15, but S Flag not set, Result is placed in R15, but CPSR is not affected (page 44)
		if (rdn == eR15 && !(insn & INSN_S))
		{
			if (MODE32)
			{
				R15 = rd;
			}
			else
			{
				R15 = (R15 & ~0x03FFFFFC) | (rd & 0x03FFFFFC);
			}
			// extra cycles (PC written)
			arm_icount -= 2;
		}
		else
		{
			// Rd = 15 and S Flag IS set, Result is placed in R15, and current mode SPSR moved to CPSR
			if (rdn == eR15) {
				if (MODE32)
				{
				// When Rd is R15 and the S flag is set the result of the operation is placed in R15 and the SPSR corresponding to
				// the current mode is moved to the CPSR. This allows state changes which automatically restore both PC and
				// CPSR. --> This form of instruction should not be used in User mode. <--

				if (GET_MODE != eARM7_MODE_USER)
				{
					// Update CPSR from SPSR
					arm_set_cpsr(GetRegister(SPSR));
					arm_switch_mode(GET_MODE);
				}

				R15 = rd;

				}
				else
				{
					u32 temp;
					R15 = rd; //(R15 & 0x03FFFFFC) | (rd & 0xFC000003);
					temp = (GET_CPSR & 0x0FFFFF20) | (rd & 0xF0000000) /* N Z C V */ | ((rd & 0x0C000000) >> (26 - 6)) /* I F */ | (rd & 0x00000003) /* M1 M0 */;
					arm_set_cpsr( temp);
					arm_switch_mode( temp & 3);
				}

				// extra cycles (PC written)
				arm_icount -= 2;

				/* IRQ masks may have changed in this instruction */
//              arm_check_irq_state();
			}
			else
				/* S Flag is set - Write results to register & update CPSR (which was already handled using HandleALU flag macros) */
				SetRegister(rdn, rd);
		}
	}
	// SJE: Don't think this applies any more.. (see page 44 at bottom)
	/* TST & TEQ can affect R15 (the condition code register) with the S bit set */
	else if (rdn == eR15)
	{
		if (insn & INSN_S) {
			if (MODE32)
				R15 = rd;
			else
			{
				u32 temp;
				R15 = (R15 & 0x03FFFFFC) | (rd & ~0x03FFFFFC);
				temp = (GET_CPSR & 0x0FFFFF20) | (rd & 0xF0000000) /* N Z C V */ | ((rd & 0x0C000000) >> (26 - 6)) /* I F */ | (rd & 0x00000003) /* M1 M0 */;
				arm_set_cpsr( temp);
				arm_switch_mode( temp & 3);
			}

			/* IRQ masks may have changed in this instruction */
//          arm_check_irq_state();
		}
		else
		{
		}
		// extra cycles (PC written)
		arm_icount -= 2;
	}

	// compensate for the -3 at the end
	arm_icount += 2;
}

static void HandleMul(u32 insn)
{
	u32 r, rm, rs;

	// MUL takes 1S + mI and MLA 1S + (m+1)I cycles to execute, where S and I are as
	// defined in 6.2 Cycle Types on page 6-2.
	// m is the number of 8 bit multiplier array cycles required to complete the
	// multiply, which is controlled by the value of the multiplier operand
	// specified by Rs.

	rm = GetRegister(insn & INSN_MUL_RM);
	rs = GetRegister((insn & INSN_MUL_RS) >> INSN_MUL_RS_SHIFT);

	/* Do the basic multiply of Rm and Rs */
	r = rm * rs;

	/* Add on Rn if this is a MLA */
	if (insn & INSN_MUL_A)
	{
		r += GetRegister((insn & INSN_MUL_RN) >> INSN_MUL_RN_SHIFT);
		// extra cycle for MLA
		arm_icount -= 1;
	}

	/* Write the result */
	SetRegister((insn & INSN_MUL_RD) >> INSN_MUL_RD_SHIFT, r);

	/* Set N and Z if asked */
	if (insn & INSN_S)
	{
		arm_set_cpsr((GET_CPSR & ~(N_MASK | Z_MASK)) | HandleALUNZFlags(r));
	}

	if (rs & SIGN_BIT) rs = -rs;
	if (rs < 0x00000100) arm_icount -= 1 + 1;
	else if (rs < 0x00010000) arm_icount -= 1 + 2;
	else if (rs < 0x01000000) arm_icount -= 1 + 3;
	else arm_icount -= 1 + 4;

	arm_icount += 3;
}

// todo: add proper cycle counts
static void HandleSMulLong(u32 insn)
{
	// MULL takes 1S + (m+1)I and MLAL 1S + (m+2)I cycles to execute, where m is the
	// number of 8 bit multiplier array cycles required to complete the multiply, which is
	// controlled by the value of the multiplier operand specified by Rs.

	s32  rm  = (s32)GetRegister(insn & 0xf);
	s32  rs  = (s32)GetRegister(((insn >> 8) & 0xf));
	u32 rhi = (insn >> 16) & 0xf;
	u32 rlo = (insn >> 12) & 0xf;

	/* Perform the multiplication */
	s64 res = mul_32x32(rm, rs);

	/* Add on Rn if this is a MLA */
	if (insn & INSN_MUL_A)
	{
		s64 acum = (s64)((((s64)(GetRegister(rhi))) << 32) | GetRegister(rlo));
		res += acum;
		// extra cycle for MLA
		arm_icount -= 1;
	}

	/* Write the result (upper dword goes to RHi, lower to RLo) */
	SetRegister(rhi, res >> 32);
	SetRegister(rlo, res & 0xFFFFFFFF);

	/* Set N and Z if asked */
	if (insn & INSN_S)
	{
		arm_set_cpsr((GET_CPSR & ~(N_MASK | Z_MASK)) | HandleLongALUNZFlags(res));
	}

	if (rs < 0) rs = -rs;
	if (rs < 0x00000100) arm_icount -= 1 + 1 + 1;
	else if (rs < 0x00010000) arm_icount -= 1 + 2 + 1;
	else if (rs < 0x01000000) arm_icount -= 1 + 3 + 1;
	else arm_icount -= 1 + 4 + 1;

	arm_icount += 3;
}

// todo: add proper cycle counts
static void HandleUMulLong(u32 insn)
{
	// MULL takes 1S + (m+1)I and MLAL 1S + (m+2)I cycles to execute, where m is the
	// number of 8 bit multiplier array cycles required to complete the multiply, which is
	// controlled by the value of the multiplier operand specified by Rs.

	u32 rm  = GetRegister(insn & 0xf);
	u32 rs  = GetRegister(((insn >> 8) & 0xf));
	u32 rhi = (insn >> 16) & 0xf;
	u32 rlo = (insn >> 12) & 0xf;

	/* Perform the multiplication */
	u64 res = mulu_32x32(rm, rs);

	/* Add on Rn if this is a MLA */
	if (insn & INSN_MUL_A)
	{
		u64 acum = (u64)((((u64)(GetRegister(rhi))) << 32) | GetRegister(rlo));
		res += acum;
		// extra cycle for MLA
		arm_icount -= 1;
	}

	/* Write the result (upper dword goes to RHi, lower to RLo) */
	SetRegister(rhi, res >> 32);
	SetRegister(rlo, res & 0xFFFFFFFF);

	/* Set N and Z if asked */
	if (insn & INSN_S)
	{
		arm_set_cpsr((GET_CPSR & ~(N_MASK | Z_MASK)) | HandleLongALUNZFlags(res));
	}

	if (rs < 0x00000100) arm_icount -= 1 + 1 + 1;
	else if (rs < 0x00010000) arm_icount -= 1 + 2 + 1;
	else if (rs < 0x01000000) arm_icount -= 1 + 3 + 1;
	else arm_icount -= 1 + 4 + 1;

	arm_icount += 3;
}

static void HandleMemBlock(u32 insn)
{
	u32 rb = (insn & INSN_RN) >> INSN_RN_SHIFT;
	u32 rbp = GetRegister(rb);
	int result;

	// Normal LDM instructions take nS + 1N + 1I and LDM PC takes (n+1)S + 2N + 1I
	// incremental cycles, where S,N and I are as defined in 6.2 Cycle Types on page 6-2.
	// STM instructions take (n-1)S + 2N incremental cycles to execute, where n is the
	// number of words transferred.

	if (insn & INSN_BDT_L)
	{
		/* Loading */
		if (insn & INSN_BDT_U)
		{
			/* Incrementing */
			if (!(insn & INSN_BDT_P))
			{
				rbp = rbp + (- 4);
			}

			// S Flag Set, but R15 not in list = User Bank Transfer
			if (insn & INSN_BDT_S && (insn & 0x8000) == 0)
			{
				// !! actually switching to user mode triggers a section permission fault in Happy Fish 302-in-1 (BP C0030DF4, press F5 ~16 times) !!
				// set to user mode - then do the transfer, and set back
				//int curmode = GET_MODE;
				//SwitchMode(eARM7_MODE_USER);
				//LOGMASKED(LOG_OPS, "%08x: User Bank Transfer not fully tested - please check if working properly!\n", R15);
				result = loadInc(insn & 0xffff, rbp, insn & INSN_BDT_S, eARM7_MODE_USER);
				// todo - not sure if Writeback occurs on User registers also..
				//SwitchMode(curmode);
			}
			else
				result = loadInc(insn & 0xffff, rbp, insn & INSN_BDT_S, GET_MODE);

			if ((insn & INSN_BDT_W) && !arm_pendingAbtD)
			{
				// "A LDM will always overwrite the updated base if the base is in the list." (also for a user bank transfer?)
				// GBA "V-Rally 3" expects R0 not to be overwritten with the updated base value [BP 8077B0C]
				if (((insn >> rb) & 1) == 0)
				{
					SetRegister(rb, GetRegister(rb) + result * 4);
				}
			}

			// R15 included? (NOTE: CPSR restore must occur LAST otherwise wrong registers restored!)
			if ((insn & 0x8000) && !arm_pendingAbtD)
			{
				R15 -= 4;     // SJE: I forget why i did this?
				// S - Flag Set? Signals transfer of current mode SPSR->CPSR
				if (insn & INSN_BDT_S)
				{
					if (MODE32)
					{
						arm_set_cpsr(GetRegister(SPSR));
						arm_switch_mode(GET_MODE);
					}
					else
					{
						u32 temp;
						//LOGMASKED(LOG_OPS, "LDM + S | R15 %08X CPSR %08X\n", R15, GET_CPSR);
						temp = (GET_CPSR & 0x0FFFFF20) | (R15 & 0xF0000000) /* N Z C V */ | ((R15 & 0x0C000000) >> (26 - 6)) /* I F */ | (R15 & 0x00000003) /* M1 M0 */;
						arm_set_cpsr( temp);
						arm_switch_mode(temp & 3);
					}
				}
				else
					if ((R15 & 1) && arm_archRev >= 5)
					{
						arm_set_cpsr(GET_CPSR | T_MASK);
						R15--;
					}
				// LDM PC - takes 2 extra cycles
				arm_icount -= 2;
			}
		}
		else
		{
			/* Decrementing */
			if (!(insn & INSN_BDT_P))
			{
				rbp = rbp - (- 4);
			}

			// S Flag Set, but R15 not in list = User Bank Transfer
			if (insn & INSN_BDT_S && ((insn & 0x8000) == 0))
			{
				// set to user mode - then do the transfer, and set back
				//int curmode = GET_MODE;
				//SwitchMode(eARM7_MODE_USER);
				//LOGMASKED(LOG_OPS, "%08x: User Bank Transfer not fully tested - please check if working properly!\n", R15);
				result = loadDec(insn & 0xffff, rbp, insn & INSN_BDT_S, eARM7_MODE_USER);
				// todo - not sure if Writeback occurs on User registers also..
				//SwitchMode(curmode);
			}
			else
				result = loadDec(insn & 0xffff, rbp, insn & INSN_BDT_S, GET_MODE);

			if ((insn & INSN_BDT_W) && !arm_pendingAbtD)
			{
				//if (rb == 0xf)
				//	LOGMASKED(LOG_OPS, "%08x:  Illegal LDRM writeback to r15\n", R15);
				// "A LDM will always overwrite the updated base if the base is in the list." (also for a user bank transfer?)
				if (((insn >> rb) & 1) == 0)
				{
					SetRegister(rb, GetRegister(rb) - result * 4);
				}
			}

			// R15 included? (NOTE: CPSR restore must occur LAST otherwise wrong registers restored!)
			if ((insn & 0x8000) && !arm_pendingAbtD) {
				R15 -= 4;     // SJE: I forget why i did this?
				// S - Flag Set? Signals transfer of current mode SPSR->CPSR
				if (insn & INSN_BDT_S)
				{
					if (MODE32)
					{
						arm_set_cpsr(GetRegister(SPSR));
						arm_switch_mode(GET_MODE);
					}
					else
					{
						u32 temp;
						//LOGMASKED(LOG_OPS, "LDM + S | R15 %08X CPSR %08X\n", R15, GET_CPSR);
						temp = (GET_CPSR & 0x0FFFFF20) /* N Z C V I F M4 M3 M2 M1 M0 */ | (R15 & 0xF0000000) /* N Z C V */ | ((R15 & 0x0C000000) >> (26 - 6)) /* I F */ | (R15 & 0x00000003) /* M1 M0 */;
						arm_set_cpsr(temp);
						arm_switch_mode(temp & 3);
					}
				}
				else
					if ((R15 & 1) && arm_archRev >= 5)
					{
						arm_set_cpsr(GET_CPSR | T_MASK);
						R15--;
					}
				// LDM PC - takes 2 extra cycles
				arm_icount -= 2;
			}
		}
		// LDM (NO PC) takes (n)S + 1N + 1I cycles (n = # of register transfers)
		arm_icount -= result + 1 + 1;
	} /* Loading */
	else
	{
		/* Storing - STM */
		if (insn & (1 << eR15))
		{
			/* special case handling if writing to PC */
			R15 += 12;
		}
		if (insn & INSN_BDT_U)
		{
			/* Incrementing */
			if (!(insn & INSN_BDT_P))
			{
				rbp = rbp + (- 4);
			}

			// S Flag Set = User Bank Transfer
			if (insn & INSN_BDT_S)
			{
				// todo: needs to be tested..

				// set to user mode - then do the transfer, and set back
				//int curmode = GET_MODE;
				//SwitchMode(eARM7_MODE_USER);
				//LOGMASKED(LOG_OPS, "%08x: User Bank Transfer not fully tested - please check if working properly!\n", R15);
				result = storeInc(insn & 0xffff, rbp, eARM7_MODE_USER);
				// todo - not sure if Writeback occurs on User registers also..
				//SwitchMode(curmode);
			}
			else
				result = storeInc(insn & 0xffff, rbp, GET_MODE);

			if ((insn & INSN_BDT_W) && !arm_pendingAbtD)
			{
				SetRegister(rb, GetRegister(rb) + result * 4);
			}
		}
		else
		{
			/* Decrementing - but real CPU writes in incrementing order */
			if (!(insn & INSN_BDT_P))
			{
				rbp = rbp - (-4);
			}

			// S Flag Set = User Bank Transfer
			if (insn & INSN_BDT_S)
			{
				// set to user mode - then do the transfer, and set back
				//int curmode = GET_MODE;
				//SwitchMode(eARM7_MODE_USER);
				//LOGMASKED(LOG_OPS, "%08x: User Bank Transfer not fully tested - please check if working properly!\n", R15);
				result = storeDec(insn & 0xffff, rbp, eARM7_MODE_USER);
				// todo - not sure if Writeback occurs on User registers also..
				//SwitchMode(curmode);
			}
			else
				result = storeDec(insn & 0xffff, rbp, GET_MODE);

			if ((insn & INSN_BDT_W) && !arm_pendingAbtD)
			{
				SetRegister(rb, GetRegister(rb) - result * 4);
			}
		}
		if (insn & (1 << eR15))
			R15 -= 12;

		// STM takes (n-1)S + 2N cycles (n = # of register transfers)
		arm_icount -= (result - 1) + 2;
	}

	// We will specify the cycle count for each case, so remove the -3 that occurs at the end
	arm_icount += 3;

} /* HandleMemBlock */


static void arm9ops_undef(u32 insn)
{
	// unsupported instruction
	//LOGMASKED(LOG_OPS, "ARM7: Instruction %08X unsupported\n", insn);
}

static void arm9ops_1(u32 insn)
{
	/* Change processor state (CPS) */
	if ((insn & 0x00f10020) == 0x00000000)
	{
		// unsupported (armv6 onwards only)
		arm9ops_undef(insn);
		R15 += 4;
	}
	else if ((insn & 0x00ff00f0) == 0x00010000) /* set endianness (SETEND) */
	{
		// unsupported (armv6 onwards only)
		if (arm_archRev < 6) arm9ops_undef(insn);
		else
		{
			u32 new_cpsr = GET_CPSR & ~(1 << 9);
			arm_set_cpsr(new_cpsr | (insn & (1 << 9)));
		}
		R15 += 4;
	}
	else
	{
		arm9ops_undef(insn);
		R15 += 4;
	}
}

static void arm9ops_57(u32 insn)
{
	/* Cache Preload (PLD) */
	if ((insn & 0x0070f000) == 0x0050f000)
	{
		// unsupported (armv6 onwards only)
		if(arm_archRev < 6) arm9ops_undef(insn);
		R15 += 4;
	}
	else
	{
		arm9ops_undef(insn);
		R15 += 4;
	}
}

static void arm9ops_89(u32 insn)
{
	/* Save Return State (SRS) */
	if ((insn & 0x005f0f00) == 0x004d0500)
	{
		// unsupported (armv6 onwards only)
		arm9ops_undef(insn);
		R15 += 4;
	}
	else if ((insn & 0x00500f00) == 0x00100a00) /* Return From Exception (RFE) */
	{
		// unsupported (armv6 onwards only)
		arm9ops_undef(insn);
		R15 += 4;
	}
	else
	{
		arm9ops_undef(insn);
		R15 += 4;
	}
}

static void arm9ops_ab(u32 insn)
{
	// BLX
	HandleBranch(insn, true);
	arm_set_cpsr(GET_CPSR|T_MASK);
}

static void arm9ops_c(u32 insn)
{
	/* Additional coprocessor double register transfer */
	if ((insn & 0x00e00000) == 0x00400000)
	{
		// unsupported
		arm9ops_undef(insn);
		R15 += 4;
	}
	else
	{
		arm9ops_undef(insn);
		R15 += 4;
	}
}

static void arm9ops_e(u32 insn)
{
	/* Additional coprocessor register transfer */
	// unsupported
	arm9ops_undef(insn);
	R15 += 4;
}


static void arm7ops_0123(u32 insn)
{
//case 0:
//case 1:
//case 2:
//case 3:
	/* Branch and Exchange (BX) */
	if ((insn & 0x0ffffff0) == 0x012fff10)     // bits 27-4 == 000100101111111111110001
	{
		R15 = GetRegister(insn & 0x0f);
		// If new PC address has A0 set, switch to Thumb mode
		if (R15 & 1) {
			R15--;
			arm_set_cpsr(GET_CPSR|T_MASK);
		}
	}
	else if ((insn & 0x0ff000f0) == 0x01200030) // BLX Rn - v5
	{
		// save link address
		SetRegister(14, R15 + 4);

		R15 = GetRegister(insn & 0x0f);
		// If new PC address has A0 set, switch to Thumb mode
		if (R15 & 1) {
			R15--;
			arm_set_cpsr(GET_CPSR|T_MASK);
		}
	}
	else if ((insn & 0x0ff000f0) == 0x01600010) // CLZ - v5
	{
		u32 rm = insn&0xf;
		u32 rd = (insn>>12)&0xf;

		SetRegister(rd, count_leading_zeros_32(GetRegister(rm)));

		R15 += 4;
	}
	else if ((insn & 0x0ff000f0) == 0x01000050) // QADD - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>16)&0xf);

		s64 res = saturate_qbit_overflow((s64)src1 + (s64)src2);

		SetRegister((insn>>12)&0xf, (s32)res);
		R15 += 4;
	}
	else if ((insn & 0x0ff000f0) == 0x01400050) // QDADD - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>16)&0xf);

		// check if doubling operation will overflow
		s64 res = (s64)src2 * 2;
		saturate_qbit_overflow(res);

		src2 *= 2;
		res = saturate_qbit_overflow((s64)src1 + (s64)src2);

		SetRegister((insn>>12)&0xf, (s32)res);
		R15 += 4;
	}
	else if ((insn & 0x0ff000f0) == 0x01200050) // QSUB - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>16)&0xf);

		s64 res = saturate_qbit_overflow((s64)src1 - (s64)src2);

		SetRegister((insn>>12)&0xf, (s32)res);
		R15 += 4;
	}
	else if ((insn & 0x0ff000f0) == 0x01600050) // QDSUB - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>16)&0xf);

		// check if doubling operation will overflow
		s64 res = (s64)src2 * 2;
		saturate_qbit_overflow(res);

		src2 *= 2;
		res = saturate_qbit_overflow((s64)src1 - (s64)src2);

		SetRegister((insn>>12)&0xf, (s32)res);
		R15 += 4;
	}
	else if ((insn & 0x0ff00090) == 0x01000080) // SMLAxy - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>8)&0xf);
		s32 res1;

		// select top and bottom halves of src1/src2 and sign extend if necessary
		if (insn & 0x20)
			src1 >>= 16;
		else
			src1 = sext(src1, 16);

		if (insn & 0x40)
			src2 >>= 16;
		else
			src2 = sext(src2, 16);

		// do the signed multiply
		res1 = src1 * src2;
		// and the accumulate.  NOTE: only the accumulate can cause an overflow, which is why we do it this way.
		saturate_qbit_overflow((s64)res1 + (s64)GetRegister((insn>>12)&0xf));

		SetRegister((insn>>16)&0xf, res1 + GetRegister((insn>>12)&0xf));
		R15 += 4;
	}
	else if ((insn & 0x0ff00090) == 0x01400080) // SMLALxy - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>8)&0xf);

		s64 dst = (s64)GetRegister((insn>>12)&0xf);
		dst |= (s64)GetRegister((insn>>16)&0xf)<<32;

		// do the multiply and accumulate
		dst += mul_32x32(src1, src2);

		// write back the result
		SetRegister((insn>>12)&0xf, (u32)dst);
		SetRegister((insn>>16)&0xf, (u32)(dst >> 32));
		R15 += 4;
	}
	else if ((insn & 0x0ff00090) == 0x01600080) // SMULxy - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>8)&0xf);

		// select top and bottom halves of src1/src2 and sign extend if necessary
		if (insn & 0x20)
			src1 >>= 16;
		else
			src1 = sext(src1, 16);

		if (insn & 0x40)
			src2 >>= 16;
		else
			src2 = sext(src2, 16);

		s32 res = src1 * src2;
		SetRegister((insn>>16)&0xf, res);
		R15 += 4;
	}
	else if ((insn & 0x0ff000b0) == 0x012000a0) // SMULWy - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>8)&0xf);

		if (insn & 0x40)
			src2 >>= 16;
		else
			src2 = sext(src2, 16);

		s32 res = mul_32x32_shift(src1, src2, 16);
		SetRegister((insn>>16)&0xf, (u32)res);
		R15 += 4;
	}
	else if ((insn & 0x0ff000b0) == 0x01200080) // SMLAWy - v5
	{
		s32 src1 = GetRegister(insn&0xf);
		s32 src2 = GetRegister((insn>>8)&0xf);
		s32 src3 = GetRegister((insn>>12)&0xf);

		if (insn & 0x40)
			src2 >>= 16;
		else
			src2 = sext(src2, 16);

		s32 res = mul_32x32_shift(src1, src2, 16);

		// check for overflow and set the Q bit
		saturate_qbit_overflow((s64)src3 + res);

		// do the real accumulate
		src3 += res;

		// write the result back
		SetRegister((insn>>16)&0xf, (u32)res);
		R15 += 4;
	}
	else
	/* Multiply OR Swap OR Half Word Data Transfer */
	if ((insn & 0x0e000000) == 0 && (insn & 0x80) && (insn & 0x10))  // bits 27-25=000 bit 7=1 bit 4=1
	{
		/* Half Word Data Transfer */
		if (insn & 0x60)         // bits = 6-5 != 00
		{
			HandleHalfWordDT(insn);
		}
		else
		/* Swap */
		if (insn & 0x01000000)   // bit 24 = 1
		{
			HandleSwap(insn);
		}
		/* Multiply Or Multiply Long */
		else
		{
			/* multiply long */
			if (insn & 0x800000) // Bit 23 = 1 for Multiply Long
			{
				/* Signed? */
				if (insn & 0x00400000)
					HandleSMulLong(insn);
				else
					HandleUMulLong(insn);
			}
			/* multiply */
			else
			{
				HandleMul(insn);
			}
			R15 += 4;
		}
	}
	/* Data Processing OR PSR Transfer */
	else if ((insn & 0x0c000000) == 0)   // bits 27-26 == 00 - This check can only exist properly after Multiplication check above
	{
		/* PSR Transfer (MRS & MSR) */
		if (((insn & 0x00100000) == 0) && ((insn & 0x01800000) == 0x01000000)) // S bit must be clear, and bit 24,23 = 10
		{
			HandlePSRTransfer(insn);
			arm_icount += 2;       // PSR only takes 1 - S Cycle, so we add + 2, since at end, we -3..
			R15 += 4;
		}
		/* Data Processing */
		else
		{
			HandleALU(insn);
		}
	}
//  break;
}

static void arm7ops_4567(u32 insn) /* Data Transfer - Single Data Access */
{
//case 4:
//case 5:
//case 6:
//case 7:
	HandleMemSingle(insn);
	R15 += 4;
//  break;
}

static void arm7ops_89(u32 insn) /* Block Data Transfer/Access */
{
//case 8:
//case 9:
	HandleMemBlock(insn);
	R15 += 4;
//  break;
}

static void arm7ops_ab(u32 insn) /* Branch or Branch & Link */
{
//case 0xa:
//case 0xb:
	HandleBranch(insn, false);
//  break;
}

static void arm7ops_cd(u32 insn) /* Co-Processor Data Transfer */
{
//case 0xc:
//case 0xd:
	HandleCoProcDT(insn);
	R15 += 4;
//  break;
}

static void arm7ops_e(u32 insn) /* Co-Processor Data Operation or Register Transfer */
{
//case 0xe:
	if (insn & 0x10)
		HandleCoProcRT(insn);
	else
		HandleCoProcDO(insn);
	R15 += 4;
//  break;
}

static void arm7ops_f(u32 insn) /* Software Interrupt */
{
	arm_pendingSwi = true;
	arm_update_irq_state();
	arm_check_irq_state();
	//couldn't find any cycle counts for SWI
//  break;
}


const armops_ophandler ops_handler[0x20] =
{
	&arm7ops_0123, &arm7ops_0123, &arm7ops_0123, &arm7ops_0123,
	&arm7ops_4567, &arm7ops_4567, &arm7ops_4567, &arm7ops_4567,
	&arm7ops_89,   &arm7ops_89,   &arm7ops_ab,   &arm7ops_ab,
	&arm7ops_cd,   &arm7ops_cd,   &arm7ops_e,    &arm7ops_f,
	&arm9ops_undef,&arm9ops_1,    &arm9ops_undef,&arm9ops_undef,
	&arm9ops_undef,&arm9ops_57,   &arm9ops_undef,&arm9ops_57,
	&arm9ops_89,   &arm9ops_89,   &arm9ops_ab,   &arm9ops_ab,
	&arm9ops_c,    &arm9ops_undef,&arm9ops_e,    &arm9ops_undef,
};

