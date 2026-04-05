// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz

/*****************************************************************************
 *
 *   arm7.c
 *   Portable CPU Emulator for 32-bit ARM v3/4/5/6
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *   Thumb, DSP, and MMU support and many bugfixes by R. Belmont and Ryan Holtz.
 *
 *  This work is based on:
 *  #1) 'Atmel Corporation ARM7TDMI (Thumb) Datasheet - January 1999'
 *  #2) Arm 2/3/6 emulator By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino (MAME CORE 0.76)
 *
 *****************************************************************************/

/******************************************************************************
 *  Notes:

	** This is a plain vanilla implementation of an ARM7 cpu which incorporates my ARM7 core.
	   It can be used as is, or used to demonstrate how to utilize the arm7 core to create a cpu
	   that uses the core, since there are numerous different mcu packages that incorporate an arm7 core.

	   See the notes in the arm7core.inc file itself regarding issues/limitations of the arm7 core.
	**

TODO:
- Cleanups
- Fix and finish the DRC code, or remove it entirely

*****************************************************************************/

/*****************************************************************************
 *
 *   arm7core.inc
 *   Portable ARM7TDMI Core Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *  This work is based on:
 *  #1) 'Atmel Corporation ARM7TDMI (Thumb) Datasheet - January 1999'
 *  #2) Arm 2/3/6 emulator By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino (MAME CORE 0.76)
 *  #3) Thumb support by Ryan Holtz
 *  #4) Additional Thumb support and bugfixes by R. Belmont
 *
 *****************************************************************************/

 /******************************************************************************
 *  Notes:

	**This core comes from my AT91 cpu core contributed to PinMAME,
	  but with all the AT91 specific junk removed,
	  which leaves just the ARM7TDMI core itself. I further removed the CPU specific MAME stuff
	  so you just have the actual ARM7 core itself, since many cpu's incorporate an ARM7 core, but add on
	  many cpu specific functionality.

	  Therefore, to use the core, you simpy include this file along with the .h file into your own cpu specific
	  implementation, and therefore, this file shouldn't be compiled as part of your project directly.

	  For better or for worse, the code itself is very much intact from it's arm 2/3/6 origins from
	  Bryan & Phil's work. I contemplated merging it in, but thought the fact that the CPSR is
	  no longer part of the PC was enough of a change to make it annoying to merge.
	**

	Coprocessor functions are heavily implementation specific, so callback handlers are used to allow the
	implementation to handle the functionality. Custom DASM handlers are included as well to allow the DASM
	output to be tailored to the co-proc implementation details.

	Todo:
	26 bit compatibility mode not implemented.
	Data Processing opcodes need cycle count adjustments (see page 194 of ARM7TDMI manual for instruction timing summary)
	Multi-emulated cpu support untested, but probably will not work too well, as no effort was made to code for more than 1.
	Could not find info on what the TEQP opcode is from page 44..
	I have no idea if user bank switching is right, as I don't fully understand it's use.
	Search for Todo: tags for remaining items not done.


	Differences from Arm 2/3 (6 also?)
	-Thumb instruction support
	-Full 32 bit address support
	-PC no longer contains CPSR information, CPSR is own register now
	-New register SPSR to store previous contents of CPSR (this register is banked in many modes)
	-New opcodes for CPSR transfer, Long Multiplication, Co-Processor support, and some others
	-User Bank Mode transfer using certain flags which were previously unallowed (LDM/STM with S Bit & R15)
	-New operation modes? (unconfirmed)

	Based heavily on arm core from MAME 0.76:
	*****************************************
	ARM 2/3/6 Emulation

	Todo:
	Software interrupts unverified (nothing uses them so far, but they should be ok)
	Timing - Currently very approximated, nothing relies on proper timing so far.
	IRQ timing not yet correct (again, nothing is affected by this so far).

	By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino
*****************************************************************************/


#include "mame_stuff.h"
#include "arm9.h"
#include "arm9core.h"

// -----------------------------------------------------------------------------
//   Variables Declaration Section
// -----------------------------------------------------------------------------

const int *arm_reg_group;

static u32  arm_insn_prefetch_depth;
static u32  arm_insn_prefetch_count;
static u32  arm_insn_prefetch_index;
static u32  arm_insn_prefetch_buffer[3];
static u32  arm_insn_prefetch_address[3];
static bool arm_insn_prefetch_valid[3];

static u32 arm_prefetch_word0_shift;
static u32 arm_prefetch_word1_shift;

struct tlb_entry
{
	bool valid;
	u8 domain;
	u8 access;
	u32 table_bits;
	u32 base_addr;
	u8 type;
};
static tlb_entry m_dtlb_entries[0x2000];
static tlb_entry m_itlb_entries[0x2000];
static u8 m_dtlb_entry_index[0x1000];
static u8 m_itlb_entry_index[0x1000];

// CPU state ----------------
u32 arm_reg[NUM_REGS];

bool arm_pendingIrq;
bool arm_pendingFiq;
bool arm_pendingAbtD;
bool arm_pendingAbtP;
bool arm_pendingUnd;
bool arm_pendingSwi;
bool arm_pending_interrupt;

int arm_icount;

u32 arm_control;
u32 arm_tlbBase;
u32 arm_tlb_base_mask;

u32 arm_faultStatus[2];
u32 arm_faultAddress;
u32 arm_fcsePID;

u32 arm_pid_offset;

u32 arm_domainAccessControl;
u32 arm_decoded_access_control[16];

u8  arm_archRev;		// ARM architecture revision (3, 4, and 5 are valid)
u32 arm_archFlags;		// architecture flags
// CPU state ----------------

IrqTrigger arm_irq_trigger;

static endian_t arm_endian;

static u32 arm_vectorbase;
static u32 arm_copro_id;

u32 cp15_control, cp15_itcm_base, cp15_dtcm_base, cp15_itcm_size, cp15_dtcm_size;
u32 cp15_itcm_end, cp15_dtcm_end, cp15_itcm_reg, cp15_dtcm_reg;
u8 ITCM[0x8000], DTCM[0x4000];

static void arm946es_refresh_dtcm();	// forward
static void arm946es_refresh_itcm();	// forward

static inline u32 arm_cpu_readop32(u32 addr);	// m_pr32

static inline void update_reg_ptr();	// forward

// burn.cpp -> BurnPostloadFunction
extern "C" void state_save_register_func_postload(void (*pFunction)());

/**************************************************************************
 * ARM TLB IMPLEMENTATION
 **************************************************************************/

enum
{
	TLB_COARSE = 0,
	TLB_FINE
};

enum
{
	FAULT_NONE = 0,
	FAULT_DOMAIN,
	FAULT_PERMISSION
};

