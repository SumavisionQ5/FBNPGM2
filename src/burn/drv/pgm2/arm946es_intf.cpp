#include "driver.h"	// I/O line states
#include "mame_stuff.h"
#include "arm9.h"
#include "arm9core.h"

// -----------------------------------------------------------------------------
//   Memory Access Handlers
// -----------------------------------------------------------------------------

#define MAX_MEMORY		(1ULL << 32)		// unsigned long long
#define MEMORY_MASK		(MAX_MEMORY - 1)

#define PAGE_SHIFT		(8)
#define PAGE_SIZE		(1 << PAGE_SHIFT)	// 0x100 -> 8 bits
#define PAGE_MASK		(PAGE_SIZE - 1)
#define PAGE_COUNT		(MAX_MEMORY / PAGE_SIZE)

enum MemFlag { READ = 0, WRITE, FETCH, MemCount, INITIALIZE, RELEASE };
static u8 **MemMap[MemCount];				// 0 read, 1, write, 2 opcode

static s32 Arm9MapManage(MemFlag nAction)
{
	if (nAction == INITIALIZE) {
		for (s32 i = 0; i < MemCount; i++) {
			MemMap[i] = (u8 **)malloc(PAGE_COUNT * sizeof(u8 *));
			memset(MemMap[i], 0, PAGE_COUNT * sizeof(u8 *));
		}
	}
	else if (nAction == RELEASE) {
		for (s32 i = 0; i < MemCount; i++) {
			if (MemMap[i]) {
				free(MemMap[i]);
				MemMap[i] = NULL;
			}
		}
	}

	return 0;
}
void Arm9MapMemory(u8 *pMemory, u32 nStart, u32 nEnd, s32 nType)
{
	u32 len = (nEnd - nStart) >> PAGE_SHIFT;

	for (u32 i = 0; i < len + 1; i++)
	{
		u32 offset = i + (nStart >> PAGE_SHIFT);

		if (nType & MAP_READ)    MemMap[READ ][offset] = pMemory + (i << PAGE_SHIFT);
		if (nType & MAP_WRITE)   MemMap[WRITE][offset] = pMemory + (i << PAGE_SHIFT);
		if (nType & MAP_FETCHOP) MemMap[FETCH][offset] = pMemory + (i << PAGE_SHIFT);
	}
}
s32 Arm9MapHandler(uintptr_t nHandler, u32 nStart, u32 nEnd, s32 nType)
{
	u8 **pMemMapR = MemMap[READ ] + (nStart >> PAGE_SHIFT);
	u8 **pMemMapW = MemMap[WRITE] + (nStart >> PAGE_SHIFT);
	u8 **pMemMapF = MemMap[FETCH] + (nStart >> PAGE_SHIFT);

	for (u32 i = (nStart & ~PAGE_MASK); i <= nEnd; i += PAGE_SIZE, pMemMapR++, pMemMapW++, pMemMapF++)
	{
		if (nType & MAP_READ)    *pMemMapR = (u8*)nHandler;
		if (nType & MAP_WRITE)   *pMemMapW = (u8*)nHandler;
		if (nType & MAP_FETCHOP) *pMemMapF = (u8*)nHandler;
	}

	return 0;
}

static pReadByteHandler  ReadByte [MAX_HANDLER];
static pWriteByteHandler WriteByte[MAX_HANDLER];
static pReadWordHandler  ReadWord [MAX_HANDLER];
static pWriteWordHandler WriteWord[MAX_HANDLER];
static pReadLongHandler  ReadLong [MAX_HANDLER];
static pWriteLongHandler WriteLong[MAX_HANDLER];

s32 Arm9SetReadByteHandler (s32 i, pReadByteHandler  pHandler) { ReadByte [i] = pHandler; return 0; }
s32 Arm9SetWriteByteHandler(s32 i, pWriteByteHandler pHandler) { WriteByte[i] = pHandler; return 0; }
s32 Arm9SetReadWordHandler (s32 i, pReadWordHandler  pHandler) { ReadWord [i] = pHandler; return 0; }
s32 Arm9SetWriteWordHandler(s32 i, pWriteWordHandler pHandler) { WriteWord[i] = pHandler; return 0; }
s32 Arm9SetReadLongHandler (s32 i, pReadLongHandler  pHandler) { ReadLong [i] = pHandler; return 0; }
s32 Arm9SetWriteLongHandler(s32 i, pWriteLongHandler pHandler) { WriteLong[i] = pHandler; return 0; }

