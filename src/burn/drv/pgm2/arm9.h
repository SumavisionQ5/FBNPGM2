// Ported from MAME 0.254

// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz

/*****************************************************************************
 *
 *   arm7.h
 *   Portable ARM7TDMI CPU Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *  This work is based on:
 *  #1) 'Atmel Corporation ARM7TDMI (Thumb) Datasheet - January 1999'
 *  #2) Arm 2/3/6 emulator By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino (MAME CORE 0.76)
 *
 *****************************************************************************

 This file contains everything related to the arm7 cpu specific implementation.
 Anything related to the arm7 core itself is defined in arm7core.h instead.

 ******************************************************************************/

#pragma once

#ifndef MAME_CPU_ARM7_ARM7_H
#define MAME_CPU_ARM7_ARM7_H


#include "burnint.h"	// SCAN_VAR
#include "arm946es_intf.h"

/****************************************************************************************************
 *  PUBLIC FUNCTIONS
 ***************************************************************************************************/

enum
{
	ARCHFLAG_T        = 1,    // Thumb present
	ARCHFLAG_E        = 2,    // extended DSP operations present (only for v5+)
	ARCHFLAG_J        = 4,    // "Jazelle" (direct execution of Java bytecode)
	ARCHFLAG_MMU      = 8,    // has on-board MMU (traditional ARM style like the SA1110)
	ARCHFLAG_SA       = 16,   // StrongARM extensions (enhanced TLB)
	ARCHFLAG_XSCALE   = 32,   // XScale extensions (CP14, enhanced TLB)
	ARCHFLAG_MODE26   = 64,   // supports 26-bit backwards compatibility mode
	ARCHFLAG_K        = 128,  // enhanced MMU extensions present (only for v6)
	ARCHFLAG_T2       = 256,  // Thumb-2 present
};

enum
{
	ARM9_COPRO_ID_STEP_SA1110_A0 = 0,
	ARM9_COPRO_ID_STEP_SA1110_B0 = 4,
	ARM9_COPRO_ID_STEP_SA1110_B1 = 5,
	ARM9_COPRO_ID_STEP_SA1110_B2 = 8,
	ARM9_COPRO_ID_STEP_SA1110_B4 = 8,

	ARM9_COPRO_ID_STEP_PXA255_A0 = 6,

	ARM9_COPRO_ID_STEP_ARM946_A0 = 1,

	ARM9_COPRO_ID_STEP_ARM1176JZF_S_R0P0 = 0,
	ARM9_COPRO_ID_STEP_ARM1176JZF_S_R0P7 = 7,

	ARM9_COPRO_ID_PART_ARM1176JZF_S = 0xB76 << 4,
	ARM9_COPRO_ID_PART_SA1110       = 0xB11 << 4,
	ARM9_COPRO_ID_PART_ARM946       = 0x946 << 4,
	ARM9_COPRO_ID_PART_ARM920       = 0x920 << 4,
	ARM9_COPRO_ID_PART_ARM710       = 0x710 << 4,
	ARM9_COPRO_ID_PART_PXA250       = 0x200 << 4,
	ARM9_COPRO_ID_PART_PXA255       = 0x2d0 << 4,
	ARM9_COPRO_ID_PART_PXA270       = 0x411 << 4,
	ARM9_COPRO_ID_PART_GENERICARM7  = 0x700 << 4,

	ARM9_COPRO_ID_PXA255_CORE_REV_SHIFT = 10,

	ARM9_COPRO_ID_ARCH_V4     = 0x01 << 16,
	ARM9_COPRO_ID_ARCH_V4T    = 0x02 << 16,
	ARM9_COPRO_ID_ARCH_V5     = 0x03 << 16,
	ARM9_COPRO_ID_ARCH_V5T    = 0x04 << 16,
	ARM9_COPRO_ID_ARCH_V5TE   = 0x05 << 16,
	ARM9_COPRO_ID_ARCH_V5TEJ  = 0x06 << 16,
	ARM9_COPRO_ID_ARCH_V6     = 0x07 << 16,
	ARM9_COPRO_ID_ARCH_CPUID  = 0x0F << 16,