// COARSE, desc_level1, vaddr
static u32 get_lvl2_desc_from_page_table( u32 granularity, u32 first_desc, u32 vaddr )
{
	u32 desc_lvl2 = vaddr;

	switch( granularity )
	{
		case TLB_COARSE:
			desc_lvl2 = (first_desc & COPRO_TLB_CFLD_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_CSLTI_MASK) >> COPRO_TLB_VADDR_CSLTI_MASK_SHIFT);
			//if (m_tlb_log)
			//	LOGMASKED(LOG_TLB, "%s: get_lvl2_desc_from_page_table: coarse descriptor, lvl2 address is %08x\n", machine().describe_context(), desc_lvl2);
			break;
		case TLB_FINE:
			desc_lvl2 = (first_desc & COPRO_TLB_FPTB_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_FSLTI_MASK) >> COPRO_TLB_VADDR_FSLTI_MASK_SHIFT);
			//if (m_tlb_log)
			//	LOGMASKED(LOG_TLB, "%s: get_lvl2_desc_from_page_table: fine descriptor, lvl2 address is %08x\n", machine().describe_context(), desc_lvl2);
			break;
		default:
			// We shouldn't be here
			//LOGMASKED(LOG_MMU, "ARM7: Attempting to get second-level TLB descriptor of invalid granularity (%d)\n", granularity);
			break;
	}

	return arm_read_dword( desc_lvl2 );
}

static int detect_fault(int desc_lvl1, int ap, int flags)
{
	switch (arm_decoded_access_control[(desc_lvl1 >> 5) & 0xf])
	{
		case 0 : // "No access - Any access generates a domain fault"
		{
			return FAULT_DOMAIN;
		}
		case 1 : // "Client - Accesses are checked against the access permission bits in the section or page descriptor"
		{
			if ((ap & 3) == 3)
			{
				return FAULT_NONE;
			}
			else if (ap & 2)
			{
				if (((arm_reg[eCPSR] & MODE_FLAG) == eARM7_MODE_USER) && (flags & ARM7_TLB_WRITE))
				{
					return FAULT_PERMISSION;
				}
			}
			else if (ap & 1)
			{
				if ((arm_reg[eCPSR] & MODE_FLAG) == eARM7_MODE_USER)
				{
					return FAULT_PERMISSION;
				}
			}
			else
			{
				int s = (arm_control & COPRO_CTRL_SYSTEM) ? 1 : 0;
				int r = (arm_control & COPRO_CTRL_ROM) ? 1 : 0;
				if (s == 0)
				{
					if (r == 0) // "Any access generates a permission fault"
					{
						return FAULT_PERMISSION;
					}
					else // "Any write generates a permission fault"
					{
						if (flags & ARM7_TLB_WRITE)
						{
							return FAULT_PERMISSION;
						}
					}
				}
				else
				{
					if (r == 0) // "Only Supervisor read permitted"
					{
						if (((arm_reg[eCPSR] & MODE_FLAG) == eARM7_MODE_USER) || (flags & ARM7_TLB_WRITE))
						{
							return FAULT_PERMISSION;
						}
					}
					else // "Reserved" -> assume same behaviour as S=0/R=0 case
					{
						return FAULT_PERMISSION;
					}
				}
			}
		}
		break;
		case 2 : // "Reserved - Reserved. Currently behaves like the no access mode"
		{
			return FAULT_DOMAIN;
		}
		case 3 : // "Manager - Accesses are not checked against the access permission bits so a permission fault cannot be generated"
		{
			return FAULT_NONE;
		}
	}
	return FAULT_NONE;
}

static tlb_entry *tlb_map_entry(const u32 vaddr, const int flags)
{
	const u32 section = (vaddr >> (COPRO_TLB_VADDR_FLTI_MASK_SHIFT + 2)) & 0xFFF;
	tlb_entry *entries = (flags & ARM7_TLB_ABORT_D) ? m_dtlb_entries : m_itlb_entries;
	const u32 start = section << 1;
	u32 index = (flags & ARM7_TLB_ABORT_D) ? m_dtlb_entry_index[section] : m_itlb_entry_index[section];

	bool entry_found = false;

	for (u32 i = 0; i < 2; i++)
	{
		index = (index + 1) & 1;
		if (!entries[start + index].valid)
		{
			entry_found = true;
			break;
		}
	}

	if (!entry_found)
	{
		index = (index + 1) & 1;
	}

	if (flags & ARM7_TLB_ABORT_D)
		m_dtlb_entry_index[section] = index;
	else
		m_itlb_entry_index[section] = index;

	return &entries[start + index];
}

static tlb_entry *tlb_probe(const u32 vaddr, const int flags)
{
	const u32 section = (vaddr >> (COPRO_TLB_VADDR_FLTI_MASK_SHIFT + 2)) & 0xFFF;
	tlb_entry *entries = (flags & ARM7_TLB_ABORT_D) ? m_dtlb_entries : m_itlb_entries;
	const u32 start = section << 1;
	u32 index = (flags & ARM7_TLB_ABORT_D) ? m_dtlb_entry_index[section] : m_itlb_entry_index[section];

	//if (m_tlb_log)
	//	LOGMASKED(LOG_TLB, "%s: tlb_probe: vaddr %08x, section %02x, start %02x, index %d\n", machine().describe_context(), vaddr, section, start, index);

	for (u32 i = 0; i < 2; i++)
	{
		u32 position = start + index;
		if (entries[position].valid)
		{
			switch (entries[position].type)
			{
			case COPRO_TLB_TYPE_SECTION:
				if (entries[position].table_bits == (vaddr & COPRO_TLB_STABLE_MASK))
					return &entries[position];
				break;
			case COPRO_TLB_TYPE_LARGE:
			case COPRO_TLB_TYPE_SMALL:
				if (entries[position].table_bits == (vaddr & COPRO_TLB_LSTABLE_MASK))
					return &entries[position];
				break;
			case COPRO_TLB_TYPE_TINY:
				if (entries[position].table_bits == (vaddr & COPRO_TLB_TTABLE_MASK))
					return &entries[position];
				break;
			}
		}
		//if (m_tlb_log)
		//{
		//	LOGMASKED(LOG_TLB, "%s: tlb_probe: skipped due to mismatch (valid %d, domain %02x, access %d, table_bits %08x, base_addr %08x, type %d\n",
		//		machine().describe_context(), entries[position].valid ? 1 : 0, entries[position].domain, entries[position].access,
		//		entries[position].table_bits, entries[position].base_addr, entries[position].type);
		//}

		index = (index - 1) & 1;
	}

	return nullptr;
}