// -----------------------------------------------------------------------------
//   Memory Access Functions
// -----------------------------------------------------------------------------

/*inline */u8  arm_read_byte (u32 addr)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[READ][addr >> PAGE_SHIFT];	// Memory Page or nHandler index

	if ((uintptr_t)ptr >= MAX_HANDLER)
		return ptr[addr & PAGE_MASK];	// Direct RW
	else
		return ReadByte[(uintptr_t)ptr](addr);	// Handler RW
}
/*inline */u16 arm_read_word (u32 addr)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[READ][addr >> PAGE_SHIFT];

	if ((uintptr_t)ptr >= MAX_HANDLER)
		return BURN_ENDIAN_SWAP_INT16(*((u16 *)(ptr + (addr & PAGE_MASK))));
	else
		return ReadWord[(uintptr_t)ptr](addr);
}
/*inline */u32 arm_read_dword(u32 addr)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[READ][addr >> PAGE_SHIFT];

	if ((uintptr_t)ptr >= MAX_HANDLER)
		return BURN_ENDIAN_SWAP_INT32(*((u32 *)(ptr + (addr & PAGE_MASK))));
	else
		return ReadLong[(uintptr_t)ptr](addr);
}

/*inline */void arm_write_byte (u32 addr, u8  data)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[WRITE][addr >> PAGE_SHIFT];

	if ((uintptr_t)ptr >= MAX_HANDLER)
		ptr[addr & PAGE_MASK] = data;
	else
		WriteByte[(uintptr_t)ptr](addr, data);
}
/*inline */void arm_write_word (u32 addr, u16 data)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[WRITE][addr >> PAGE_SHIFT];

	if ((uintptr_t)ptr >= MAX_HANDLER)
		*((u16 *)(ptr + (addr & PAGE_MASK))) = (u16)BURN_ENDIAN_SWAP_INT16(data);
	else
		WriteWord[(uintptr_t)ptr](addr, data);
}
/*inline */void arm_write_dword(u32 addr, u32 data)
{
	addr &= MEMORY_MASK;
	u8 *ptr = MemMap[WRITE][addr >> PAGE_SHIFT];

	if ((uintptr_t)ptr >= MAX_HANDLER)
		*((u32 *)(ptr + (addr & PAGE_MASK))) = (u32)BURN_ENDIAN_SWAP_INT32(data);
	else
		WriteLong[(uintptr_t)ptr](addr, data);
}

/*inline */u32 arm_fetch_dword(u32 addr)
{
	u8 *ptr = MemMap[FETCH][addr >> PAGE_SHIFT];

	return BURN_ENDIAN_SWAP_INT32(*((u32 *)(ptr + (addr & PAGE_MASK))));
}

u32 Arm9GetPC(s32 nCPU)
{
	return R15 & 0x1fffffff;	// 29 bits
}

static u8 Arm9CheatRead(u32 addr) { return arm_read_byte(addr); }
static void Arm9CheatWrite(u32 addr, u8 data) { return arm_write_byte(addr, data); }
//{
//	u8 *ptr;
//	addr &= MEMORY_MASK;
//
//	// write to ram
//	ptr = MemMap[READ][addr >> PAGE_SHIFT];
//
//	if ((uintptr_t)ptr >= MAX_HANDLER)
//		ptr[addr & PAGE_MASK] = data;
//
//	// write to rom
//	ptr = MemMap[FETCH][addr >> PAGE_SHIFT];	// FETCH 以便加密游戏的运行，详见 m68000_intf.cpp -> WriteByteROM()
//
//	if ((uintptr_t)ptr >= MAX_HANDLER)
//		ptr[addr & PAGE_MASK] = data;
//	else
//		WriteByte[(uintptr_t)ptr](addr, data);
//}

// -----------------------------------------------------------------------------
//   CPU Interface
// -----------------------------------------------------------------------------

bool arm_end_run = false;			// end timeslice

static int total_cycles_frame = 0;	// FBN: total frame cycles executed
static int cycles_to_run = 0;
static int cycles_stolen = 0;		// number of cycles we artificially stole
static bool m_suspend = false;		// spin_until_interrupt

static u64 m_totalcycles = 0;		// MAME: total device cycles executed
static u32 m_cycles_per_second = 0;	// MAME: cycles per second, adjusted for multipliers


