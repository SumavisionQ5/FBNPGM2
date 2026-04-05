// license:BSD-3-Clause
// copyright-holders:David Haywood, MetalliC

/*
	 ARM AIC (Advanced Interrupt Controller) from Atmel
	 typically integrated into the AM91SAM series of chips

	 current implementation was tested in pgm2 (pgm2.cpp) only, there might be mistakes if more advanced usage.
	 if this peripheral is not available as a standalone chip it could also be moved to
	 the CPU folder alongside the ARM instead
	 TODO:
		low/high input source types
		check if level/edge input source types logic correct
		FIQ output
*/

#include "driver.h"	// I/O line states
#include "burnint.h"	// SCAN_VAR ACB_DRIVER_DATA
#include "mame_stuff.h"
#include "atmel_arm_aic.h"

static u32 m_irqs_enabled;
static u32 m_irqs_pending;
static u32 m_current_irq_vector;
static u32 m_current_firq_vector;
static u32 m_status;
static u32 m_core_status;
static u32 m_spurious_vector;
static u32 m_debug;
static u32 m_fast_irqs;
static int m_lvlidx;
static int m_level_stack[9];

static u32 m_aic_smr[32];
static u32 m_aic_svr[32];


static void (__fastcall *m_irq_out)(s32);	// devcb_write_line m_irq_out;
static void check_irqs();
static void set_lines();

static void push_level(int lvl) { m_level_stack[++m_lvlidx] = lvl; }
static void pop_level() { if (m_lvlidx) --m_lvlidx; }
static int  get_level() { return m_level_stack[m_lvlidx]; }

static u32 irq_vector_r()
{
	u32 mask = m_irqs_enabled & m_irqs_pending & ~m_fast_irqs;
	m_current_irq_vector = m_spurious_vector;
	if (mask)
	{
		// looking for highest level pending interrupt, bigger than current
		int pri = get_level();
		int midx = -1;
		do
		{
			u8 idx = 31 - count_leading_zeros_32(mask);
			if ((int)(m_aic_smr[idx] & 7) >= pri)
			{
				midx = idx;
				pri = m_aic_smr[idx] & 7;
			}
			mask ^= 1 << idx;
		} while (mask);

		if (midx > 0)
		{
			m_status = midx;
			m_current_irq_vector = m_aic_svr[midx];
			// note: Debug PROTect mode not implemented (new level pushed on stack and pending line clear only when this register writen after read)
			push_level(m_aic_smr[midx] & 7);
			if (m_aic_smr[midx] & 0x20)         // auto clear pending if edge trigger mode
				m_irqs_pending ^= 1 << midx;
		}
	}

	m_core_status &= ~2;
	set_lines();
	return m_current_irq_vector;
}
static u32 firq_vector_r()
{
	m_current_firq_vector = (m_irqs_enabled & m_irqs_pending & m_fast_irqs) ? m_aic_svr[0] : m_spurious_vector;
	return m_current_firq_vector;
}

static u32 aic_isr_r()  { return m_status; }
static u32 aic_cisr_r() { return m_core_status; }
static u32 aic_ipr_r()  { return m_irqs_pending; }
static u32 aic_imr_r()  { return m_irqs_enabled; }
static u32 aic_ffsr_r() { return m_fast_irqs; }

// can't use ram() and share() in device submaps
static u32  aic_smr_r  (u32 offset) { return m_aic_smr[offset]; }
static u32  aic_svr_r  (u32 offset) { return m_aic_svr[offset]; }
static void aic_smr_w  (u32 offset, u32 data, u32 mem_mask = ~0) { COMBINE_DATA(&m_aic_smr[offset]); }
static void aic_svr_w  (u32 offset, u32 data, u32 mem_mask = ~0) { COMBINE_DATA(&m_aic_svr[offset]); }
static void aic_spu_w  (u32 offset, u32 data, u32 mem_mask = ~0) { COMBINE_DATA(&m_spurious_vector); }
static void aic_dcr_w  (u32 offset, u32 data, u32 mem_mask = ~0) { COMBINE_DATA(&m_debug); check_irqs(); }
static void aic_ffer_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_fast_irqs |= data & mem_mask; check_irqs(); }
static void aic_ffdr_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_fast_irqs &= ~(data & mem_mask) | 1; check_irqs(); }

static void aic_iecr_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_irqs_enabled |= data & mem_mask; check_irqs(); }
static void aic_idcr_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_irqs_enabled &= ~(data & mem_mask); check_irqs(); }
static void aic_iccr_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_irqs_pending &= ~(data & mem_mask); check_irqs(); }
static void aic_iscr_w (u32 offset, u32 data, u32 mem_mask = ~0) { m_irqs_pending |= data & mem_mask; check_irqs(); }
static void aic_eoicr_w(u32 offset, u32 data, u32 mem_mask = ~0) { m_status = 0; pop_level(); check_irqs(); }