static u32 get_fault_from_permissions(const u8 access, const u8 domain, const u8 type, int flags)
{
	const u8 domain_bits = arm_decoded_access_control[domain];
	switch (domain_bits)
	{
		case COPRO_DOMAIN_NO_ACCESS:
			if (type == COPRO_TLB_TYPE_SECTION)
				return (domain << 4) | COPRO_FAULT_DOMAIN_SECTION;
			return (domain << 4) | COPRO_FAULT_DOMAIN_PAGE;
		case COPRO_DOMAIN_CLIENT:
		{
			const u32 mode = GET_CPSR & 0xF;
			switch (access)
			{
				case 0: // Check System/ROM bit
				{
					const u32 sr = (COPRO_CTRL >> COPRO_CTRL_SYSTEM_SHIFT) & 3;
					switch (sr)
					{
						case 0: // No Access
							if (type == COPRO_TLB_TYPE_SECTION)
								return (domain << 4) | COPRO_FAULT_PERM_SECTION;
							return (domain << 4) | COPRO_FAULT_PERM_PAGE;
						case 1: // No User Access, Read-Only System Access
							if (mode == 0 || (flags & ARM7_TLB_WRITE))
							{
								if (type == COPRO_TLB_TYPE_SECTION)
									return (domain << 4) | COPRO_FAULT_PERM_SECTION;
								return (domain << 4) | COPRO_FAULT_PERM_PAGE;
							}
							return COPRO_FAULT_NONE;
						case 2: // Read-Only Access
							if (flags & ARM7_TLB_WRITE)
							{
								if (type == COPRO_TLB_TYPE_SECTION)
									return (domain << 4) | COPRO_FAULT_PERM_SECTION;
								return (domain << 4) | COPRO_FAULT_PERM_PAGE;
							}
							return COPRO_FAULT_NONE;
						case 3: // Unpredictable Access
							//LOGMASKED(LOG_MMU, "%s: get_fault_from_permissions: Unpredictable access permissions (AP bits are 0, SR bits are 3).", machine().describe_context());
							return COPRO_FAULT_NONE;
					}
					return COPRO_FAULT_NONE;
				}
				case 1: // No User Access
					if (mode != 0)
						return COPRO_FAULT_NONE;
					if (type == COPRO_TLB_TYPE_SECTION)
						return (domain << 4) | COPRO_FAULT_PERM_SECTION;
					return (domain << 4) | COPRO_FAULT_PERM_PAGE;
				case 2: // Read-Only User Access
					if (mode != 0 || (flags & ARM7_TLB_READ))
						return COPRO_FAULT_NONE;
					if (type == COPRO_TLB_TYPE_SECTION)
						return (domain << 4) | COPRO_FAULT_PERM_SECTION;
					return (domain << 4) | COPRO_FAULT_PERM_PAGE;
				case 3: // Full Access
					return COPRO_FAULT_NONE;
			}
			return COPRO_FAULT_NONE;
		}
		case COPRO_DOMAIN_RESV:
			//LOGMASKED(LOG_MMU, "%s: get_fault_from_permissions: Domain type marked as Reserved.\n", machine().describe_context());
			return COPRO_FAULT_NONE;
		default:
			return COPRO_FAULT_NONE;
	}
}

static u32 tlb_check_permissions(tlb_entry *entry, const int flags)
{
	return get_fault_from_permissions(entry->access, entry->domain, entry->type, flags);
}

static u32 tlb_translate(tlb_entry *entry, const u32 vaddr)
{
	switch (entry->type)
	{
		case COPRO_TLB_TYPE_SECTION:
			return entry->base_addr | (vaddr & ~COPRO_TLB_SECTION_PAGE_MASK);
		case COPRO_TLB_TYPE_LARGE:
			return entry->base_addr | (vaddr & ~COPRO_TLB_LARGE_PAGE_MASK);
		case COPRO_TLB_TYPE_SMALL:
			return entry->base_addr | (vaddr & ~COPRO_TLB_SMALL_PAGE_MASK);
		case COPRO_TLB_TYPE_TINY:
			return entry->base_addr | (vaddr & ~COPRO_TLB_TINY_PAGE_MASK);
		default:
			return 0;
	}
}

static bool page_table_finish_translation(u32 &vaddr, const u8 type, const u32 lvl1, const u32 lvl2, const int flags, const u32 lvl1a, const u32 lvl2a)
{
	const u8 domain = (u8)(lvl1 >> 5) & 0xF;
	u8 access = 0;
	u32 table_bits = 0;
	switch (type)
	{
		case COPRO_TLB_TYPE_SECTION:
			access = (u8)((lvl2 >> 10) & 3);
			table_bits = vaddr & COPRO_TLB_STABLE_MASK;
			break;
		case COPRO_TLB_TYPE_LARGE:
		{
			const u8 subpage_shift = 4 + (u8)((vaddr >> 13) & 6);
			access = (u8)((lvl2 >> subpage_shift) & 3);
			table_bits = vaddr & COPRO_TLB_LSTABLE_MASK;
			break;
		}

		case COPRO_TLB_TYPE_SMALL:
		{
			const u8 subpage_shift = 4 + (u8)((vaddr >> 9) & 6);
			access = (u8)((lvl2 >> subpage_shift) & 3);
			table_bits = vaddr & COPRO_TLB_LSTABLE_MASK;
			break;
		}

		case COPRO_TLB_TYPE_TINY:
			access = (u8)((lvl2 >> 4) & 3);
			table_bits = vaddr & COPRO_TLB_TTABLE_MASK;
			break;
	}

	const u32 access_result = get_fault_from_permissions(access, domain, type, flags);
	if (access_result != 0)
	{
		if (flags & ARM7_TLB_ABORT_P)
		{
			//LOGMASKED(LOG_MMU, "ARM7: Page walk, Potential prefetch abort, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", vaddr, lvl1a, lvl1, lvl2a, lvl2);
		}
		else if (flags & ARM7_TLB_ABORT_D)
		{
			//LOGMASKED(LOG_MMU, "ARM7: Page walk, Data abort, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", vaddr, lvl1a, lvl1, lvl2a, lvl2);
			//LOGMASKED(LOG_MMU, "access: %d, domain: %d, type: %d\n", access, domain, type);
			arm_faultStatus[0] = access_result;
			arm_faultAddress = vaddr;
			arm_pendingAbtD = true;
			arm_update_irq_state();
		}
		return false;
	}

	static const u32 s_page_masks[4] = { COPRO_TLB_SECTION_PAGE_MASK, COPRO_TLB_LARGE_PAGE_MASK, COPRO_TLB_SMALL_PAGE_MASK, COPRO_TLB_TINY_PAGE_MASK };
	const u32 base_addr = lvl2 & s_page_masks[type];
	const u32 paddr = base_addr | (vaddr & ~s_page_masks[type]);

	if (flags)
	{
		tlb_entry *entry = tlb_map_entry(vaddr, flags);

		entry->valid = true;
		entry->domain = domain;
		entry->access = access;
		entry->table_bits = table_bits;
		entry->base_addr = base_addr;
		entry->type = type;
	}

	vaddr = paddr;
	return true;
}