	ARM9_COPRO_ID_SPEC_REV0   = 0x00 << 20,
	ARM9_COPRO_ID_SPEC_REV1   = 0x01 << 20,

	ARM9_COPRO_ID_MFR_ARM     = 0x41 << 24,
	ARM9_COPRO_ID_MFR_DEC     = 0x44 << 24,
	ARM9_COPRO_ID_MFR_INTEL   = 0x69 << 24
};

extern const int *arm_reg_group;

extern u32 arm_reg[/*NUM_REGS*/];

extern bool arm_pendingIrq;
extern bool arm_pendingFiq;
extern bool arm_pendingAbtD;
extern bool arm_pendingAbtP;
extern bool arm_pendingUnd;
extern bool arm_pendingSwi;
extern bool arm_pending_interrupt;

extern int arm_icount;

extern u32 arm_control;
extern u32 arm_tlbBase;
extern u32 arm_tlb_base_mask;
extern u32 arm_faultStatus[2];
extern u32 arm_faultAddress;
extern u32 arm_fcsePID;

extern u32 arm_pid_offset;

extern u32 arm_domainAccessControl;
extern u32 arm_decoded_access_control[16];

extern u8  arm_archRev;
extern u32 arm_archFlags;

enum endian_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };

void arm_device_init(/*u32 clock, */u8 archRev, u32 archFlags, endian_t endianness);
void arm946es_device_init();

void arm_device_start();
void arm946es_device_start();

void arm_device_reset();

void arm_execute_run();
void arm_execute_set_input(int inputnum, int state);

struct IrqTrigger
{
	bool m_pending = false;
	s32 m_line, m_state;

	void set(s32 line, s32 state) {
		m_line = line; m_state = state;
		m_pending = true;
	}	// MAME: set_state_synced()

	void check() {
		if (m_pending) {
			arm_execute_set_input(m_line, m_state);
			m_pending = false;
		}
	}	// MAME: execute_timers()

	void scan() {
		SCAN_VAR(m_pending);
	}

	void reset() {
		m_pending = false;
	}
};
extern IrqTrigger arm_irq_trigger;

void arm_set_cpsr(u32 val);

void arm_check_irq_state();
void arm_update_irq_state();

void arm946es_cpu_write32(u32 addr, u32 data);
void arm946es_cpu_write16(u32 addr, u16 data);
void arm946es_cpu_write8(u32 addr, u8 data);
u32 arm946es_cpu_read32(u32 addr);
u32 arm946es_cpu_read16(u32 addr);
u8  arm946es_cpu_read8(u32 addr);

// Coprocessor support
// 946E-S has Protection Unit instead of ARM MMU so CP15 is quite different
void arm_do_callback(u32 data);
void arm_dt_r_callback(u32 insn, u32 *prn);
void arm_dt_w_callback(u32 insn, u32 *prn);
u32  arm946es_rt_r_callback(u32 offset);
void arm946es_rt_w_callback(u32 offset, u32 data);

typedef void ( *armthumb_ophandler ) (u32, u32);
extern const armthumb_ophandler thumb_handler[0x40 * 0x10];

typedef void ( *armops_ophandler ) (u32);
extern const armops_ophandler ops_handler[0x20];

/****************************************************************************************************
 *  HELPER FUNCTIONS
 ***************************************************************************************************/

// TODO LD:
//  - SIGN_BITS_DIFFER = THUMB_SIGN_BITS_DIFFER
//  - do while (0)
//  - HandleALUAddFlags = HandleThumbALUAddFlags except for PC incr
//  - HandleALUSubFlags = HandleThumbALUSubFlags except for PC incr

#define IsNeg(i)	((i) >> 31)
#define IsPos(i)	((~(i)) >> 31)

