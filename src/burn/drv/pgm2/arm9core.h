// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz

/*****************************************************************************
 *
 *   arm7core.h
 *   Portable ARM7TDMI Core Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *  This work is based on:
 *  #1) 'Atmel Corporation ARM7TDMI (Thumb) Datasheet - January 1999'
 *  #2) Arm 2/3/6 emulator By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino (MAME CORE 0.76)
 *
 *****************************************************************************

 This file contains everything related to the arm7 core itself, and is presumed
 to be cpu implementation non-specific, ie, applies to only the core.

 ******************************************************************************/

#pragma once

#ifndef MAME_CPU_ARM7_CORE_H
#define MAME_CPU_ARM7_CORE_H


/****************************************************************************************************
 *  INTERRUPT LINES/EXCEPTIONS
 ***************************************************************************************************/
enum
{
	ARM7_IRQ_LINE=0, ARM7_FIRQ_LINE,
	ARM7_ABORT_EXCEPTION, ARM7_ABORT_PREFETCH_EXCEPTION, ARM7_UNDEFINE_EXCEPTION,
	ARM7_NUM_LINES
};
// Really there's only 1 ABORT Line.. and cpu decides whether it's during data fetch or prefetch, but we let the user specify

/****************************************************************************************************
 *  ARM7 CORE REGISTERS
 ***************************************************************************************************/
enum
{
	ARM7_PC = 0,
	ARM7_R0, ARM7_R1, ARM7_R2, ARM7_R3, ARM7_R4, ARM7_R5, ARM7_R6, ARM7_R7,
	ARM7_R8, ARM7_R9, ARM7_R10, ARM7_R11, ARM7_R12, ARM7_R13, ARM7_R14, ARM7_R15,
	ARM7_FR8, ARM7_FR9, ARM7_FR10, ARM7_FR11, ARM7_FR12, ARM7_FR13, ARM7_FR14,
	ARM7_IR13, ARM7_IR14, ARM7_SR13, ARM7_SR14, ARM7_FSPSR, ARM7_ISPSR, ARM7_SSPSR,
	ARM7_CPSR, ARM7_AR13, ARM7_AR14, ARM7_ASPSR, ARM7_UR13, ARM7_UR14, ARM7_USPSR, ARM7_LOGTLB
};

/* There are 36 Unique - 32 bit processor registers */
/* Each mode has 17 registers (except user & system, which have 16) */
/* This is a list of each *unique* register */
enum
{
	/* All modes have the following */
	eR0 = 0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
	eR8, eR9, eR10, eR11, eR12,
	eR13, /* Stack Pointer */
	eR14, /* Link Register (holds return address) */
	eR15, /* Program Counter */
	eCPSR, /* Current Status Program Register */

	/* Fast Interrupt - Bank switched registers */
	eR8_FIQ, eR9_FIQ, eR10_FIQ, eR11_FIQ, eR12_FIQ, eR13_FIQ, eR14_FIQ, eSPSR_FIQ,

	/* IRQ - Bank switched registers */
	eR13_IRQ, eR14_IRQ, eSPSR_IRQ,

	/* Supervisor/Service Mode - Bank switched registers */
	eR13_SVC, eR14_SVC, eSPSR_SVC,

	/* Abort Mode - Bank switched registers */
	eR13_ABT, eR14_ABT, eSPSR_ABT,

	/* Undefined Mode - Bank switched registers */
	eR13_UND, eR14_UND, eSPSR_UND,

	NUM_REGS
};

/****************************************************************************************************
 *  Coprocessor-related macros
 ***************************************************************************************************/
#define COPRO_DOMAIN_NO_ACCESS              0
#define COPRO_DOMAIN_CLIENT                 1
#define COPRO_DOMAIN_RESV                   2
#define COPRO_DOMAIN_MANAGER                3