static bool page_table_translate(u32 &vaddr, const int flags)
{
	const u32 lvl1_addr = arm_tlb_base_mask | ((vaddr & COPRO_TLB_VADDR_FLTI_MASK) >> COPRO_TLB_VADDR_FLTI_MASK_SHIFT);
	const u32 lvl1_desc = arm_read_dword(lvl1_addr);

	//LOGMASKED(LOG_MMU, "ARM7: Translating page table entry for %08x, lvl1_addr %08x, lvl1_desc %08x\n", vaddr, lvl1_addr, lvl1_desc);

	switch (lvl1_desc & 3)
	{
		case 0: // Unmapped
			//LOGMASKED(LOG_MMU, "ARM7: Translating page table entry for %08x, Unmapped, lvl1a %08x, lvl1d %08x\n", vaddr, lvl1_addr, lvl1_desc);
			if (flags & ARM7_TLB_ABORT_D)
			{
				//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (D), PC %08x, lvl1 unmapped, vaddr = %08x, lvl1A = %08x, lvl1D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc);
				arm_faultStatus[0] = COPRO_FAULT_TRANSLATE_SECTION;
				arm_faultAddress = vaddr;
				arm_pendingAbtD = true;
				arm_update_irq_state();
			}
			else if (flags & ARM7_TLB_ABORT_P)
			{
				//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (P), PC %08x, lvl1 unmapped, vaddr = %08x, lvl1A = %08x, lvl1D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc);
			}
			return false;

		case 1: // Coarse Table
		{
			const u32 lvl2_addr = (lvl1_desc & COPRO_TLB_CFLD_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_CSLTI_MASK) >> COPRO_TLB_VADDR_CSLTI_MASK_SHIFT);
			const u32 lvl2_desc = arm_read_dword(lvl2_addr);

			//LOGMASKED(LOG_MMU, "ARM7: Translating page table entry for %08x, Coarse, lvl1a %08x, lvl1d %08x, lvl2a %08x, lvl2d %08x\n", vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);

			switch (lvl2_desc & 3)
			{
				case 0: // Unmapped
					if (flags & ARM7_TLB_ABORT_D)
					{
						//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (D), coarse lvl2 unmapped, PC %08x, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);
						arm_faultStatus[0] = ((lvl1_desc >> 1) & 0xF0) | COPRO_FAULT_TRANSLATE_PAGE;
						arm_faultAddress = vaddr;
						arm_pendingAbtD = true;
						arm_update_irq_state();
					}
					else if (flags & ARM7_TLB_ABORT_P)
					{
						//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (P), coarse lvl2 unmapped, PC %08x, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);
					}
					return false;

				case 1: // Large Page
					return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_LARGE, lvl1_desc, lvl2_desc, flags, lvl1_addr, lvl2_addr);

				case 2: // Small Page
					return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_SMALL, lvl1_desc, lvl2_desc, flags, lvl1_addr, lvl2_addr);

				case 3: // Tiny Page (invalid)
					//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed, tiny page present in coarse lvl2 table, PC %08x, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);
					return false;
			}
			return false;
		}

		case 2: // Section Descriptor
			//LOGMASKED(LOG_MMU, "ARM7: Translating page table entry for %08x, Section, lvl1a %08x, lvl1d %08x\n", vaddr, lvl1_addr, lvl1_desc);
			return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_SECTION, lvl1_desc, lvl1_desc, flags, lvl1_addr, lvl1_addr);

		case 3: // Fine Table
		{
			const u32 lvl2_addr = (lvl1_desc & COPRO_TLB_FPTB_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_FSLTI_MASK) >> COPRO_TLB_VADDR_FSLTI_MASK_SHIFT);
			const u32 lvl2_desc = arm_read_dword(lvl2_addr);

			//LOGMASKED(LOG_MMU, "ARM7: Translating page table entry for %08x, Fine, lvl1a %08x, lvl1d %08x, lvl2a %08x, lvl2d %08x\n", vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);

			switch (lvl2_desc & 3)
			{
				case 0: // Unmapped
					if (flags & ARM7_TLB_ABORT_D)
					{
						//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (D), fine lvl2 unmapped, PC %08x, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);
						arm_faultStatus[0] = ((lvl1_desc >> 1) & 0xF0) | COPRO_FAULT_TRANSLATE_PAGE;
						arm_faultAddress = vaddr;
						arm_pendingAbtD = true;
						arm_update_irq_state();
					}
					else if (flags & ARM7_TLB_ABORT_P)
					{
						//LOGMASKED(LOG_MMU, "ARM7: Page Table Translation failed (P), fine lvl2 unmapped, PC %08x, vaddr = %08x, lvl1A = %08x, lvl1D = %08x, lvl2A = %08x, lvl2D = %08x\n", arm_reg[eR15], vaddr, lvl1_addr, lvl1_desc, lvl2_addr, lvl2_desc);
					}
					return false;

				case 1: // Large Page
					return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_LARGE, lvl1_desc, lvl2_desc, flags, lvl1_addr, lvl2_addr);

				case 2: // Small Page
					return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_SMALL, lvl1_desc, lvl2_desc, flags, lvl1_addr, lvl2_addr);

				case 3: // Tiny Page
					return page_table_finish_translation(vaddr, COPRO_TLB_TYPE_TINY, lvl1_desc, lvl2_desc, flags, lvl1_addr, lvl2_addr);
			}
			return false;
		}
	}

	return false;
}

static bool translate_vaddr_to_paddr(u32 &vaddr, const int flags)
{
	//if (m_tlb_log)
	//	LOGMASKED(LOG_TLB, "%s: translate_vaddr_to_paddr: vaddr %08x, flags %08x\n", machine().describe_context(), vaddr, flags);

	if (vaddr < 0x2000000)
	{
		vaddr += arm_pid_offset;
		//if (m_tlb_log)
		//	LOGMASKED(LOG_TLB, "%s: translate_vaddr_to_paddr: vaddr < 32M, adding PID (%08x) = %08x\n", machine().describe_context(), arm_pid_offset, vaddr);
	}

	tlb_entry *entry = tlb_probe(vaddr, flags);

	if (entry)
	{
		//if (m_tlb_log)
		//{
		//	LOGMASKED(LOG_TLB, "%s: translate_vaddr_to_paddr: found entry (domain %02x, access %d, table_bits %08x, base_addr %08x, type %d\n",
		//		machine().describe_context(), entry->domain, entry->access, entry->table_bits, entry->base_addr, entry->type);
		//}

		const u32 access_result = tlb_check_permissions(entry, flags);
		if (access_result == 0)
		{
			vaddr = tlb_translate(entry, vaddr);
			return true;
		}
		else if (flags & ARM7_TLB_ABORT_P)
		{
			//LOGMASKED(LOG_MMU, "ARM7: TLB, Potential prefetch abort, vaddr = %08x\n", vaddr);
		}
		else if (flags & ARM7_TLB_ABORT_D)
		{
			//LOGMASKED(LOG_MMU, "ARM7: TLB, Data abort, vaddr = %08x\n", vaddr);
			arm_faultStatus[0] = access_result;
			arm_faultAddress = vaddr;
			arm_pendingAbtD = true;
			arm_update_irq_state();
		}
		return false;
	}
	else
	{
		//if (m_tlb_log)
		//	LOGMASKED(LOG_MMU, "No TLB entry for %08x yet, running page_table_translate\n", vaddr);
		return page_table_translate(vaddr, flags);
	}
}

/***************************************************************************
 * CPU SPECIFIC IMPLEMENTATIONS
 **************************************************************************/

// CPU INIT
void arm_device_init(/*u32 clock, */u8 archRev, u32 archFlags, endian_t endianness)
{
	arm_prefetch_word0_shift = (endianness == ENDIANNESS_LITTLE ?  0 : 16);
	arm_prefetch_word1_shift = (endianness == ENDIANNESS_LITTLE ? 16 :  0);
	arm_endian = endianness;
	arm_archRev = archRev;
	arm_archFlags = archFlags;
	arm_vectorbase = 0;

	memset(arm_reg, 0, sizeof(arm_reg));

	u32 arch = ARM9_COPRO_ID_ARCH_V4;
	if (arm_archFlags & ARCHFLAG_T)
		arch = ARM9_COPRO_ID_ARCH_V4T;

	arm_copro_id = ARM9_COPRO_ID_MFR_ARM | arch | ARM9_COPRO_ID_PART_GENERICARM7;

	// TODO[RH]: Default to 3-instruction prefetch for unknown ARM variants.
	// Derived cores should set the appropriate value in their constructors.
	arm_insn_prefetch_depth = 3;

	memset(arm_insn_prefetch_buffer, 0, sizeof(arm_insn_prefetch_buffer));
	memset(arm_insn_prefetch_address, 0, sizeof(arm_insn_prefetch_address));
	memset(arm_insn_prefetch_valid, false, sizeof(arm_insn_prefetch_valid));
	arm_insn_prefetch_count = 0;
	arm_insn_prefetch_index = 0;
}