/* Set NZCV flags for ADDS / SUBS */
#define HandleALUAddFlags(rd, rn, op2)																				\
	if (insn & INSN_S)																								\
	arm_set_cpsr(((GET_CPSR & ~(N_MASK | Z_MASK | V_MASK | C_MASK)) |												\
				(((!SIGN_BITS_DIFFER(rn, op2)) && SIGN_BITS_DIFFER(rn, rd)) << V_BIT) |								\
				(((IsNeg(rn) & IsNeg(op2)) | (IsNeg(rn) & IsPos(rd)) | (IsNeg(op2) & IsPos(rd))) ? C_MASK : 0) |	\
				HandleALUNZFlags(rd)));																				\
	R15 += 4;

#define HandleThumbALUAddFlags(rd, rn, op2)																			\
	arm_set_cpsr(((GET_CPSR & ~(N_MASK | Z_MASK | V_MASK | C_MASK)) |												\
				(((!THUMB_SIGN_BITS_DIFFER(rn, op2)) && THUMB_SIGN_BITS_DIFFER(rn, rd)) << V_BIT) |					\
				(((~(rn)) < (op2)) << C_BIT) |																		\
				HandleALUNZFlags(rd)));																				\
	R15 += 2;

#define HandleALUSubFlags(rd, rn, op2)																				\
	if (insn & INSN_S)																								\
	arm_set_cpsr(((GET_CPSR & ~(N_MASK | Z_MASK | V_MASK | C_MASK)) |												\
				((SIGN_BITS_DIFFER(rn, op2) && SIGN_BITS_DIFFER(rn, rd)) << V_BIT) |								\
				(((IsNeg(rn) & IsPos(op2)) | (IsNeg(rn) & IsPos(rd)) | (IsPos(op2) & IsPos(rd))) ? C_MASK : 0) |	\
				HandleALUNZFlags(rd)));																				\
	R15 += 4;

#define HandleThumbALUSubFlags(rd, rn, op2)																			\
	arm_set_cpsr(((GET_CPSR & ~(N_MASK | Z_MASK | V_MASK | C_MASK)) |												\
				((THUMB_SIGN_BITS_DIFFER(rn, op2) && THUMB_SIGN_BITS_DIFFER(rn, rd)) << V_BIT) |					\
				(((IsNeg(rn) & IsPos(op2)) | (IsNeg(rn) & IsPos(rd)) | (IsPos(op2) & IsPos(rd))) ? C_MASK : 0) |	\
				HandleALUNZFlags(rd)));																				\
	R15 += 2;

/* Set NZC flags for logical operations. */

// This macro (which I didn't write) - doesn't make it obvious that the SIGN BIT = 31, just as the N Bit does,
// therefore, N is set by default
#define HandleALUNZFlags(rd)		(((rd) & SIGN_BIT) | ((!(rd)) << Z_BIT))

// Long ALU Functions use bit 63
#define HandleLongALUNZFlags(rd)	((((rd) & ((u64)1 << 63)) >> 32) | ((!(rd)) << Z_BIT))

#define HandleALULogicalFlags(rd, sc)																				\
	if (insn & INSN_S)																								\
	arm_set_cpsr(((GET_CPSR & ~(N_MASK | Z_MASK | C_MASK)) |														\
				HandleALUNZFlags(rd) |																				\
				(((sc) != 0) << C_BIT)));																			\
	R15 += 4;


// used to be functions, but no longer a need, so we'll use define for better speed.
#define GetRegister(rIndex)        arm_reg[arm_reg_group[rIndex]]
#define SetRegister(rIndex, value) arm_reg[arm_reg_group[rIndex]] = value

#define GetModeRegister(mode, rIndex)        arm_reg[sRegisterTable[mode][rIndex]]
#define SetModeRegister(mode, rIndex, value) arm_reg[sRegisterTable[mode][rIndex]] = value


#endif // MAME_CPU_ARM7_ARM7_H