#define COPRO_FAULT_NONE                    0
#define COPRO_FAULT_TRANSLATE_SECTION       5
#define COPRO_FAULT_TRANSLATE_PAGE          7
#define COPRO_FAULT_DOMAIN_SECTION          9
#define COPRO_FAULT_DOMAIN_PAGE             11
#define COPRO_FAULT_PERM_SECTION            13
#define COPRO_FAULT_PERM_PAGE               15

#define COPRO_TLB_BASE                      arm_tlbBase
#define COPRO_TLB_BASE_MASK                 0xffffc000
#define COPRO_TLB_VADDR_FLTI_MASK           0xfff00000
#define COPRO_TLB_VADDR_FLTI_MASK_SHIFT     18
#define COPRO_TLB_VADDR_CSLTI_MASK          0x000ff000
#define COPRO_TLB_VADDR_CSLTI_MASK_SHIFT    10
#define COPRO_TLB_VADDR_FSLTI_MASK          0x000ffc00
#define COPRO_TLB_VADDR_FSLTI_MASK_SHIFT    8
#define COPRO_TLB_CFLD_ADDR_MASK            0xfffffc00
#define COPRO_TLB_CFLD_ADDR_MASK_SHIFT      10
#define COPRO_TLB_FPTB_ADDR_MASK            0xfffff000
#define COPRO_TLB_FPTB_ADDR_MASK_SHIFT      12
#define COPRO_TLB_SECTION_PAGE_MASK         0xfff00000
#define COPRO_TLB_LARGE_PAGE_MASK           0xffff0000
#define COPRO_TLB_SMALL_PAGE_MASK           0xfffff000
#define COPRO_TLB_TINY_PAGE_MASK            0xfffffc00
#define COPRO_TLB_STABLE_MASK               0xfff00000
#define COPRO_TLB_LSTABLE_MASK              0xfffff000
#define COPRO_TLB_TTABLE_MASK               0xfffffc00
#define COPRO_TLB_UNMAPPED                  0
#define COPRO_TLB_LARGE_PAGE                1
#define COPRO_TLB_SMALL_PAGE                2
#define COPRO_TLB_TINY_PAGE                 3
#define COPRO_TLB_COARSE_TABLE              1
#define COPRO_TLB_SECTION_TABLE             2
#define COPRO_TLB_FINE_TABLE                3
#define COPRO_TLB_TYPE_SECTION              0
#define COPRO_TLB_TYPE_LARGE                1
#define COPRO_TLB_TYPE_SMALL                2
#define COPRO_TLB_TYPE_TINY                 3

#define COPRO_CTRL                          arm_control
#define COPRO_CTRL_MMU_EN                   0x00000001
#define COPRO_CTRL_ADDRFAULT_EN             0x00000002
#define COPRO_CTRL_DCACHE_EN                0x00000004
#define COPRO_CTRL_WRITEBUF_EN              0x00000008
#define COPRO_CTRL_ENDIAN                   0x00000080
#define COPRO_CTRL_SYSTEM                   0x00000100
#define COPRO_CTRL_ROM                      0x00000200
#define COPRO_CTRL_ICACHE_EN                0x00001000
#define COPRO_CTRL_INTVEC_ADJUST            0x00002000
#define COPRO_CTRL_ADDRFAULT_EN_SHIFT       1
#define COPRO_CTRL_DCACHE_EN_SHIFT          2
#define COPRO_CTRL_WRITEBUF_EN_SHIFT        3
#define COPRO_CTRL_ENDIAN_SHIFT             7
#define COPRO_CTRL_SYSTEM_SHIFT             8
#define COPRO_CTRL_ROM_SHIFT                9
#define COPRO_CTRL_ICACHE_EN_SHIFT          12
#define COPRO_CTRL_INTVEC_ADJUST_SHIFT      13
#define COPRO_CTRL_LITTLE_ENDIAN            0
#define COPRO_CTRL_BIG_ENDIAN               1
#define COPRO_CTRL_INTVEC_0                 0
#define COPRO_CTRL_INTVEC_F                 1
#define COPRO_CTRL_MASK                     0x0000338f

#define COPRO_DOMAIN_ACCESS_CONTROL         arm_domainAccessControl