void arm946es_device_init()
{
	arm_device_init(5, ARCHFLAG_T | ARCHFLAG_E, ENDIANNESS_LITTLE);

	arm_copro_id = ARM9_COPRO_ID_MFR_ARM     | ARM9_COPRO_ID_ARCH_V5TE |
				   ARM9_COPRO_ID_PART_ARM946 | ARM9_COPRO_ID_STEP_ARM946_A0;

	memset(ITCM, 0, sizeof(ITCM));
	memset(DTCM, 0, sizeof(DTCM));

	cp15_control = 0x78;

	cp15_itcm_base = 0xffffffff;
	cp15_itcm_size = 0;
	cp15_itcm_end = 0;
	cp15_dtcm_base = 0xffffffff;
	cp15_dtcm_size = 0;
	cp15_dtcm_end = 0;
	cp15_itcm_reg = cp15_dtcm_reg = 0;
}

void arm_device_start()
{
	SCAN_VAR(arm_insn_prefetch_depth);
	SCAN_VAR(arm_insn_prefetch_count);
	SCAN_VAR(arm_insn_prefetch_index);
	SCAN_VAR(arm_insn_prefetch_buffer);
	SCAN_VAR(arm_insn_prefetch_address);
	SCAN_VAR(arm_insn_prefetch_valid);

	// CPU state
	SCAN_VAR(arm_reg);
	SCAN_VAR(arm_pendingIrq);
	SCAN_VAR(arm_pendingFiq);
	SCAN_VAR(arm_pendingAbtD);
	SCAN_VAR(arm_pendingAbtP);
	SCAN_VAR(arm_pendingUnd);
	SCAN_VAR(arm_pendingSwi);
	SCAN_VAR(arm_pending_interrupt);
	SCAN_VAR(arm_control);
	SCAN_VAR(arm_tlbBase);
	SCAN_VAR(arm_tlb_base_mask);
	SCAN_VAR(arm_faultStatus);
	SCAN_VAR(arm_faultAddress);
	SCAN_VAR(arm_fcsePID);
	SCAN_VAR(arm_pid_offset);
	SCAN_VAR(arm_domainAccessControl);
	SCAN_VAR(arm_decoded_access_control);

	SCAN_VAR(m_dtlb_entries);
	SCAN_VAR(m_itlb_entries);
	SCAN_VAR(m_dtlb_entry_index);
	SCAN_VAR(m_itlb_entry_index);

	arm_irq_trigger.scan();

	/* machine().save().register_postload(save_prepost_delegate(FUNC(arm7_cpu_device::postload), this)); */
	state_save_register_func_postload(update_reg_ptr);
	// 仅在 if (nAction & ACB_WRITE) 加载存档时调用，存档时 BurnAreaScan 会执行 3 次注册 回调函数 BurnPostload[update_reg_ptr]()
}

void arm946es_device_start()
{
	arm_device_start();

	SCAN_VAR(cp15_control);
	SCAN_VAR(cp15_itcm_base);
	SCAN_VAR(cp15_dtcm_base);
	SCAN_VAR(cp15_itcm_size);
	SCAN_VAR(cp15_dtcm_size);
	SCAN_VAR(cp15_itcm_end);
	SCAN_VAR(cp15_dtcm_end);
	SCAN_VAR(cp15_itcm_reg);
	SCAN_VAR(cp15_dtcm_reg);
	SCAN_VAR(ITCM);
	SCAN_VAR(DTCM);
}

// CPU RESET
void arm_device_reset()
{
	memset(arm_reg, 0, sizeof(arm_reg));
	arm_pendingIrq = false;
	arm_pendingFiq = false;
	arm_pendingAbtD = false;
	arm_pendingAbtP = false;
	arm_pendingUnd = false;
	arm_pendingSwi = false;
	arm_pending_interrupt = false;
	arm_control = 0;
	arm_tlbBase = 0;
	arm_tlb_base_mask = 0;
	arm_faultStatus[0] = 0;
	arm_faultStatus[1] = 0;
	arm_faultAddress = 0;
	arm_fcsePID = 0;
	arm_pid_offset = 0;
	arm_domainAccessControl = 0;
	memset(arm_decoded_access_control, 0, sizeof(arm_decoded_access_control));

	/* start up in SVC mode with interrupts disabled. */
	arm_reg[eCPSR] = I_MASK | F_MASK | 0x10;
	arm_switch_mode(eARM7_MODE_SVC);
	arm_reg[eR15] = 0 | arm_vectorbase;

	memset(m_dtlb_entries, 0, sizeof(m_dtlb_entries));
	memset(m_itlb_entries, 0, sizeof(m_itlb_entries));
	memset(m_dtlb_entry_index, 0, sizeof(m_dtlb_entry_index));
	memset(m_itlb_entry_index, 0, sizeof(m_itlb_entry_index));

	arm_irq_trigger.reset();
}

// CPU RUN
#define UNEXECUTED()	\
	arm_reg[eR15] += 4;	\
	arm_icount += 2;	/* Any unexecuted instruction only takes 1 cycle (page 193) */

static void update_insn_prefetch(u32 curr_pc)
{
	curr_pc &= ~3;
	if (arm_insn_prefetch_address[arm_insn_prefetch_index] != curr_pc)
	{
		//LOGMASKED(LOG_PREFETCH, "Prefetch addr %08x doesn't match curr_pc %08x, flushing prefetch buffer\n", arm_insn_prefetch_address[arm_insn_prefetch_index], curr_pc);
		arm_insn_prefetch_count = 0;
		arm_insn_prefetch_index = 0;
	}

	if (arm_insn_prefetch_count == arm_insn_prefetch_depth)
	{
		//LOGMASKED(LOG_PREFETCH, "We have prefetched up to the max depth, bailing\n");
		return;
	}

	const u32 to_fetch = arm_insn_prefetch_depth - arm_insn_prefetch_count;
	const u32 start_index = (arm_insn_prefetch_depth + (arm_insn_prefetch_index - to_fetch)) % arm_insn_prefetch_depth;
	//printf("need to prefetch %d instructions starting at index %d\n", to_fetch, start_index);

	//LOGMASKED(LOG_PREFETCH, "Need to fetch %d entries starting from index %d\n", to_fetch, start_index);
	u32 pc = curr_pc + arm_insn_prefetch_count * 4;
	for (u32 i = 0; i < to_fetch; i++)
	{
		u32 index = (i + start_index) % arm_insn_prefetch_depth;
		//LOGMASKED(LOG_PREFETCH, "About to get prefetch index %d from addr %08x\n", index, pc);
		arm_insn_prefetch_valid[index] = true;
		u32 physical_pc = pc;
		if ((arm_control & COPRO_CTRL_MMU_EN) && !translate_vaddr_to_paddr(physical_pc, ARM7_TLB_ABORT_P | ARM7_TLB_READ))
		{
			//LOGMASKED(LOG_PREFETCH, "Unable to fetch, bailing\n");
			arm_insn_prefetch_valid[index] = false;
			break;
		}
		u32 op = arm_cpu_readop32(physical_pc);
		//LOGMASKED(LOG_PREFETCH, "Got op %08x\n", op);
		//printf("ipb[%d] <- %08x(%08x)\n", index, op, pc);
		arm_insn_prefetch_buffer[index] = op;
		arm_insn_prefetch_address[index] = pc;
		arm_insn_prefetch_count++;
		pc += 4;
	}
}