u32  __fastcall arm_aic_regs_map_r(u32 offset)
{
	offset &= ~0xfffff000;

	switch ( offset / 0x80 ) {
		case 0:	// map(0x000, 0x07f)
			return aic_smr_r(offset - 0x000 >> 2);	// AIC_SMR[32] (AIC_SMR)  Source Mode Register
		case 1:	// map(0x080, 0x0ff)
			return aic_svr_r(offset - 0x080 >> 2);	// AIC_SVR[32] (AIC_SVR)  Source Vector Register
		case 2:	// map(0x100, 0x14b)
			switch (offset) {
				case 0x100: return irq_vector_r();	// AIC_IVR IRQ Vector Register
				case 0x104: return firq_vector_r();	// AIC_FVR FIQ Vector Register
				case 0x108: return aic_isr_r();		// AIC_ISR Interrupt Status Register
				case 0x10c: return aic_ipr_r();		// AIC_IPR Interrupt Pending Register
				case 0x110: return aic_imr_r();		// AIC_IMR Interrupt Mask Register
				case 0x114: return aic_cisr_r();	// AIC_CISR Core Interrupt Status Register
				case 0x148: return aic_ffsr_r();	// AIC_FFSR Fast Forcing Status Register
			}
	}	

	return 0;
}
void __fastcall arm_aic_regs_map_w(u32 offset, u32 data)
{
	offset &= ~0xfffff000;

	switch ( offset / 0x80 ) {
		case 0:	// map(0x000, 0x07f)
			aic_smr_w(offset - 0x000 >> 2, data); break;	// AIC_SMR[32] (AIC_SMR)  Source Mode Register
		case 1:	// map(0x080, 0x0ff)
			aic_svr_w(offset - 0x080 >> 2, data); break;	// AIC_SVR[32] (AIC_SVR)  Source Vector Register
		case 2:	// map(0x100, 0x14b)
			switch (offset) {
				case 0x120: aic_iecr_w (0, data); break;	// AIC_IECR Interrupt Enable Command Register
				case 0x124: aic_idcr_w (0, data); break;	// AIC_IDCR Interrupt Disable Command Register
				case 0x128: aic_iccr_w (0, data); break;	// AIC_ICCR	Interrupt Clear Command Register
				case 0x12c: aic_iscr_w (0, data); break;	// AIC_ISCR	Interrupt Set Command Register
				case 0x130: aic_eoicr_w(0, data); break;	// AIC_EOICR End of Interrupt Command Register
				case 0x134: aic_spu_w  (0, data); break;	// AIC_SPU Spurious Vector Register
				case 0x138: aic_dcr_w  (0, data); break;	// AIC_DCR Debug Control Register (Protect)
				case 0x140: aic_ffer_w (0, data); break;	// AIC_FFER Fast Forcing Enable Register
				case 0x144: aic_ffdr_w (0, data); break;	// AIC_FFDR Fast Forcing Disable Register
			}
	}
}

void arm_aic_device_start( void (__fastcall *irq_callback)(s32) )
{
	//m_irq_out.resolve_safe();	// mame/commit/e9c1f4a42a6758a6fb75403e28c7dc6cf869081c
	m_irq_out = irq_callback;
}

void arm_aic_device_scan(s32 action)
{
	if (action & ACB_DRIVER_DATA) {
		SCAN_VAR(m_irqs_enabled);
		SCAN_VAR(m_irqs_pending);
		SCAN_VAR(m_current_irq_vector);
		SCAN_VAR(m_current_firq_vector);
		SCAN_VAR(m_status);
		SCAN_VAR(m_core_status);
		SCAN_VAR(m_spurious_vector);
		SCAN_VAR(m_debug);
		SCAN_VAR(m_fast_irqs);
		SCAN_VAR(m_lvlidx);

		SCAN_VAR(m_aic_smr);
		SCAN_VAR(m_aic_svr);
		SCAN_VAR(m_level_stack);
	}
}

void arm_aic_device_reset()
{
	m_irqs_enabled = 0;
	m_irqs_pending = 0;
	m_current_irq_vector = 0;
	m_current_firq_vector = 0;
	m_status = 0;
	m_core_status = 0;
	m_spurious_vector = 0;
	m_debug = 0;
	m_fast_irqs = 1;
	m_lvlidx = 0;

	memset(m_aic_smr, 0, sizeof(m_aic_smr));
	memset(m_aic_svr, 0, sizeof(m_aic_svr));
	memset(m_level_stack, 0, sizeof(m_level_stack));
	m_level_stack[0] = -1;
}

void arm_aic_set_irq(int line, int state)
{
	// note: might be incorrect if edge triggered mode set
	if (state == ASSERT_LINE)
		m_irqs_pending |= 1 << line;
	else
		if (m_aic_smr[line] & 0x40)
			m_irqs_pending &= ~(1 << line);

	check_irqs();
}

static void check_irqs()
{
	m_core_status = 0;

	u32 mask = m_irqs_enabled & m_irqs_pending & ~m_fast_irqs;
	if (mask)
	{
		// check if we have pending interrupt with level more than current
		int pri = get_level();
		do
		{
			u8 idx = 31 - count_leading_zeros_32(mask);
			if ((int)(m_aic_smr[idx] & 7) > pri)
			{
				m_core_status |= 2;
				break;
			}
			mask ^= 1 << idx;
		} while (mask);
	}

	if (m_irqs_enabled & m_irqs_pending & m_fast_irqs)
		m_core_status |= 1;

	set_lines();
}

static void set_lines() // TODO FIQ
{
	m_irq_out((m_core_status & ~m_debug & 2) ? ASSERT_LINE : CLEAR_LINE);
}