#define COPRO_FAULT_STATUS_D                arm_faultStatus[0]
#define COPRO_FAULT_STATUS_P                arm_faultStatus[1]

#define COPRO_FAULT_ADDRESS                 arm_faultAddress

#define COPRO_FCSE_PID                      arm_fcsePID

/****************************************************************************************************
 *  VARIOUS INTERNAL STRUCS/DEFINES/ETC..
 ***************************************************************************************************/
// Mode values come from bit 4-0 of CPSR, but we are ignoring bit 4 here, since bit 4 always = 1 for valid modes
enum
{
	eARM7_MODE_USER = 0x0,      // Bit: 4-0 = 10000
	eARM7_MODE_FIQ  = 0x1,      // Bit: 4-0 = 10001
	eARM7_MODE_IRQ  = 0x2,      // Bit: 4-0 = 10010
	eARM7_MODE_SVC  = 0x3,      // Bit: 4-0 = 10011
	eARM7_MODE_ABT  = 0x7,      // Bit: 4-0 = 10111
	eARM7_MODE_UND  = 0xb,      // Bit: 4-0 = 11011
	eARM7_MODE_SYS  = 0xf       // Bit: 4-0 = 11111
};

#define ARM7_NUM_MODES 0x10

static const int thumbCycles[256] =
{
//  0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 1
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 2
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 3
	1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 4
	2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 5
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,  // 6
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,  // 7
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,  // 8
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,  // 9
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // a
	1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 4, 1, 1,  // b
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // c
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3,  // d
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // e
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2   // f
};

/* 17 processor registers are visible at any given time,
 * banked depending on processor mode.
 */

static const int sRegisterTable[ARM7_NUM_MODES][18] =
{
	{ /* USR */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13, eR14,
		eR15, eCPSR		// No SPSR in this mode
	},
	{ /* FIQ */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8_FIQ, eR9_FIQ, eR10_FIQ, eR11_FIQ, eR12_FIQ,
		eR13_FIQ, eR14_FIQ,
		eR15, eCPSR, eSPSR_FIQ
	},
	{ /* IRQ */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13_IRQ, eR14_IRQ,
		eR15, eCPSR, eSPSR_IRQ
	},
	{ /* SVC */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13_SVC, eR14_SVC,
		eR15, eCPSR, eSPSR_SVC
	},
	{0}, {0}, {0},		// values for modes 4,5,6 are not valid
	{ /* ABT */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13_ABT, eR14_ABT,
		eR15, eCPSR, eSPSR_ABT
	},
	{0}, {0}, {0},		// values for modes 8,9,a are not valid!
	{ /* UND */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13_UND, eR14_UND,
		eR15, eCPSR, eSPSR_UND
	},
	{0}, {0}, {0},		// values for modes c,d, e are not valid!
	{ /* SYS */
		eR0, eR1, eR2, eR3, eR4, eR5, eR6, eR7,
		eR8, eR9, eR10, eR11, eR12,
		eR13, eR14,
		eR15, eCPSR		// No SPSR in this mode
	}
};

#define N_BIT   31
#define Z_BIT   30
#define C_BIT   29
#define V_BIT   28
#define Q_BIT   27
#define I_BIT   7
#define F_BIT   6
#define T_BIT   5   // Thumb mode

#define N_MASK  ((u32)(1 << N_BIT)) /* Negative flag */
#define Z_MASK  ((u32)(1 << Z_BIT)) /* Zero flag */
#define C_MASK  ((u32)(1 << C_BIT)) /* Carry flag */
#define V_MASK  ((u32)(1 << V_BIT)) /* oVerflow flag */
#define Q_MASK  ((u32)(1 << Q_BIT)) /* signed overflow for QADD, MAC */
#define I_MASK  ((u32)(1 << I_BIT)) /* Interrupt request disable */
#define F_MASK  ((u32)(1 << F_BIT)) /* Fast interrupt request disable */
#define T_MASK  ((u32)(1 << T_BIT)) /* Thumb Mode flag */