static bool insn_fetch_thumb(u32 pc, u32 &out_insn)
{
	if (pc & 2)
	{
		out_insn = (u16)(arm_insn_prefetch_buffer[arm_insn_prefetch_index] >> arm_prefetch_word1_shift);
		bool valid = arm_insn_prefetch_valid[arm_insn_prefetch_index];
		arm_insn_prefetch_index = (arm_insn_prefetch_index + 1) % arm_insn_prefetch_depth;
		arm_insn_prefetch_count--;
		return valid;
	}
	out_insn = (u16)(arm_insn_prefetch_buffer[arm_insn_prefetch_index] >> arm_prefetch_word0_shift);
	return arm_insn_prefetch_valid[arm_insn_prefetch_index];
}

static bool insn_fetch_arm(u32 pc, u32 &out_insn)
{
	//printf("ipb[%d] = %08x\n", arm_insn_prefetch_index, arm_insn_prefetch_buffer[arm_insn_prefetch_index]);
	out_insn = arm_insn_prefetch_buffer[arm_insn_prefetch_index];
	bool valid = arm_insn_prefetch_valid[arm_insn_prefetch_index];
	//LOGMASKED(LOG_PREFETCH, "Fetched op %08x for PC %08x with %s entry from %08x\n", out_insn, pc, valid ? "valid" : "invalid", arm_insn_prefetch_address[arm_insn_prefetch_index]);
	arm_insn_prefetch_index = (arm_insn_prefetch_index + 1) % arm_insn_prefetch_depth;
	arm_insn_prefetch_count--;
	return valid;
}

