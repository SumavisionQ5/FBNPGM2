#pragma once

#ifndef MAME_ARM946ES_INTF_H
#define MAME_ARM946ES_INTF_H


/* Macros that can be re-defined for custom cpu implementations - The core expects these to be defined */
/* In this case, we are using the default arm7 handlers (supplied by the core)
   - but simply changes these and define your own if needed for cpu implementation specific needs */
#define READ8(addr)         arm946es_cpu_read8(addr)
#define WRITE8(addr,data)   arm946es_cpu_write8(addr,data)
#define READ16(addr)        arm946es_cpu_read16(addr)
#define WRITE16(addr,data)  arm946es_cpu_write16(addr,data)
#define READ32(addr)        arm946es_cpu_read32(addr)
#define WRITE32(addr,data)  arm946es_cpu_write32(addr,data)
#define PTR_READ32          &arm946es_cpu_read32
#define PTR_WRITE32         &arm946es_cpu_write32

/* CPU Interface */
extern bool arm_end_run;

void Arm9Open(s32 nCPU = 0);
s32 Arm946esInit(s32 nClockspeed, s32 nCPU = 0);

void Arm9NewFrame();
s32 Arm9TotalCycles();

s32 Arm9Run(s32 cycles);
s32 Arm9RunTime();
void Arm9BurnUntilInt();

void Arm9SetIRQLine(s32 line, s32 state);

s32 Arm946esScan(s32 nAction);
void Arm9Reset();

void Arm9Exit();
void Arm9Close();

/* Memory Access Handlers */
enum {MEM_HANDLER = 0, AIC_HANDLER, MCU_HANDLER, mRAM_HANDLER, sRAM_HANDLER, MAX_HANDLER};

void Arm9MapMemory(u8 *pMemory, u32 nStart, u32 nEnd, s32 nType);
s32 Arm9MapHandler(uintptr_t nHandler, u32 nStart, u32 nEnd, s32 nType);

typedef void(__fastcall *pWriteByteHandler)(u32, u8);
typedef void(__fastcall *pWriteWordHandler)(u32, u16);
typedef void(__fastcall *pWriteLongHandler)(u32, u32);

typedef u8 (__fastcall *pReadByteHandler)(u32);
typedef u16(__fastcall *pReadWordHandler)(u32);
typedef u32(__fastcall *pReadLongHandler)(u32);

s32 Arm9SetReadByteHandler (s32 i, pReadByteHandler  pHandler);
s32 Arm9SetWriteByteHandler(s32 i, pWriteByteHandler pHandler);

s32 Arm9SetReadWordHandler (s32 i, pReadWordHandler  pHandler);
s32 Arm9SetWriteWordHandler(s32 i, pWriteWordHandler pHandler);

s32 Arm9SetReadLongHandler (s32 i, pReadLongHandler  pHandler);
s32 Arm9SetWriteLongHandler(s32 i, pWriteLongHandler pHandler);

u32 Arm9GetPC(s32 nCPU = 0);

/* Memory Access Functions */
/*inline */u8  arm_read_byte (u32 addr);
/*inline */u16 arm_read_word (u32 addr);
/*inline */u32 arm_read_dword(u32 addr);

/*inline */void arm_write_byte (u32 addr, u8  data);
/*inline */void arm_write_word (u32 addr, u16 data);
/*inline */void arm_write_dword(u32 addr, u32 data);

/*inline */u32 arm_fetch_dword(u32 addr);

// depreciate this and use BurnTimerAttach directly!
extern struct cpu_core_config Arm946esConfig;
#define BurnTimerAttachArm946es(clock) BurnTimerAttach(&Arm946esConfig, clock)


#endif // MAME_ARM946ES_INTF_H