#define N_IS_SET(pc)    ((pc) & N_MASK)
#define Z_IS_SET(pc)    ((pc) & Z_MASK)
#define C_IS_SET(pc)    ((pc) & C_MASK)
#define V_IS_SET(pc)    ((pc) & V_MASK)
#define Q_IS_SET(pc)    ((pc) & Q_MASK)
#define I_IS_SET(pc)    ((pc) & I_MASK)
#define F_IS_SET(pc)    ((pc) & F_MASK)
#define T_IS_SET(pc)    ((pc) & T_MASK)

#define N_IS_CLEAR(pc)  (!N_IS_SET(pc))
#define Z_IS_CLEAR(pc)  (!Z_IS_SET(pc))
#define C_IS_CLEAR(pc)  (!C_IS_SET(pc))
#define V_IS_CLEAR(pc)  (!V_IS_SET(pc))
#define Q_IS_CLEAR(pc)  (!Q_IS_SET(pc))
#define I_IS_CLEAR(pc)  (!I_IS_SET(pc))
#define F_IS_CLEAR(pc)  (!F_IS_SET(pc))
#define T_IS_CLEAR(pc)  (!T_IS_SET(pc))

/* Deconstructing an instruction */
#define INSN_COND                   ((u32)0xf0000000u)
#define INSN_SDT_L                  ((u32)0x00100000u)
#define INSN_SDT_W                  ((u32)0x00200000u)
#define INSN_SDT_B                  ((u32)0x00400000u)
#define INSN_SDT_U                  ((u32)0x00800000u)
#define INSN_SDT_P                  ((u32)0x01000000u)
#define INSN_BDT_L                  ((u32)0x00100000u)
#define INSN_BDT_W                  ((u32)0x00200000u)
#define INSN_BDT_S                  ((u32)0x00400000u)
#define INSN_BDT_U                  ((u32)0x00800000u)
#define INSN_BDT_P                  ((u32)0x01000000u)
#define INSN_BDT_REGS               ((u32)0x0000ffffu)
#define INSN_SDT_IMM                ((u32)0x00000fffu)
#define INSN_MUL_A                  ((u32)0x00200000u)
#define INSN_MUL_RM                 ((u32)0x0000000fu)
#define INSN_MUL_RS                 ((u32)0x00000f00u)
#define INSN_MUL_RN                 ((u32)0x0000f000u)
#define INSN_MUL_RD                 ((u32)0x000f0000u)
#define INSN_I                      ((u32)0x02000000u)
#define INSN_OPCODE                 ((u32)0x01e00000u)
#define INSN_S                      ((u32)0x00100000u)
#define INSN_BL                     ((u32)0x01000000u)
#define INSN_BRANCH                 ((u32)0x00ffffffu)
#define INSN_SWI                    ((u32)0x00ffffffu)
#define INSN_RN                     ((u32)0x000f0000u)
#define INSN_RD                     ((u32)0x0000f000u)
#define INSN_OP2                    ((u32)0x00000fffu)
#define INSN_OP2_SHIFT              ((u32)0x00000f80u)
#define INSN_OP2_SHIFT_TYPE         ((u32)0x00000070u)
#define INSN_OP2_RM                 ((u32)0x0000000fu)
#define INSN_OP2_ROTATE             ((u32)0x00000f00u)
#define INSN_OP2_IMM                ((u32)0x000000ffu)
#define INSN_OP2_SHIFT_TYPE_SHIFT   4
#define INSN_OP2_SHIFT_SHIFT        7
#define INSN_OP2_ROTATE_SHIFT       8
#define INSN_MUL_RS_SHIFT           8
#define INSN_MUL_RN_SHIFT           12
#define INSN_MUL_RD_SHIFT           16
#define INSN_OPCODE_SHIFT           21
#define INSN_RN_SHIFT               16
#define INSN_RD_SHIFT               12
#define INSN_COND_SHIFT             28