void arm_execute_run()
{
	u32 insn;

	do
	{
		u32 pc = GET_PC;

		update_insn_prefetch(pc);

		/* handle Thumb instructions if active */
		if (T_IS_SET(arm_reg[eCPSR]))
		{
			u32 raddr;

			pc = arm_reg[eR15];

			// "In Thumb state, bit [0] is undefined and must be ignored. Bits [31:1] contain the PC."
			raddr = pc & (~1);

			if (!insn_fetch_thumb(raddr, insn))
			{
				arm_pendingAbtP = true;
				arm_update_irq_state();
				goto skip_exec;
			}
			(thumb_handler[(insn & 0xffc0) >> 6])(pc, insn);
		}
		else
		{
			u32 raddr;

			/* load 32 bit instruction */

			// "In ARM state, bits [1:0] of r15 are undefined and must be ignored. Bits [31:2] contain the PC."
			raddr = pc & (~3);

			if (!insn_fetch_arm(raddr, insn))
			{
				arm_pendingAbtP = true;
				arm_update_irq_state();
				goto skip_exec;
			}

			int op_offset = 0;
			/* process condition codes for this instruction */
			if ((insn >> INSN_COND_SHIFT) != COND_AL)
			{
				switch (insn >> INSN_COND_SHIFT)
				{
					case COND_EQ:
						if (Z_IS_CLEAR(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_NE:
						if (Z_IS_SET(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_CS:
						if (C_IS_CLEAR(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_CC:
						if (C_IS_SET(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_MI:
						if (N_IS_CLEAR(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_PL:
						if (N_IS_SET(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_VS:
						if (V_IS_CLEAR(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_VC:
						if (V_IS_SET(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_HI:
						if (C_IS_CLEAR(arm_reg[eCPSR]) || Z_IS_SET(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_LS:
						if (C_IS_SET(arm_reg[eCPSR]) && Z_IS_CLEAR(arm_reg[eCPSR]))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_GE:
						if (!(arm_reg[eCPSR] & N_MASK) != !(arm_reg[eCPSR] & V_MASK)) /* Use x ^ (x >> ...) method */
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_LT:
						if (!(arm_reg[eCPSR] & N_MASK) == !(arm_reg[eCPSR] & V_MASK))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_GT:
						if (Z_IS_SET(arm_reg[eCPSR]) || (!(arm_reg[eCPSR] & N_MASK) != !(arm_reg[eCPSR] & V_MASK)))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_LE:
						if (Z_IS_CLEAR(arm_reg[eCPSR]) && (!(arm_reg[eCPSR] & N_MASK) == !(arm_reg[eCPSR] & V_MASK)))
							{ UNEXECUTED();  goto skip_exec; }
						break;
					case COND_NV:
						if (arm_archRev < 5)
						  { UNEXECUTED();  goto skip_exec; }
						else
							op_offset = 0x10;
						break;
				}
			}
			/*******************************************************************/
			/* If we got here - condition satisfied, so decode the instruction */
			/*******************************************************************/
			(ops_handler[((insn & 0xF000000) >> 24) + op_offset])(insn);
		}

skip_exec:

		arm_check_irq_state();

		/* All instructions remove 3 cycles.. Others taking less / more will have adjusted this # prior to here */
		arm_icount -= 3;

		/* MAME: execute_timers() : timer callback */
		arm_irq_trigger.check();	// See if interrupts came in

	} while (arm_icount > 0 && !arm_end_run);
}

// CPU SET IRQ LINE
void arm_execute_set_input(int irqline, int state)	///static void arm7_core_set_irq_line(int irqline, int state)
{
	switch (irqline) {
	case ARM7_IRQ_LINE: /* IRQ */
		arm_pendingIrq = state ? true : false;
		break;

	case ARM7_FIRQ_LINE: /* FIRQ */
		arm_pendingFiq = state ? true : false;
		break;

	case ARM7_ABORT_EXCEPTION:
		arm_pendingAbtD = state ? true : false;
		break;
	case ARM7_ABORT_PREFETCH_EXCEPTION:
		arm_pendingAbtP = state ? true : false;
		break;

	case ARM7_UNDEFINE_EXCEPTION:
		arm_pendingUnd = state ? true : false;
		break;
	}

	arm_update_irq_state();
	arm_check_irq_state();
}

// CPU CHECK IRQ STATE
void arm_set_cpsr(u32 val)
{
	u8 old_mode = GET_CPSR & MODE_FLAG;
	bool call_hook = false;
	if (arm_archFlags & ARCHFLAG_MODE26)
	{
		if ((val & 0x10) != (arm_reg[eCPSR] & 0x10))
		{
			if (val & 0x10)
			{
				// 26 -> 32
				val = (val & 0x0FFFFF3F) | (arm_reg[eR15] & 0xF0000000) /* N Z C V */ | ((arm_reg[eR15] & 0x0C000000) >> (26 - 6)) /* I F */;
				arm_reg[eR15] = arm_reg[eR15] & 0x03FFFFFC;
			}
			else
			{
				// 32 -> 26
				arm_reg[eR15] = (arm_reg[eR15] & 0x03FFFFFC) /* PC */ | (val & 0xF0000000) /* N Z C V */ | ((val & 0x000000C0) << (26 - 6)) /* I F */ | (val & 0x00000003) /* M1 M0 */;
			}
			call_hook = true;
		}
		else
		{
			if (!(val & 0x10))
			{
				// mirror bits in pc
				arm_reg[eR15] = (arm_reg[eR15] & 0x03FFFFFF) | (val & 0xF0000000) /* N Z C V */ | ((val & 0x000000C0) << (26 - 6)) /* I F */;
			}
		}
	}
	else
	{
		val |= 0x10; // force valid mode
	}
	if ((val & T_MASK) != (arm_reg[eCPSR] & T_MASK))
		call_hook = true;
	arm_reg[eCPSR] = val;
	if ((GET_CPSR & MODE_FLAG) != old_mode)
	{
		if ((GET_CPSR & MODE_FLAG) == eARM7_MODE_USER || old_mode == eARM7_MODE_USER)
			call_hook = true;
		update_reg_ptr();
	}
}

void arm_update_irq_state()
{
	arm_pending_interrupt = arm_pendingAbtD || arm_pendingAbtP || arm_pendingUnd || arm_pendingSwi || arm_pendingFiq || arm_pendingIrq;
}

void arm_check_irq_state()	// Note: couldn't find any exact cycle counts for most of these exceptions
{
	if (!arm_pending_interrupt)
		return;

	u32 cpsr = arm_reg[eCPSR];   /* save current CPSR */
	u32 pc = arm_reg[eR15] + 4;      /* save old pc (already incremented in pipeline) */;

	/* Exception priorities:

		Reset
		Data abort
		FIRQ
		IRQ
		Prefetch abort
		Undefined instruction
		Software Interrupt
	*/

	// Data Abort
	if (arm_pendingAbtD)
	{
		//if (MODE26) fatalerror( "ARM7: pendingAbtD (todo)\n");
		arm_switch_mode(eARM7_MODE_ABT);             /* Set ABT mode so PC is saved to correct R14 bank */
		SetRegister(14, pc - 8 + 8);                   /* save PC to R14 */
		SetRegister(SPSR, cpsr);               /* Save current CPSR */
		arm_set_cpsr(GET_CPSR | I_MASK);            /* Mask IRQ */
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		R15 = 0x10;                             /* IRQ Vector address */
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		arm_pendingAbtD = false;
		arm_update_irq_state();
		return;
	}

	// FIQ
	if (arm_pendingFiq && (cpsr & F_MASK) == 0)
	{
		//if (MODE26) fatalerror( "pendingFiq (todo)\n");
		arm_switch_mode(eARM7_MODE_FIQ);             /* Set FIQ mode so PC is saved to correct R14 bank */
		SetRegister(14, pc - 4 + 4);                   /* save PC to R14 */
		SetRegister(SPSR, cpsr);               /* Save current CPSR */
		arm_set_cpsr(GET_CPSR | I_MASK | F_MASK);   /* Mask both IRQ & FIQ */
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		R15 = 0x1c;                             /* IRQ Vector address */
		R15 |= arm_vectorbase;
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		return;
	}

	// IRQ
	if (arm_pendingIrq && (cpsr & I_MASK) == 0)
	{
		arm_switch_mode(eARM7_MODE_IRQ);             /* Set IRQ mode so PC is saved to correct R14 bank */
		SetRegister(14, pc - 4 + 4);                   /* save PC to R14 */
		if (MODE32)
		{
			SetRegister(SPSR, cpsr);               /* Save current CPSR */
			arm_set_cpsr(GET_CPSR | I_MASK);            /* Mask IRQ */
			arm_set_cpsr(GET_CPSR & ~T_MASK);
			R15 = 0x18;                             /* IRQ Vector address */
		}
		else
		{
			u32 temp;
			R15 = (pc & 0xF4000000) /* N Z C V F */ | 0x18 | 0x00000002 /* IRQ */ | 0x08000000 /* I */;
			temp = (GET_CPSR & 0x0FFFFF3F) /* N Z C V I F */ | (R15 & 0xF0000000) /* N Z C V */ | ((R15 & 0x0C000000) >> (26 - 6)) /* I F */;
			arm_set_cpsr(temp);            /* Mask IRQ */
		}
		R15 |= arm_vectorbase;
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		return;
	}

	// Prefetch Abort
	if (arm_pendingAbtP)
	{
		//if (MODE26) fatalerror( "pendingAbtP (todo)\n");
		arm_switch_mode(eARM7_MODE_ABT);             /* Set ABT mode so PC is saved to correct R14 bank */
		SetRegister(14, pc - 4 + 4);                   /* save PC to R14 */
		SetRegister(SPSR, cpsr);               /* Save current CPSR */
		arm_set_cpsr(GET_CPSR | I_MASK);            /* Mask IRQ */
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		R15 = 0x0c | arm_vectorbase;                             /* IRQ Vector address */
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		arm_pendingAbtP = false;
		arm_update_irq_state();
		return;
	}

	// Undefined instruction
	if (arm_pendingUnd)
	{
		//if (MODE26) printf( "ARM7: pendingUnd (todo)\n");
		arm_switch_mode(eARM7_MODE_UND);             /* Set UND mode so PC is saved to correct R14 bank */
		// compensate for prefetch (should this also be done for normal IRQ?)
		if (T_IS_SET(GET_CPSR))
		{
			SetRegister(14, pc - 4 + 2);         /* save PC to R14 */
		}
		else
		{
			SetRegister(14, pc - 4 + 4 - 4);           /* save PC to R14 */
		}
		SetRegister(SPSR, cpsr);               /* Save current CPSR */
		arm_set_cpsr(GET_CPSR | I_MASK);            /* Mask IRQ */
		arm_set_cpsr(GET_CPSR & ~T_MASK);
		R15 = 0x04 | arm_vectorbase;                             /* IRQ Vector address */
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		arm_pendingUnd = false;
		arm_update_irq_state();
		return;
	}

	// Software Interrupt
	if (arm_pendingSwi)
	{
		arm_switch_mode(eARM7_MODE_SVC);             /* Set SVC mode so PC is saved to correct R14 bank */
		// compensate for prefetch (should this also be done for normal IRQ?)
		if (T_IS_SET(GET_CPSR))
		{
			SetRegister(14, pc - 4 + 2);         /* save PC to R14 */
		}
		else
		{
			SetRegister(14, pc - 4 + 4);           /* save PC to R14 */
		}
		if (MODE32)
		{
			SetRegister(SPSR, cpsr);               /* Save current CPSR */
			arm_set_cpsr(GET_CPSR | I_MASK);            /* Mask IRQ */
			arm_set_cpsr(GET_CPSR & ~T_MASK);           /* Go to ARM mode */
			R15 = 0x08;                             /* Jump to the SWI vector */
		}
		else
		{
			u32 temp;
			R15 = (pc & 0xF4000000) /* N Z C V F */ | 0x08 | 0x00000003 /* SVC */ | 0x08000000 /* I */;
			temp = (GET_CPSR & 0x0FFFFF3F) /* N Z C V I F */ | (R15 & 0xF0000000) /* N Z C V */ | ((R15 & 0x0C000000) >> (26 - 6)) /* I F */;
			arm_set_cpsr(temp);            /* Mask IRQ */
		}
		R15 |= arm_vectorbase;
		if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) && (COPRO_CTRL & COPRO_CTRL_INTVEC_ADJUST)) R15 |= 0xFFFF0000;
		arm_pendingSwi = false;
		arm_update_irq_state();
		return;
	}
}

static inline void update_reg_ptr()
{
	arm_reg_group = sRegisterTable[GET_MODE];
}

//void postload()	// 加载存档时
//{
//	update_reg_ptr();
//}

/***************************************************************************
 * ARM system coprocessor support
 ***************************************************************************/

void arm_do_callback(u32 data)
{
	arm_pendingUnd = true;
	arm_update_irq_state();
}

void arm_dt_r_callback(u32 insn, u32 *prn)
{
	u8 cpn = (insn >> 8) & 0xF;
	if ((arm_archFlags & ARCHFLAG_XSCALE) && (cpn == 0))
	{
		//LOGMASKED(LOG_DSP, "arm_dt_r_callback: DSP Coprocessor 0 (CP0) not yet emulated (PC %08x)\n", GET_PC);
	}
	else
	{
		arm_pendingUnd = true;
		arm_update_irq_state();
	}
}

void arm_dt_w_callback(u32 insn, u32 *prn)
{
	u8 cpn = (insn >> 8) & 0xF;
	if ((arm_archFlags & ARCHFLAG_XSCALE) && (cpn == 0))
	{
		//LOGMASKED(LOG_DSP, "arm_dt_w_callback: DSP Coprocessor 0 (CP0) not yet emulated (PC %08x)\n", GET_PC);
	}
	else
	{
		arm_pendingUnd = true;
		arm_update_irq_state();
	}
}

u32  arm946es_rt_r_callback(u32 offset)
{
	u32 opcode = offset;
	u8 cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	u8 op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	u8 op3 =    opcode & INSN_COPRO_OP3;
	u8 cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;
	u32 data = 0;

	//printf("arm7946: read cpnum %d cReg %d op2 %d op3 %d (%x)\n", cpnum, cReg, op2, op3, opcode);
	if (cpnum == 15)
	{
		switch( cReg )
		{
			case 0:
				switch (op2)
				{
					case 0: // chip ID
						data = 0x41059461;
						break;

					case 1: // cache ID
						data = 0x0f0d2112;
						break;

					case 2: // TCM size
						data = (6 << 6) | (5 << 18);
						break;
				}
				break;

			case 1:
				return cp15_control;
				break;

			case 9:
				if (op3 == 1)
				{
					if (op2 == 0)
					{
						return cp15_dtcm_reg;
					}
					else
					{
						return cp15_itcm_reg;
					}
				}
				break;
		}
	}

	return data;
}

void arm946es_rt_w_callback(u32 offset, u32 data)
{
	u32 opcode = offset;
	u8 cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	u8 op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	u8 op3 =    opcode & INSN_COPRO_OP3;
	u8 cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;

	//printf("arm7946: copro %d write %x to cReg %d op2 %d op3 %d (mask %08x)\n", cpnum, data, cReg, op2, op3, mem_mask);

	if (cpnum == 15)
	{
		switch (cReg)
		{
			case 1: // control
				cp15_control = data;
				arm946es_refresh_dtcm();
				arm946es_refresh_itcm();
				break;

			case 2: // Protection Unit cacheability bits
				break;

			case 3: // write bufferability bits for PU
				break;

			case 5: // protection unit region controls
				break;

			case 6: // protection unit region controls 2
				break;

			case 7: // cache commands
				break;

			case 9: // cache lockdown & TCM controls
				if (op3 == 1)
				{
					if (op2 == 0)
					{
						cp15_dtcm_reg = data;
						arm946es_refresh_dtcm();
					}
					else if (op2 == 1)
					{
						cp15_itcm_reg = data;
						arm946es_refresh_itcm();
					}
				}
				break;
		}
	}
}

static void arm946es_refresh_dtcm()
{
	if (cp15_control & (1<<16))
	{
		cp15_dtcm_base = (cp15_dtcm_reg & ~0xfff);
		cp15_dtcm_size = 512 << ((cp15_dtcm_reg & 0x3f) >> 1);
		cp15_dtcm_end = cp15_dtcm_base + cp15_dtcm_size;
		//printf("DTCM enabled: base %08x size %x\n", cp15_dtcm_base, cp15_dtcm_size);
	}
	else
	{
		cp15_dtcm_base = 0xffffffff;
		cp15_dtcm_size = cp15_dtcm_end = 0;
	}
}

static void arm946es_refresh_itcm()
{
	if (cp15_control & (1<<18))
	{
		cp15_itcm_base = 0; //(cp15_itcm_reg & ~0xfff);
		cp15_itcm_size = 512 << ((cp15_itcm_reg & 0x3f) >> 1);
		cp15_itcm_end = cp15_itcm_base + cp15_itcm_size;
		//printf("ITCM enabled: base %08x size %x\n", cp15_dtcm_base, cp15_dtcm_size);
	}
	else
	{
		cp15_itcm_base = 0xffffffff;
		cp15_itcm_size = cp15_itcm_end = 0;
	}
}

/***************************************************************************
 * Default Memory Handlers
 ***************************************************************************/

void arm946es_cpu_write32(u32 addr, u32 data)
{
	addr &= ~3;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		u32 *wp = (u32 *)&ITCM[addr&0x7fff];
		*wp = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		u32 *wp = (u32 *)&DTCM[addr&0x3fff];
		*wp = data;
		return;
	}

	arm_write_dword(addr, data);
}

void arm946es_cpu_write16(u32 addr, u16 data)
{
	addr &= ~1;
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		u16 *wp = (u16 *)&ITCM[addr&0x7fff];
		*wp = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		u16 *wp = (u16 *)&DTCM[addr&0x3fff];
		*wp = data;
		return;
	}

	arm_write_word(addr, data);
}

void arm946es_cpu_write8(u32 addr, u8 data)
{
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		ITCM[addr&0x7fff] = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		DTCM[addr&0x3fff] = data;
		return;
	}

	arm_write_byte(addr, data);
}

u32 arm946es_cpu_read32(u32 addr)
{
	u32 result;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		if (addr & 3)
		{
			u32 *wp = (u32 *)&ITCM[(addr & ~3)&0x7fff];
			result = rotr_32(*wp, 8 * (addr & 3));
		}
		else
		{
			u32 *wp = (u32 *)&ITCM[addr&0x7fff];
			result = *wp;
		}
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		if (addr & 3)
		{
			u32 *wp = (u32 *)&DTCM[(addr & ~3)&0x3fff];
			result = rotr_32(*wp, 8 * (addr & 3));
		}
		else
		{
			u32 *wp = (u32 *)&DTCM[addr&0x3fff];
			result = *wp;
		}
	}
	else
	{
		if (addr & 3)
		{
			result = rotr_32(arm_read_dword(addr & ~3), 8 * (addr & 3));
		}
		else
		{
			result = arm_read_dword(addr);
		}
	}
	return result;
}

u32 arm946es_cpu_read16(u32 addr)
{
	addr &= ~1;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		u16 *wp = (u16 *)&ITCM[addr & 0x7fff];
		return *wp;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		u16 *wp = (u16 *)&DTCM[addr &0x3fff];
		return *wp;
	}

	return arm_read_word(addr);
}

u8 arm946es_cpu_read8(u32 addr)
{
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		return ITCM[addr & 0x7fff];
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		return DTCM[addr & 0x3fff];
	}

	// Handle through normal 8 bit handler (for 32 bit cpu)
	return arm_read_byte(addr);
}

static inline u32 arm_cpu_readop32(u32 addr)	// m_pr32
{
	u32 result;

	//if (arm_endian == ENDIANNESS_LITTLE) // u32 { return m_cachele.read_dword(address); };
	//{
		if (addr & 3) {
			result = arm_fetch_dword(addr & ~3);
			result = (result >> (8 * (addr & 3))) | (result << (32 - (8 * (addr & 3))));
		} else {
			result = arm_fetch_dword(addr);
		}
	//}

	return result;
}