void Arm9Open(s32 nCPU)
{

}

void Arm9Close()
{

}

static s32 Arm9GetActive()	// Get the current CPU
{
	return 0;
}

s32 Arm9TotalCycles()
{
	return total_cycles_frame + cycles_to_run - arm_icount;
}

void Arm9NewFrame()
{
	cycles_to_run = arm_icount = 0;
	total_cycles_frame = 0;
}

static s32 Arm9Idle(s32 cycles)
{
	total_cycles_frame += cycles;
	m_totalcycles += cycles;

	return cycles;
}

void Arm9SetIRQLine(s32 line, s32 state)
{
	/* m_maincpu->set_input_line(int state, int state) */
	arm_irq_trigger.set(line, state);	// arm_execute_set_input(line, state);

	/* m_execute->signal_interrupt_trigger(); */
	if (state != CLEAR_LINE) { m_suspend = false; }
}

static void Arm9CheatSetIRQLine(s32/* cpu*/, s32 line, s32 state)
{
	Arm9SetIRQLine(line, state);
}

s32 Arm9Run(s32 cycles)
{
	/* if we're not m_suspended, actually execute */
	if (m_suspend) { return Arm9Idle(cycles); }	// CPU spin

	// int ran = exec->m_cycles_running = divu_64x32(u64(delta) >> exec->m_divshift, exec->m_divisor);
	cycles_to_run = arm_icount = cycles;	// set how many cycles we want to execute
	cycles_stolen = 0;	// note that this global variable cycles_stolen can be modified via the call to cpu_execute

	arm_end_run = false;
	arm_execute_run();	// execute some instructions until we run out of clock cycles

	cycles = cycles_to_run - arm_icount - cycles_stolen;	// adjust for any cycles we took back
	total_cycles_frame += cycles;
	m_totalcycles += cycles;	// account for these cycles

	return cycles;
}

s32 Arm9RunTime()
{
	/* a replacement for machine().time().seconds() */
	return ( m_totalcycles + cycles_to_run - arm_icount ) / m_cycles_per_second;
}

void Arm9BurnUntilInt()	// Kill cpu until interrupt
{
	/* m_maincpu->spin_until_interrupt(); */
	m_suspend = true;

	/* abort_timeslice(); */
	cycles_stolen += arm_icount;
	arm_icount = 0;
}

static void Arm9RunEnd()	// End the active CPU's timeslice
{
	arm_end_run = true;
}

void Arm9Reset()
{
	arm_device_reset();
	
	arm_end_run = false;
	total_cycles_frame = 0;
	cycles_to_run = 0;
	cycles_stolen = 0;
	m_suspend = false;

}

s32 Arm946esScan(s32 nAction)	// Savestate support
{
	if (nAction & ACB_DRIVER_DATA) {
		arm946es_device_start();
		
		SCAN_VAR(arm_end_run);
		SCAN_VAR(total_cycles_frame);
		SCAN_VAR(cycles_to_run);
		SCAN_VAR(cycles_stolen);
		SCAN_VAR(arm_icount);
		SCAN_VAR(m_suspend);
	}

	return 0;
}

void Arm9Exit()
{
	Arm9MapManage(RELEASE);
}


cpu_core_config Arm946esConfig =
{
	"Arm946es",
	Arm9Open,
	Arm9Close,

	Arm9CheatRead,
	Arm9CheatWrite,
	Arm9GetActive,
	Arm9TotalCycles,
	Arm9NewFrame,
	Arm9Idle,

	Arm9CheatSetIRQLine,

	Arm9Run,		// execute cycles
	Arm9RunEnd,
	Arm9Reset,
	//Arm946esScan,	// state handleing
	//Arm9Exit,

	MAX_MEMORY,		// how large is our memory range?
	0				// little endian
};

s32 Arm946esInit(s32 nClockspeed, s32 nCPU) // only one cpu supported
{
	m_cycles_per_second = nClockspeed;

	/* arm946es_cpu_device(mconfig, IGS036, tag, owner, clock, 5, ARCHFLAG_T | ARCHFLAG_E, ENDIANNESS_LITTLE) */
	arm946es_device_init();

	/* device_memory_interface::space_config_vector arm7_cpu_device::memory_space_config() */
	Arm9MapManage(INITIALIZE);

	CpuCheatRegister(nCPU, &Arm946esConfig);

	return 0;
}