#define INSN_COPRO_OP1              ((u32)0x00e00000u)
#define INSN_COPRO_N                ((u32)0x00100000u)
#define INSN_COPRO_CREG             ((u32)0x000f0000u)
#define INSN_COPRO_AREG             ((u32)0x0000f000u)
#define INSN_COPRO_CPNUM            ((u32)0x00000f00u)
#define INSN_COPRO_OP2              ((u32)0x000000e0u)
#define INSN_COPRO_OP3              ((u32)0x0000000fu)
#define INSN_COPRO_OP1_SHIFT        21
#define INSN_COPRO_N_SHIFT          20
#define INSN_COPRO_CREG_SHIFT       16
#define INSN_COPRO_AREG_SHIFT       12
#define INSN_COPRO_CPNUM_SHIFT      8
#define INSN_COPRO_OP2_SHIFT        5

#define THUMB_INSN_TYPE             ((u16)0xf000)
#define THUMB_COND_TYPE             ((u16)0x0f00)
#define THUMB_GROUP4_TYPE           ((u16)0x0c00)
#define THUMB_GROUP5_TYPE           ((u16)0x0e00)
#define THUMB_GROUP5_RM             ((u16)0x01c0)
#define THUMB_GROUP5_RN             ((u16)0x0038)
#define THUMB_GROUP5_RD             ((u16)0x0007)
#define THUMB_ADDSUB_RNIMM          ((u16)0x01c0)
#define THUMB_ADDSUB_RS             ((u16)0x0038)
#define THUMB_ADDSUB_RD             ((u16)0x0007)
#define THUMB_INSN_CMP              ((u16)0x0800)
#define THUMB_INSN_SUB              ((u16)0x0800)
#define THUMB_INSN_IMM_RD           ((u16)0x0700)
#define THUMB_INSN_IMM_S            ((u16)0x0080)
#define THUMB_INSN_IMM              ((u16)0x00ff)
#define THUMB_INSN_ADDSUB           ((u16)0x0800)
#define THUMB_ADDSUB_TYPE           ((u16)0x0600)
#define THUMB_HIREG_OP              ((u16)0x0300)
#define THUMB_HIREG_H               ((u16)0x00c0)
#define THUMB_HIREG_RS              ((u16)0x0038)
#define THUMB_HIREG_RD              ((u16)0x0007)
#define THUMB_STACKOP_TYPE          ((u16)0x0f00)
#define THUMB_STACKOP_L             ((u16)0x0800)
#define THUMB_STACKOP_RD            ((u16)0x0700)
#define THUMB_ALUOP_TYPE            ((u16)0x03c0)
#define THUMB_BLOP_LO               ((u16)0x0800)
#define THUMB_BLOP_OFFS             ((u16)0x07ff)
#define THUMB_SHIFT_R               ((u16)0x0800)
#define THUMB_SHIFT_AMT             ((u16)0x07c0)
#define THUMB_HALFOP_L              ((u16)0x0800)
#define THUMB_HALFOP_OFFS           ((u16)0x07c0)
#define THUMB_BRANCH_OFFS           ((u16)0x07ff)
#define THUMB_LSOP_L                ((u16)0x0800)
#define THUMB_LSOP_OFFS             ((u16)0x07c0)
#define THUMB_MULTLS                ((u16)0x0800)
#define THUMB_MULTLS_BASE           ((u16)0x0700)
#define THUMB_RELADDR_SP            ((u16)0x0800)
#define THUMB_RELADDR_RD            ((u16)0x0700)
#define THUMB_INSN_TYPE_SHIFT       12
#define THUMB_COND_TYPE_SHIFT       8
#define THUMB_GROUP4_TYPE_SHIFT     10
#define THUMB_GROUP5_TYPE_SHIFT     9
#define THUMB_ADDSUB_TYPE_SHIFT     9
#define THUMB_INSN_IMM_RD_SHIFT     8
#define THUMB_STACKOP_TYPE_SHIFT    8
#define THUMB_HIREG_OP_SHIFT        8
#define THUMB_STACKOP_RD_SHIFT      8
#define THUMB_MULTLS_BASE_SHIFT     8
#define THUMB_RELADDR_RD_SHIFT      8
#define THUMB_HIREG_H_SHIFT         6
#define THUMB_HIREG_RS_SHIFT        3
#define THUMB_ALUOP_TYPE_SHIFT      6
#define THUMB_SHIFT_AMT_SHIFT       6
#define THUMB_HALFOP_OFFS_SHIFT     6
#define THUMB_LSOP_OFFS_SHIFT       6
#define THUMB_GROUP5_RM_SHIFT       6
#define THUMB_GROUP5_RN_SHIFT       3
#define THUMB_GROUP5_RD_SHIFT       0
#define THUMB_ADDSUB_RNIMM_SHIFT    6
#define THUMB_ADDSUB_RS_SHIFT       3
#define THUMB_ADDSUB_RD_SHIFT       0

enum
{
	OPCODE_AND, /* 0000 */
	OPCODE_EOR, /* 0001 */
	OPCODE_SUB, /* 0010 */
	OPCODE_RSB, /* 0011 */
	OPCODE_ADD, /* 0100 */
	OPCODE_ADC, /* 0101 */
	OPCODE_SBC, /* 0110 */
	OPCODE_RSC, /* 0111 */
	OPCODE_TST, /* 1000 */
	OPCODE_TEQ, /* 1001 */
	OPCODE_CMP, /* 1010 */
	OPCODE_CMN, /* 1011 */
	OPCODE_ORR, /* 1100 */
	OPCODE_MOV, /* 1101 */
	OPCODE_BIC, /* 1110 */
	OPCODE_MVN  /* 1111 */
};

enum
{
	COND_EQ = 0,          /*  Z           equal                   */
	COND_NE,              /* ~Z           not equal               */
	COND_CS, COND_HS = 2, /*  C           unsigned higher or same */
	COND_CC, COND_LO = 3, /* ~C           unsigned lower          */
	COND_MI,              /*  N           negative                */
	COND_PL,              /* ~N           positive or zero        */
	COND_VS,              /*  V           overflow                */
	COND_VC,              /* ~V           no overflow             */
	COND_HI,              /*  C && ~Z     unsigned higher         */
	COND_LS,              /* ~C ||  Z     unsigned lower or same  */
	COND_GE,              /*  N == V      greater or equal        */
	COND_LT,              /*  N != V      less than               */
	COND_GT,              /* ~Z && N == V greater than            */
	COND_LE,              /*  Z || N != V less than or equal      */
	COND_AL,              /*  1           always                  */
	COND_NV               /*  0           never                   */
};

/* Convenience Macros */
#define R15                           arm_reg[eR15]
#define SPSR                          17               // SPSR is always the 18th register in our 0 based array sRegisterTable[][18]
#define GET_CPSR                      arm_reg[eCPSR]
#define MODE_FLAG                     0xF              // Mode bits are 4:0 of CPSR, but we ignore bit 4.
#define GET_MODE                      (GET_CPSR & MODE_FLAG)
#define SIGN_BIT                      ((u32)(1 << 31))
#define SIGN_BITS_DIFFER(a, b)        (((a) ^ (b)) >> 31)
/* I really don't know why these were set to 16-bit, the thumb registers are still 32-bit ... */
#define THUMB_SIGN_BIT                ((u32)(1 << 31))
#define THUMB_SIGN_BITS_DIFFER(a, b)  (((a)^(b)) >> 31)

#define SR_MODE32 0x10

#define MODE32    (GET_CPSR & SR_MODE32)
#define MODE26    (!(GET_CPSR & SR_MODE32))
#define GET_PC    (MODE32 ? R15 : R15 & 0x03FFFFFC)

#define ARM7_TLB_ABORT_D  (1 << 0)
#define ARM7_TLB_ABORT_P  (1 << 1)
#define ARM7_TLB_READ     (1 << 2)
#define ARM7_TLB_WRITE    (1 << 3)

void arm_switch_mode(u32 cpsr_mode_val);


#endif /* MAME_CPU_ARM7_CORE_H */
