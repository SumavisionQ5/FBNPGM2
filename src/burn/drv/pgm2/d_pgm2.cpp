// FinalBurn Neo IGS PGM2 System driver module
// Based on MAME (0.254) driver by David Haywood, Xing Xing, Andreas Naive

#include "driver.h"	// I/O line states
#include "burn_pal.h"
#include "tiles_generic.h"
#include "ymz770.h"
#include "mame_stuff.h"
#include "arm9core.h"	// ARM7_IRQ_LINE
#include "arm946es_intf.h"
#include "atmel_arm_aic.h"
#include "pgm2_memcard.h"

// -----------------------------------------------------------------------------
//   Header
// -----------------------------------------------------------------------------

#define IGS036_CLOCK			(100000000LL)	// Unknown clock / divider 100 mhz
#define IGS036_CYCS_PER_FRAME	((IGS036_CLOCK * 100) / nBurnFPS)

/* memory */
static u8 *AllMem, *MemEnd, *AllRam, *RamEnd;

static u8 *m_maincpu, *m_mainrom, *m_sram, *m_ymz774;
static u8 *m_mainram, *m_mcu_mem, *m_share_mem, *m_arm_aic;

u8 *m_default_card;

/* pgm2_draw */
static u8 *m_tiles, *m_bgtile, *m_sprites_mask, *m_sprites_colour;

static u32 *m_fg_videoram, *m_bg_videoram, *m_sp_videoram, *m_sp_zoom;
static u32 *m_tx_palette, *m_bg_palette, *m_sp_palette;

static u16 /*m_vidmode, */*m_lineram;

static struct { int x; int y; } m_fgscroll, m_bgscroll;

static struct spen_t 
{
	u16 color : 6;
	u16 palette : 6;
	u16 priority : 1;
	u16 do_not_draw : 1;
};
static spen_t *m_sprite_bitmap = NULL;	// 预绘精灵（存放像素颜色索引） 16bpp bitmaps

//static constexpr u32 bgtile_dim = 32, bgtile_bytes = bgtile_dim * bgtile_dim;	// 地图图块维度、图块占用字节
static constexpr u32 pixel_transparent = 0;	// 透明像素的颜色索引

/* graphics_length */
static u32 m_mainrom_size        = 0, m_ymz774_size        = 0;
static u32 m_tiles_size = 0x00400000, m_bgtile_size        = 0;
static u32 m_sprites_mask_size   = 0, m_sprites_mask_msk   = 0;
static u32 m_sprites_colour_size = 0, m_sprites_colour_msk = 0;

static constexpr u32 m_tx_palette_size  = 0x00000800, m_tx_palette_len  = m_tx_palette_size  / sizeof(m_tx_palette [0]);
static constexpr u32 m_fg_videoram_size = 0x00006000, m_fg_videoram_len = m_fg_videoram_size / sizeof(m_fg_videoram[0]);
static constexpr u32 m_bg_palette_size  = 0x00002000, m_bg_palette_len  = m_bg_palette_size  / sizeof(m_bg_palette [0]);
static constexpr u32 m_bg_videoram_size = 0x00002000, m_bg_videoram_len = m_bg_videoram_size / sizeof(m_bg_videoram[0]);
static constexpr u32 m_sp_palette_size  = 0x00004000, m_sp_palette_len  = m_sp_palette_size  / sizeof(m_sp_palette [0]);
static constexpr u32 m_sp_videoram_size = 0x00002000, m_sp_videoram_len = m_sp_videoram_size / sizeof(m_sp_videoram[0]);

static constexpr u32 nPgm2PaletteEntries = m_tx_palette_len + m_bg_palette_len + m_sp_palette_len;

/* 59.08Hz, 264 total lines @ 15.59KHz */
static constexpr double dFrameRate = 59.08;
static constexpr int nInterleave = 264;

/* 每秒 60 帧的扫描行数 除以 每秒的毫秒数或纳秒（取近似值，需要优化？） */
#define MSEC_TO_SCANLINE(msec)	((int)(msec * ((dFrameRate) * nInterleave / (1'000))))
#define USEC_TO_SCANLINE(usec)	((int)(usec * ((dFrameRate) * nInterleave / (1'000'000))))

static s32 nExtraCycles[1];

/* mcu */
static u8  m_shareram[0x100]{};
static u16 m_share_bank = 0;
static u32 m_mcu_regs[8]{};
static u32 m_mcu_result[2]{};
static u8  m_mcu_last_cmd = 0;
static s32 m_mcu_timer = 0;	// m_callback_timer_expire_time

/* optional_device_array<pgm2_memcard_device, 4> m_memcard; */
static constexpr int kMaxPlayer = 4;
Pgm2Memcard pgm2_memcard[kMaxPlayer];

/* input */
static constexpr int kJoyInputSize = 7;
static u8 Pgm2Joy[kJoyInputSize][8]{}, Pgm2Input[kJoyInputSize+1+1]{}, Pgm2Reset = 0;	// Joy + Dip + Region

static HoldCoin<kMaxPlayer> hold_coin;
static ClearOpposite<2, u8> clear_opposite;

static u32 nPgm2RegionHackAddress = 0;

/* callback */
static void (*pPgm2InitCallback)() = NULL;

/* stuff */
#define unk_startup_r()			((u32)(0x00000180));	// checked on startup, or doesn't boot
#define rtc_r()					Arm9RunTime()			// machine().time().seconds();
#define vblank_ack_w(...)		arm_aic_set_irq(12, CLEAR_LINE)

/* forward */
static void __fastcall mcu_command(bool is_command);
static void Pgm2DrawInit();
static void Pgm2DrawExit();
static int  Pgm2Draw();
static void draw_sprites_to_bitmap();

// -----------------------------------------------------------------------------
//   Share Ram
// -----------------------------------------------------------------------------

static void __fastcall share_bank_w(u32 offset, u16 data, u16 mem_mask = ~0)
{
	COMBINE_DATA(&m_share_bank);
}

static u8   __fastcall shareram_r(u32 offset)
{
	offset &= 0x000000ff; offset >>= 1;
	return m_shareram[offset + (m_share_bank & 1) * 128];
}
static void __fastcall shareram_w(u32 offset, u8 data)
{
	offset &= 0x000000ff; offset >>= 1;
	m_shareram[offset + (m_share_bank & 1) * 128] = data;
}

// -----------------------------------------------------------------------------
//   "MPU" MCU
// -----------------------------------------------------------------------------

//TIMER_DEVICE_CALLBACK_MEMBER(pgm2_state::mcu_interrupt)
#define mcu_interrupt(timer, param)	arm_aic_set_irq(3, ASSERT_LINE)

// "MPU" MCU HLE starts here
static u32  __fastcall mcu_r(u32 offset)
{
	offset &= 0x000fffff; offset >>= 2;
	return m_mcu_regs[(offset >> 15) & 7];
}

static void __fastcall mcu_w(u32 offset, u32 data)
{
	offset &= 0x000fffff; offset >>= 2;

	u32 mem_mask = ~0;
	int reg = (offset >> 15) & 7;
	COMBINE_DATA(&m_mcu_regs[reg]);

	if (reg == 2 && m_mcu_regs[2]) // irq to mcu
		mcu_command(true);
	if (reg == 5 && m_mcu_regs[5]) // ack to mcu (written at the end of irq handler routine)
	{
		mcu_command(false);
		arm_aic_set_irq(3, CLEAR_LINE);
	}
}

// command delays are far from correct, might not work in other games
// command results probably incorrect (except for explicit checked bytes)
static void __fastcall mcu_command(bool is_command)
{
	u8 const cmd = m_mcu_regs[0] & 0xff;
	//  if (is_command && cmd != 0xf6)
	//      logerror("MCU command %08x %08x\n", m_mcu_regs[0], m_mcu_regs[1]);

	if (is_command)
	{
		m_mcu_last_cmd = cmd;
		u8 status = 0xf7; // "command accepted" status
		int delay = 1;

		u8 arg1 = m_mcu_regs[0] >> 8  & 3;	// 玩家位
		u8 arg2 = m_mcu_regs[0] >> 16;
		u8 arg3 = m_mcu_regs[0] >> 24;
		switch (cmd)
		{
			case 0xf6:  // get result
				m_mcu_regs[3] = m_mcu_result[0];
				m_mcu_regs[4] = m_mcu_result[1];
				m_mcu_last_cmd = 0;
				break;
			case 0xe0: // command port test
				m_mcu_result[0] = m_mcu_regs[0];
				m_mcu_result[1] = m_mcu_regs[1];
				break;
			case 0xe1: // shared RAM access (unimplemented)
				{
					// MCU access to RAM shared at 0x30100000, 2x banks, in the same time CPU and MCU access different banks
					u8 mode = m_mcu_regs[0] >> 16; // 0 - ???, 1 - read, 2 - write
					u8 data = m_mcu_regs[0] >> 24;
					if (mode == 2)
					{
						// where is offset ? so far assume this command fill whole page
						memset(&m_shareram[(~m_share_bank & 1) * 128], data, 128);
					}
					m_mcu_result[0] = cmd;
					m_mcu_result[1] = 0;
				}
				break;
			// C0-C9 commands is IC Card RW comms
			case 0xc0: // insert card or/and check card presence. result: F7 - ok, F4 - no card
				if (pgm2_memcard[arg1].present() == -1)
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc1: // check ready/busy ?
				if (pgm2_memcard[arg1].present() == -1)
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc2: // read data to shared RAM
				for (int i = 0; i < arg3; i++)
				{
					if (pgm2_memcard[arg1].present() != -1)
						m_shareram[i + (~m_share_bank & 1) * 128] = pgm2_memcard[arg1].read(arg2 + i);
					else
						status = 0xf4;
				}
				m_mcu_result[0] = cmd;
				break;
			case 0xc3: // save data from shared RAM
				for (int i = 0; i < arg3; i++)
				{
					if (pgm2_memcard[arg1].present() != -1)
						pgm2_memcard[arg1].write(arg2 + i, m_shareram[i + (~m_share_bank & 1) * 128]);
					else
						status = 0xf4;
				}
				m_mcu_result[0] = cmd;
				break;
			case 0xc4: // presumable read security mem (password only?)
				if (pgm2_memcard[arg1].present() != -1)
				{
					m_mcu_result[1] =
						(pgm2_memcard[arg1].read_sec(1) <<  0) |
						(pgm2_memcard[arg1].read_sec(2) <<  8) |
						(pgm2_memcard[arg1].read_sec(3) << 16);
				}
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc5: // write security mem
				if (pgm2_memcard[arg1].present() != -1)
					pgm2_memcard[arg1].write_sec(arg2 & 3, arg3);
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc6: // presumable write protection mem
				if (pgm2_memcard[arg1].present() != -1)
					pgm2_memcard[arg1].write_prot(arg2 & 3, arg3);
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc7: // read protection mem
				if (pgm2_memcard[arg1].present() != -1)
				{
					m_mcu_result[1] =
						(pgm2_memcard[arg1].read_prot(0) <<  0) |
						(pgm2_memcard[arg1].read_prot(1) <<  8) |
						(pgm2_memcard[arg1].read_prot(2) << 16) |
						(pgm2_memcard[arg1].read_prot(3) << 24);
				}
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc8: // write data mem
				if (pgm2_memcard[arg1].present() != -1)
					pgm2_memcard[arg1].write(arg2, arg3);
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			case 0xc9: // card authentication
				if (pgm2_memcard[arg1].present() != -1)
					pgm2_memcard[arg1].auth(arg2, arg3, m_mcu_regs[1] & 0xff);
				else
					status = 0xf4;
				m_mcu_result[0] = cmd;
				break;
			default:
				//logerror("MCU unknown command %08x %08x\n", m_mcu_regs[0], m_mcu_regs[1]);
				status = 0xf4; // error
				break;
		}
		m_mcu_regs[3] = (m_mcu_regs[3] & 0xff00ffff) | (status << 16);
		m_mcu_timer = MSEC_TO_SCANLINE(delay);	// m_mcu_timer->adjust(attotime::from_msec(delay));
	}
	else // next step
	{
		if (m_mcu_last_cmd)
		{
			m_mcu_regs[3] = (m_mcu_regs[3] & 0xff00ffff) | 0x00F20000;  // set "command done and return data" status
			m_mcu_timer = USEC_TO_SCANLINE(100);	// m_mcu_timer->adjust(attotime::from_usec(100));
			m_mcu_last_cmd = 0;
		}
	}
}

// -----------------------------------------------------------------------------
//   Handler
// -----------------------------------------------------------------------------

static u8   __fastcall pgm2_read_byte (u32 offset)
{
	switch (offset)
	{
		case 0x40000000: case 0x40000001: case 0x40000002: case 0x40000003:
			return ymz774_read(offset & 3);
	}

	return 0;
}
static u16  __fastcall pgm2_read_word (u32 offset)
{
	switch (offset)
	{
		case 0x30120000: return m_bgscroll.x;
		case 0x30120002: return m_bgscroll.y;
		case 0x30120008: return m_fgscroll.x;
		case 0x3012000a: return m_fgscroll.y;
		//case 0x3012000e: return m_vidmode;
	}

	return 0;
}
static u32  __fastcall pgm2_read_long (u32 offset)
{
	switch (offset)
	{
		case 0x03900000: return ~(Pgm2Input[0] | Pgm2Input[1]<<8 | Pgm2Input[2]<<16 | Pgm2Input[3]<<24);
		case 0x03a00000: return ~(Pgm2Input[4] | Pgm2Input[5]<<8 | Pgm2Input[6]<<16 | Pgm2Input[7]<<24);

		case 0xfffffa0c: return unk_startup_r();
		case 0xfffffd28: return rtc_r();
	}

	return 0;
}

static void __fastcall pgm2_write_byte(u32 offset, u8  data)
{
	switch (offset)
	{
		case 0x40000000: case 0x40000001: case 0x40000002: case 0x40000003:
			ymz770_write(offset & 3, data); break;
	}
}
static void __fastcall pgm2_write_word(u32 offset, u16 data)
{
	switch (offset)
	{
		case 0x30120000: m_bgscroll.x = data; break;
		case 0x30120002: m_bgscroll.y = data; break;
		case 0x30120008: m_fgscroll.x = data; break;
		case 0x3012000a: m_fgscroll.y = data; break;
		//case 0x3012000e: m_vidmode    = data; break;

		case 0x30120018: vblank_ack_w(offset, data); break;
		case 0x30120032: share_bank_w(offset, data); break;
	}
}
static void __fastcall pgm2_write_long(u32 offset, u32 data)
{
	switch (offset)
	{
		//case 0x30120038: sprite_encryption_w(); break;
		//case 0xfffffa08: encryption_do_w(); break;
	}
}

// -----------------------------------------------------------------------------
//   Speed Up
// -----------------------------------------------------------------------------

static u8  __fastcall mainram_read_byte(u32 offset)
{
	u8 const* const byte_value = (u8*)(m_mainram + (offset & 0x7ffff));

	switch (offset)
	{
		case 0x20021e06:
			// u32 pgm2_state::ddpdojt_speedup2_r()
			if ( !strcmp(BurnDrvGetTextA(DRV_NAME), "ddpdojt") )
			{
				u32 const pc = Arm9GetPC();
				if ( (pc == 0x1008fefe) || (pc == 0x1008fbe8) )
				{
					if (byte_value) {
						Arm9BurnUntilInt();
					}
				}
			}
			break;
	}

	return byte_value[0];
}
static u16 __fastcall mainram_read_word(u32 offset)
{
	return *((u16*)(m_mainram + (offset & 0x7ffff)));
}
static u32 __fastcall mainram_read_long(u32 offset)
{
	u32 const* const dword_value = (u32*)(m_mainram + (offset & 0x7ffff));

	auto check_condition = [&](const char* drv_name, u32 pc_value) {
		return !strncmp(BurnDrvGetTextA(DRV_NAME), drv_name, strlen(drv_name)) &&
				Arm9GetPC() == pc_value &&
			   !dword_value[0] && !((u16*)dword_value)[2] && !((u16*)dword_value)[3];
	};	// Lambda 表达式

	switch (offset)
	{
		case 0x20020114:
			if ( check_condition("orleg2",     0x1002faec) ||	// orleg2_103, orleg2_104
				 check_condition("orleg2_101", 0x1002f9b8) ) {
				Arm9BurnUntilInt();
			}
			break;

		case 0x20020470:
			if ( check_condition("kov2nl",     0x10053a94) ||
				 check_condition("kov2nl_301", 0x1005332c) ||
				 check_condition("kov2nl_300", 0x1005327c) ) {
				Arm9BurnUntilInt();
			}
			break;

		case 0x20000060:
			if ( check_condition("ddpdojt",  0x10001a7e) ||
				 check_condition("kof98umh", 0x100028f6) ) {
				Arm9BurnUntilInt();
			}
			break;

		case 0x200000b4:
			if ( check_condition("kov3", 0x1000729a) ||		// kov3_102, kov3_101, kov3_100
				 check_condition("kov3", 0x1000729e) ) {	// kov3_104
				Arm9BurnUntilInt();
			}
			break;
	}

	return dword_value[0];
}

// -----------------------------------------------------------------------------
//   Machine Driver
// -----------------------------------------------------------------------------

// WRITE_LINE_MEMBER(pgm2_state::irq)
static void __fastcall pgm2_irq(s32 state)
{
	// m_maincpu->set_input_line(int linenum, int state)
	if (state == ASSERT_LINE)
		Arm9SetIRQLine(ARM7_IRQ_LINE, ASSERT_LINE);
	else
		Arm9SetIRQLine(ARM7_IRQ_LINE, CLEAR_LINE);
}

static void machine_start()
{
	SCAN_VAR(m_mcu_regs);
	SCAN_VAR(m_mcu_result);
	SCAN_VAR(m_mcu_last_cmd);
	SCAN_VAR(m_mcu_timer);

	SCAN_VAR(m_shareram);
	SCAN_VAR(m_share_bank);

}

static void machine_reset()
{
	m_mcu_last_cmd = 0;
	m_mcu_timer = 0;

	m_share_bank = 0;
}

// -----------------------------------------------------------------------------
//   Burn Driver
// -----------------------------------------------------------------------------

static int MemIndex() // 分配内存
{
	u8 *Next; Next = AllMem;
	
	{
		m_maincpu			= Next;	Next += 0x00004000;					// BIOS
		m_mainrom			= Next;	Next += m_mainrom_size;				// 主程序
		m_tiles				= Next;	Next += m_tiles_size;				// 文本砖图
		m_bgtile			= Next;	Next += m_bgtile_size;				// 瓦片地图
		m_sprites_mask		= Next;	Next += m_sprites_mask_size;		// 精灵遮罩
		m_sprites_colour	= Next;	Next += m_sprites_colour_size;		// 精灵颜色
		m_ymz774			= Next;	Next += m_ymz774_size;				// 声音采样

		if (BurnDrvGetHardwareCode() & HARDWARE_IGS_USE_MEM_CARD) {
			m_default_card	= Next;	Next += 0x00000108;					// 卡片模板 Memory card
		}
	}

		m_sram				= Next;	Next += 0x00010000;					// Battery backed SRAM

	AllRam = Next;
	
	{
		m_mainram			= Next;	Next += 0x00080000;					// 主内存

		m_mcu_mem			= Next;	Next += 0x000C0000;

		m_sp_videoram		= (u32*)Next;	Next += m_sp_videoram_size;	// 精灵显示内存
		m_bg_videoram		= (u32*)Next;	Next += m_bg_videoram_size;	// 背景显示内存
		m_fg_videoram		= (u32*)Next;	Next += m_fg_videoram_size;	// 前景显示内存

		{
			BurnPalette		= (u32*)Next;	// 连接 三个调色板

			m_sp_palette	= (u32*)Next;	Next += m_sp_palette_size;	// 精灵调色板	
			m_bg_palette	= (u32*)Next;	Next += m_bg_palette_size;	// 背景调色板	
			m_tx_palette	= (u32*)Next;	Next += m_tx_palette_size;	// 文本调色板	
		}

		m_sp_zoom			= (u32*)Next;	Next += 0x00000200;			// 精灵缩放表	
		m_lineram			= (u16*)Next;	Next += 0x00000400;			// 行滚动内存

		m_share_mem			= Next;	Next += 0x00000100;
		m_arm_aic			= Next;	Next += 0x0000014C;
	}

	RamEnd = Next;

	MemEnd = Next;

	return 0;
}

static int pgm2_get_roms(bool bLoad)
{
	char *pRomName;
	struct BurnRomInfo ri;	// 当前子 Rom

	u8 *m_mainrom_load          = m_mainrom;		// 主程序
	u8 *m_bgtile_load           = m_bgtile;			// 瓦片地图
	u8 *m_sprites_mask_load     = m_sprites_mask;	// 精灵遮罩
	u8 *m_sprites_colour_load   = m_sprites_colour;	// 精灵颜色
	u8 *m_ymz774_load           = m_ymz774;			// 声音采样

	for (int i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {

		BurnDrvGetRomInfo(&ri, i);	// 取当前子 Rom 信息

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 1)	// 1 | BRF_PRG 加载内部 ARM
		{
			if (bLoad) { BurnLoadRom(m_maincpu, i, 1); }
			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 2)	// 2 | BRF_PRG 加载外部 ARM
		{
			if (bLoad) { BurnLoadRom(m_mainrom_load, i, 1); } // 1、载入 Rom
			m_mainrom_load += ri.nLen;	// 0、下一内存 载入地址

			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 3)	// 3 | BRF_PRG 加载卡片模板
		{
			if (bLoad) { BurnLoadRom(m_default_card, i, 1); }
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 4)	// 4 | BRF_GRA 加载文本 Tile
		{
			if (bLoad) { BurnLoadRom(m_tiles, i, 1); }
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 5)	// 5 | BRF_GRA 加载背景 Tile
		{
			if (bLoad) { BurnLoadRom(m_bgtile_load, i, 1); }
			m_bgtile_load += ri.nLen;

			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 6)	// 6 | BRF_GRA 加载精灵遮罩
		{
			if (bLoad) { BurnLoadRom(m_sprites_mask_load, i, 1); }
			m_sprites_mask_load += ri.nLen;

			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 7)	// 7 | BRF_GRA 加载精灵颜色
		{ 
			if (bLoad) { BurnLoadRom(m_sprites_colour_load, i, 1); }
			m_sprites_colour_load += ri.nLen;

			continue;
		}

		if ((ri.nType & BRF_SND) && (ri.nType & 0x0f) == 8)	// 8 | BRF_SND 加载声音采样
		{
			if (bLoad) { BurnLoadRom(m_ymz774_load, i, 1); }
			m_ymz774_load += ri.nLen;

			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 9)	// 9 | BRF_PRG 加载 NV RAM
		{
			if (bLoad) { BurnLoadRom(m_sram, i, 1); }
		}
	}

	if (!bLoad) {	// 计算 Rom 区域大小

		m_mainrom_size = m_mainrom_load - m_mainrom;	// 主程序 大小

		m_bgtile_size = m_bgtile_load - m_bgtile;	// 瓦片地图 大小 32x32 BG Tiles

		{
			m_sprites_mask_size = m_sprites_mask_load - m_sprites_mask;	// 精灵遮罩 大小

			//m_sprites_mask_msk = (1 << (32 - __builtin_clz(m_sprites_mask_size - 1))) - 1;
			u32 needed = m_sprites_mask_size;
			m_sprites_mask_msk = 1;
			while (m_sprites_mask_msk < needed)
				m_sprites_mask_msk <<= 1;
			m_sprites_mask_msk -= 1;	// 精灵遮罩 掩码长度
		}

		{
			m_sprites_colour_size = m_sprites_colour_load - m_sprites_colour;	// 精灵颜色 大小

			//m_sprites_colour_msk = (1 << (32 - __builtin_clz(((m_sprites_colour_size + 1) & ~1) -1))) - 1;
			u32 needed = (m_sprites_colour_size + 1) & ~1;	// 每个字 (word) 含 2 个颜色
			m_sprites_colour_msk = 1;
			while (m_sprites_colour_msk < needed)
				m_sprites_colour_msk <<= 1;
			m_sprites_colour_msk -= 1;	// 精灵颜色 掩码长度
		}

		m_ymz774_size = m_ymz774_load - m_ymz774;	// 声音采样 大小
	}

	return 0;
}

static int pgm2_do_reset()
{
	memset(AllRam, 0, RamEnd - AllRam);

	machine_reset();

	Arm9Open ();
	Arm9Reset();
	Arm9Close();

	nExtraCycles[0] = 0;

	arm_aic_device_reset();	// 中断控制器

	ymz770_reset();	// 声音芯片

	hold_coin.reset();
	clear_opposite.reset();

	HiscoreReset();	// 高分系统

	if (nPgm2RegionHackAddress) {
		m_maincpu[nPgm2RegionHackAddress] = Pgm2Input[8];	// region hack
	}

	return 0;
}

static int Pgm2Init()
{
	/* 加载 ROM */
	{
		pgm2_get_roms(false);	// 计算 Rom 区域大小

		BurnAllocMemIndex();	// 分配总内存 MemIndex()、获取 AllMem 指针

		pgm2_get_roms(true);	// 载入 Rom 到对应内存区域 rom regions
	}

	/* 初始化 CPU */
	{
		Arm946esInit(IGS036_CLOCK);	// IGS036(config, m_maincpu, 100000000);
		Arm9Open();

		Arm9MapMemory(m_maincpu,          0x00000000, 0x00003fff, MAP_ROM);		// BIOS
		Arm9MapMemory(m_mainrom,          0x10000000, 0x10000000 + (m_mainrom_size-1), MAP_ROM);	// 主程序

		Arm9MapMemory(m_sram,             0x02000000, 0x0200ffff, MAP_RAM);		// Battery backed SRAM

		Arm9MapMemory(m_mainram,          0x20000000, 0x2007ffff, MAP_RAM);		// 主内存
		Arm9MapHandler(mRAM_HANDLER,      0x20000000, 0x2007ffff, MAP_READ);	// for speedup
		Arm9SetReadByteHandler (mRAM_HANDLER, mainram_read_byte);
		Arm9SetReadWordHandler (mRAM_HANDLER, mainram_read_word);
		Arm9SetReadLongHandler (mRAM_HANDLER, mainram_read_long);

		Arm9MapMemory (m_mcu_mem,         0x03600000, 0x036bffff, MAP_RAM);
		Arm9MapHandler(MCU_HANDLER,       0x03600000, 0x036bffff, MAP_READ | MAP_WRITE);
		Arm9SetReadLongHandler (MCU_HANDLER,  mcu_r);
		Arm9SetWriteLongHandler(MCU_HANDLER,  mcu_w);

		Arm9MapMemory((u8*)m_sp_videoram, 0x30000000, 0x30001fff, MAP_RAM);		// 精灵显示内存
		Arm9MapMemory((u8*)m_bg_videoram, 0x30020000, 0x30021fff, MAP_RAM);		// 背景显示内存
		Arm9MapMemory((u8*)m_fg_videoram, 0x30040000, 0x30045fff, MAP_RAM);		// 前景显示内存

		Arm9MapMemory((u8*)m_sp_palette,  0x30060000, 0x30063fff, MAP_RAM);		// 精灵调色板	
		Arm9MapMemory((u8*)m_bg_palette,  0x30080000, 0x30081fff, MAP_RAM);		// 背景调色板	
		Arm9MapMemory((u8*)m_tx_palette,  0x300a0000, 0x300a07ff, MAP_RAM);		// 文本调色板	

		Arm9MapMemory((u8*)m_sp_zoom,     0x300c0000, 0x300c01ff, MAP_RAM);		// 精灵缩放表

		for (s32 i = 0; i < 0x10000; i += 0x400) {								// 行滚动内存 + 镜像...
			Arm9MapMemory((u8*)m_lineram, 0x300e0000 | i, 0x300e03ff | i, MAP_RAM);
		}

		Arm9MapMemory (m_share_mem,       0x30100000, 0x301000ff, MAP_RAM);		// 卡片缓存
		Arm9MapHandler(sRAM_HANDLER,      0x30100000, 0x301000ff, MAP_READ | MAP_WRITE);
		Arm9SetReadByteHandler (sRAM_HANDLER, shareram_r);	// .umask32(0x00ff00ff);
		Arm9SetWriteByteHandler(sRAM_HANDLER, shareram_w);	// .umask32(0x00ff00ff);

		Arm9MapMemory (m_arm_aic,         0xfffff000, 0xfffff14b, MAP_RAM);		// 中断控制器
		Arm9MapHandler(AIC_HANDLER,       0xfffff000, 0xfffff14b, MAP_READ | MAP_WRITE);
		Arm9SetReadLongHandler (AIC_HANDLER,  arm_aic_regs_map_r);
		Arm9SetWriteLongHandler(AIC_HANDLER,  arm_aic_regs_map_w);

		Arm9SetReadByteHandler (MEM_HANDLER,  pgm2_read_byte );
		Arm9SetWriteByteHandler(MEM_HANDLER,  pgm2_write_byte);
		Arm9SetReadWordHandler (MEM_HANDLER,  pgm2_read_word );
		Arm9SetWriteWordHandler(MEM_HANDLER,  pgm2_write_word);
		Arm9SetReadLongHandler (MEM_HANDLER,  pgm2_read_long );
		Arm9SetWriteLongHandler(MEM_HANDLER,  pgm2_write_long);

		Arm9Close();

		// ARM_AIC(config, m_arm_aic, 0).irq_callback().set(FUNC(pgm2_state::irq));
		arm_aic_device_start(pgm2_irq);	// 中断控制器 回调函数
	}

	/* 初始化 图像声音 */
	{
		// m_screen->set_refresh(HZ_TO_ATTOSECONDS(59.08));
		BurnSetRefreshRate(dFrameRate);

		Pgm2DrawInit();	// void pgm2_state::video_start()

		//ymz774_device &ymz774(YMZ774(config, "ymz774", 16384000)); // is clock correct ?
		//ymz774.add_route(0, "lspeaker", 1.0);
		//ymz774.add_route(1, "rspeaker", 1.0);
		ymz774_init(m_ymz774, m_ymz774_size);	// 初始化 声音芯片
		ymz770_set_buffered(Arm9TotalCycles, IGS036_CLOCK);
	}

	if (pPgm2InitCallback) {
		pPgm2InitCallback();
	}

	pgm2_do_reset();

	return 0;
}

static int Pgm2Exit()
{
	BurnFreeMemIndex();

	Arm9Exit();
	Pgm2DrawExit();
	ymz770_exit();

	m_mainrom_size = 0;
	m_ymz774_size = 0;
	m_bgtile_size = 0;
	m_sprites_mask_size = 0;
	m_sprites_colour_size = 0;

	return 0;
}

static int Pgm2Frame()	// 帧刷新
{
	/* 输入检查 */
	{
		if (Pgm2Reset) { pgm2_do_reset(); }	// 按下 F3 重置系统

		// 重新加载输入
		memset(Pgm2Input, 0, kJoyInputSize);

		for (int i = 0; i < sizeof(Pgm2Joy[0]); i++) {
			for (int j = 0; j < kJoyInputSize; j++) {
				Pgm2Input[j] |= (Pgm2Joy[j][i] & 1) << i;
			}
		}

		// 检查投币按键
		hold_coin.check(0, Pgm2Input[5], 0b01000000, 7);	// Player1
		hold_coin.check(1, Pgm2Input[5], 0b10000000, 7);	// Player2
		hold_coin.check(2, Pgm2Input[6], 0b00000001, 7);	// Player3
		hold_coin.check(3, Pgm2Input[6], 0b00000010, 7);	// Player4

		// 清除相反方向（ddpdojt => CaveClearOpposites）
		clear_opposite.check(0, Pgm2Input[0], 0b00000011, 0b00001100);	// Player1
		clear_opposite.check(1, Pgm2Input[1], 0b00001100, 0b00110000);	// Player2
		clear_opposite.check(2, Pgm2Input[2], 0b00110000, 0b11000000);	// Player3
		clear_opposite.check(3, Pgm2Input[4], 0b00000011, 0b00001100);	// Player4
	}

	/* 执行设备 timeslice() */
	{
		Arm9NewFrame();
		Arm9Open();
	
		s32 nCyclesTotal[1] = { IGS036_CYCS_PER_FRAME };
		s32 nCyclesDone[1]  = { nExtraCycles[0] };
	
		for (int i = 0; i < nInterleave; i++)
		{
			/* nCyclesDone[0] += Arm9Run( ((i + 1) * nCyclesTotal[0] / nInterleave) - nCyclesDone[0] ); */
			CPU_RUN(0, Arm9);

			/* TIMER(config, m_mcu_timer, 0).configure_generic(FUNC(pgm2_state::mcu_interrupt)); */
			if ( m_mcu_timer > 0 && !(--m_mcu_timer) ) {
				mcu_interrupt();	// mcu_interrupt -> arm_aic_set_irq -> pgm2_irq
			}
		}

		nExtraCycles[0] = nCyclesDone[0] - nCyclesTotal[0];
		
		/* TIMER_CALLBACK_MEMBER(screen_device::vblank_begin) */
		{
			// machine().video().frame_update(); -> pgm2_state::screen_update()
			if (pBurnDraw) { BurnDrvRedraw(); }

			// m_screen_vblank(1); -> pgm2_state::screen_vblank()
			draw_sprites_to_bitmap();	// 先绘制
			arm_aic_set_irq(12, ASSERT_LINE);	// 后中断
		}

		if (pBurnSoundOut) { ymz770_update(pBurnSoundOut, nBurnSoundLen); }

		Arm9Close();
	}

	return 0;
}

static int Pgm2Scan(s32 nAction, s32 *pnMin)	// Savestate support
{
	// 启动（退出）游戏时 StateLenAcb() >> StateDecompressAcb()
	// 保存（加载）存档时 StateLenAcb() >> StateCompressAcb()

	struct BurnArea ba;

	if (pnMin) {	// Return minimum compatible version
		*pnMin = 0x029713;
	}

	if (nAction & ACB_MEMORY_ROM) {
		ba.Data     = m_maincpu;
		ba.nLen     = 0x00004000;
		ba.nAddress = 0x00000000;
		ba.szName   = "ARM BIOS";
		BurnAcb(&ba);

		ba.Data     = m_mainrom;
		ba.nLen     = m_mainrom_size;
		ba.nAddress = 0x10000000;
		ba.szName   = "ARM ROM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_MEMCARD) {
		for (int i = 0; i < kMaxPlayer; i++) {
			pgm2_memcard[i].device_start();
		}
	}

	// NVRAM(config, "sram", nvram_device::DEFAULT_ALL_0);
	if (nAction & ACB_NVRAM) {
		ba.Data     = m_sram;
		ba.nLen     = 0x00010000;
		ba.nAddress = 0x02000000;
		ba.szName   = "NVRAM";
		BurnAcb(&ba);	// StateLenAcb() >> StateDecompressAcb()
	}

	if (nAction & ACB_MEMORY_RAM) {
		ScanVar(AllRam, RamEnd - AllRam, "All RAM");
	}

	if (nAction & ACB_DRIVER_DATA) {
		machine_start();
		Arm946esScan(nAction);
		arm_aic_device_scan(nAction);
		ymz770_scan(nAction, pnMin);

		hold_coin.scan();
		clear_opposite.scan();
		
		//SCAN_VAR(m_vidmode);
		SCAN_VAR(m_fgscroll);
		SCAN_VAR(m_bgscroll);
		SCAN_VAR(nExtraCycles);
	}

	return 0;
}

// -----------------------------------------------------------------------------
//   Sprite Layer
// -----------------------------------------------------------------------------

static inline void draw_sprite_pixel(u32 palette_offset, s16 realx, s16 realy, spen_t pen)
{
	// 画面之内？X: 0 ~ 448-1, Y: 0 ~ 224-1
	if (realx >= 0 && realx < nScreenWidth && realy >= 0 && realy < nScreenHeight)
	{
		pen.color = m_sprites_colour[palette_offset];	// 64 色
		m_sprite_bitmap[realy * nScreenWidth + realx] = pen;	// 预绘制 精灵
	}
}

static inline void draw_sprite_chunk(u32 &palette_offset, s16 x, s16 realy, u16 sizex, int xdraw, spen_t pen, u32 maskdata, u32 zoomx_bits, u8 repeats, s16 &realxdraw, s8 realdraw_inc, s8 palette_inc)
{
	for (int xchunk = 0; xchunk < 32; xchunk++)
	{
		u8 pix, xzoombit;

		if (palette_inc == -1) {
			pix = BIT(maskdata, xchunk);
			xzoombit = BIT(zoomx_bits, xchunk);
		}
		else {
			pix = BIT(maskdata, 31 - xchunk);
			xzoombit = BIT(zoomx_bits, 31 - xchunk);
		}

		if (pix)	// 绘制像素
		{
			if (repeats)
			{
				// draw it the base number of times
				for (int i = 0; i < repeats; i++)
				{
					draw_sprite_pixel(palette_offset, x + realxdraw, realy, pen);
					realxdraw += realdraw_inc;
				}
			}

			{
				// draw it (again) if zoom bit is set
				if (xzoombit)
				{
					draw_sprite_pixel(palette_offset, x + realxdraw, realy, pen);
					realxdraw += realdraw_inc;
				}

				palette_offset += palette_inc;
				palette_offset &= m_sprites_colour_msk;
			}
		}
		else	// 空像素
		{
			if (repeats)
			{
				for (int i = 0; i < repeats; i++)
				{
					realxdraw += realdraw_inc;
				}
			}

			{
				if (xzoombit) realxdraw += realdraw_inc;
			}
		}
	}

}

static inline void draw_sprite_line(u32 &mask_offset, u32 &palette_offset, s16 x, s16 realy, bool flipx, bool reverse, u16 sizex, spen_t pen, u8 zoomybit, u32 zoomx_bits, u8 xrepeats)
{
	s16 realxdraw = 0;

	if (flipx ^ reverse) { realxdraw = (population_count_32(zoomx_bits) * sizex) - 1; }

	for (int xdraw = 0; xdraw < sizex; xdraw++)
	{
		u32 maskdata = *(u32*)&m_sprites_mask[mask_offset];	// 按 长字 取值

		if (reverse) { mask_offset -= 4; } else { mask_offset += 4; }
		mask_offset &= m_sprites_mask_msk;

		if (zoomybit)
		{
			if (!flipx)
			{
				if (!reverse) draw_sprite_chunk(palette_offset, x, realy, sizex, xdraw, pen, maskdata, zoomx_bits, xrepeats, realxdraw,  1,  1);
						 else draw_sprite_chunk(palette_offset, x, realy, sizex, xdraw, pen, maskdata, zoomx_bits, xrepeats, realxdraw, -1, -1);
			}
			else
			{
				if (!reverse) draw_sprite_chunk(palette_offset, x, realy, sizex, xdraw, pen, maskdata, zoomx_bits, xrepeats, realxdraw, -1,  1);
						 else draw_sprite_chunk(palette_offset, x, realy, sizex, xdraw, pen, maskdata, zoomx_bits, xrepeats, realxdraw,  1, -1);
			}
		}
		else
		{
			s32 bits = population_count_32(maskdata);

			if (!reverse) { palette_offset += bits; } else { palette_offset -= bits; }

			palette_offset &= m_sprites_colour_msk;
		}
	}
}

static void draw_sprites_to_bitmap()
{
	for (s32 i = 0; i < nScreenWidth * nScreenHeight; i++) {	// 重置画面
		m_sprite_bitmap[i].do_not_draw = 1;
	}

	for (int i = 0; i < m_sp_videoram_len; i += 4)	// 绘制精灵，每个精灵 0x10 大小，可用 位域结构 定义
	{
		if (m_sp_videoram[i + 2] & 0b10000000000000000000000000000000) { break; }		// 结束标志 0x80000000
		if (m_sp_videoram[i + 0] & 0b01000000000000000000000000000000) { continue; }	// 隐藏标志 0x40000000

		s16        x		= (m_sp_videoram[i + 0] & 0b00000000000000000000011111111111) >> 0;
		s16	       y		= (m_sp_videoram[i + 0] & 0b00000000001111111111100000000000) >> 11;
		u16  const pal		= (m_sp_videoram[i + 0] & 0b00001111110000000000000000000000) >> 22;
		u16  const pri		= (m_sp_videoram[i + 0] & 0b10000000000000000000000000000000) >> 31;
		//auto const unk0		= (m_sp_videoram[i + 0] & 0b00110000000000000000000000000000) >> 28;

		u16  const sizex	= (m_sp_videoram[i + 1] & 0b00000000000000000000000000111111) >> 0;
		u16  const sizey	= (m_sp_videoram[i + 1] & 0b00000000000000000111111111000000) >> 6;
		bool const flipx	= (m_sp_videoram[i + 1] & 0b00000000100000000000000000000000) >> 23;
		bool const reverse	= (m_sp_videoram[i + 1] & 0b10000000000000000000000000000000) >> 31;	// 与其说是 Y 翻转，不如说是“反转整个绘图”，但也用于此目的
		u8	 const zoomx	= (m_sp_videoram[i + 1] & 0b00000000011111110000000000000000) >> 16;
		u8   const zoomy	= (m_sp_videoram[i + 1] & 0b01111111000000000000000000000000) >> 24;
		//auto const unk1		= (m_sp_videoram[i + 1] & 0b00000000000000001000000000000000) >> 15;

		u32 mask_offset		= (m_sp_videoram[i + 2] << 1) & m_sprites_mask_msk;
		u32 palette_offset	= (m_sp_videoram[i + 3]) & m_sprites_colour_msk;


		if (x & 0x400) { x -= 0x800; }
		if (y & 0x400) { y -= 0x800; }

		spen_t pen = { 0, pal, pri, 0 };

		u32 const zoomy_bits = m_sp_zoom[zoomy];		// 使用 缩放的所有位 0b1111111 进行查找，
		u32 const zoomx_bits = m_sp_zoom[zoomx];		// 这可能就是为什么要 在内存中将表格复制四倍 0b11 的原因
		u8 const xrepeats = (zoomx & 0b1100000) >> 5;	// 但是 将这些位 用作 缩放倍数
		u8 const yrepeats = (zoomy & 0b1100000) >> 5;

		if (reverse) { mask_offset -= 2; }

		s16 realy = y;

		for (int ydraw = 0, sourceline = 0; ydraw < sizey; ydraw++, sourceline++)
		{
			u8 zoomy_bit = BIT(zoomy_bits, sourceline & 0b0011111);

			u32 const pre_palette_offset = palette_offset;	// 保存起来，以备 二次绘制时 使用
			u32 const pre_mask_offset = mask_offset;

			if (yrepeats)	// 放大绘制：重复行
			{
				for (int i = 0; i < yrepeats; i++)	// draw it the base number of times
				{
					palette_offset = pre_palette_offset;
					mask_offset = pre_mask_offset;
					draw_sprite_line(mask_offset, palette_offset, x, realy, flipx, reverse, sizex, pen, 1, zoomx_bits, xrepeats);

					realy++;
				}

				if (zoomy_bit) // draw it again if zoom bit is set
				{
					palette_offset = pre_palette_offset;
					mask_offset = pre_mask_offset;
					draw_sprite_line(mask_offset, palette_offset, x, realy, flipx, reverse, sizex, pen, 1, zoomx_bits, xrepeats);

					realy++;
				}
			}
			else			// 正常绘制
			{
				draw_sprite_line(mask_offset, palette_offset, x, realy, flipx, reverse, sizex, pen, 1, zoomx_bits, xrepeats);

				if (zoomy_bit)
				{
					realy++;
				}
			}
		}
	}
}

static void copy_sprites_from_bitmap(u16 pri)
{
	spen_t const* const srcptr_bitmap = m_sprite_bitmap;	// 预绘精灵 颜色索引
	u16* const dstptr_bitmap = pTransDraw;	// 传输精灵 颜色代码

	for (int i = 0; i < nScreenWidth * nScreenHeight; i++)
	{
		spen_t const pix = srcptr_bitmap[i];

		if (pix.do_not_draw == 0 && pix.priority == pri)	// 绘制否？优先级？
			dstptr_bitmap[i] = *(u16*)(&pix) & 0b0000111111111111;	// 存放像素颜色索引
	}
}

// -----------------------------------------------------------------------------
//   TX / BG Layer
// -----------------------------------------------------------------------------

// TILE_GET_INFO_MEMBER(pgm2_state::get_fg_tile_info)
static tilemap_callback( tx )
{
	u32 const tileno = (m_fg_videoram[offs] & 0b00000000000000111111111111111111) >> 0;
	u8  const colour = (m_fg_videoram[offs] & 0b00000000011111000000000000000000) >> 18;	// 32 色板
	u8  const flipxy = (m_fg_videoram[offs] & 0b00000001100000000000000000000000) >> 23;

	TILE_SET_INFO(0, tileno, colour, TILE_FLIPXY(flipxy));
}

//void fg_videoram_w(u32 offset, u32 data)
//{
//	u32 mem_mask = ~0;
//	COMBINE_DATA(&m_fg_videoram[offset]);
//	m_fg_tilemap->mark_tile_dirty(offset);
//}

// TILE_GET_INFO_MEMBER(pgm2_state::get_bg_tile_info)
static tilemap_callback( bg )
{
	u32 const tileno = (m_bg_videoram[offs] & 0b00000000000000111111111111111111) >> 0;
	u8  const colour = (m_bg_videoram[offs] & 0b00000000001111000000000000000000) >> 18;	// 16 色板
	u8  const flipxy = (m_bg_videoram[offs] & 0b00000001100000000000000000000000) >> 23;

	TILE_SET_INFO(1, tileno, colour, TILE_FLIPXY(flipxy));
}

//void bg_videoram_w(u32 offset, u32 data)
//{
//	u32 mem_mask = ~0;
//	COMBINE_DATA(&m_bg_videoram[offset]);
//	m_bg_tilemap->mark_tile_dirty(offset);
//}

// -----------------------------------------------------------------------------
//   Video - Start / Update
// -----------------------------------------------------------------------------

// pgm2_state::screen_update()
static int Pgm2Draw()
{
	BurnTransferClear(pixel_transparent);	// 重置画面
	
	int layer;

	/* 背景：精灵 */
	{
		layer = 1;

		copy_sprites_from_bitmap(layer);	// 背景精灵
	}

	/* 背景：地图 */
	{
		layer = 1;

		GenericTilemapSetScrollY(layer, m_bgscroll.y);	// scrolly

		for (s16 y = 0; y < nScreenHeight; y++) {
			GenericTilemapSetScrollRow(layer, (y + m_bgscroll.y) & 0x3ff, m_bgscroll.x + m_lineram[y]);
		}	// scrollx_table

		GenericTilemapDraw(layer, 0, 0);	// 背景地图
	}

	/* 中景：精灵 */
	{
		layer = 0;

		copy_sprites_from_bitmap(layer);	// 中景精灵
	}

	/* 前景：文本 */
	{
		layer = 0;

		GenericTilemapSetScrollX(layer, m_fgscroll.x);	// scrollx
		GenericTilemapSetScrollY(layer, m_fgscroll.y);	// scrolly

		GenericTilemapDraw(layer, 0, 0);	// 前景文本
	}
	
	// GenericTiles: pTransDraw > pBurnDraw
	BurnTransferCopy(BurnPalette);			// 绘制画面

	return 0;
}

// pgm2_state::video_start()
static void Pgm2DrawInit()
{
	GenericTilesInit();

	{
		const u32 planes = 4,	// 1 << 4 = 16 色
				  mask  = m_tx_palette_len / (1<<planes) - 1,	// 32 色板
				  offs  = m_tx_palette - BurnPalette;	// 调色板 偏移地址
		GenericTilesSetGfx(0, m_tiles, planes,8,8, m_tiles_size, offs, mask);

		GenericTilemapInit(0, TILEMAP_SCAN_ROWS, tx_map_callback, 8,8,   96,64);
		GenericTilemapSetTransparent(0, pixel_transparent);	// m_fg_tilemap->set_transparent_pen(0);
		GenericTilemapBuildSkipTable(0, 0, pixel_transparent);
	}
	{
		const u32 planes = 7,	// 1 << 7 = 128 色
				  mask  = m_bg_palette_len / (1<<planes) - 1,	// 16 色板
				  offs  = m_bg_palette - BurnPalette;	// 调色板 偏移地址
		GenericTilesSetGfx(1, m_bgtile, planes,32,32, m_bgtile_size, offs, mask);

		GenericTilemapInit(1, TILEMAP_SCAN_ROWS, bg_map_callback, 32,32, 64,32);	// scan: scrollx_table?
		GenericTilemapSetTransparent(1, pixel_transparent);	// m_bg_tilemap->set_transparent_pen(0);
		GenericTilemapSetScrollRows (1, 32*32);	// m_bg_tilemap->set_scroll_rows(32 * 32);
		GenericTilemapBuildSkipTable(1, 1, pixel_transparent);
	}
	{	// m_screen->register_screen_bitmap(m_sprite_bitmap);
		m_sprite_bitmap = (spen_t*)BurnMalloc(nScreenWidth * nScreenHeight * sizeof(m_sprite_bitmap[0]));
	}
}

static void Pgm2DrawExit()
{
	BurnFree(m_sprite_bitmap);

	GenericTilesExit();
}

// -----------------------------------------------------------------------------
//   Input Ports
// -----------------------------------------------------------------------------

static struct BurnInputInfo Pgm2InputList[] = {
	{"P1 Coin",			BIT_DIGITAL,	Pgm2Joy[5] + 6,	"p1 coin"	},
	{"P1 Start",		BIT_DIGITAL,	Pgm2Joy[5] + 2,	"p1 start"	},
	{"P1 Up",			BIT_DIGITAL,	Pgm2Joy[0] + 0,	"p1 up"		},
	{"P1 Down",			BIT_DIGITAL,	Pgm2Joy[0] + 1,	"p1 down"	},
	{"P1 Left",			BIT_DIGITAL,	Pgm2Joy[0] + 2,	"p1 left"	},
	{"P1 Right",		BIT_DIGITAL,	Pgm2Joy[0] + 3,	"p1 right"	},
	{"P1 Button 1",		BIT_DIGITAL,	Pgm2Joy[0] + 4,	"p1 fire 1"	},
	{"P1 Button 2",		BIT_DIGITAL,	Pgm2Joy[0] + 5,	"p1 fire 2"	},
	{"P1 Button 3",		BIT_DIGITAL,	Pgm2Joy[0] + 6,	"p1 fire 3"	},
	{"P1 Button 4",		BIT_DIGITAL,	Pgm2Joy[0] + 7,	"p1 fire 4"	},
	//{"P1 Button 5",		BIT_DIGITAL,	Pgm2Joy[1] + 0,	"p1 fire 5"	},
	//{"P1 Button 6",		BIT_DIGITAL,	Pgm2Joy[1] + 1,	"p1 fire 6"	},
	//{"P1 Button 7",		BIT_DIGITAL,	Pgm2Joy[5] + 0,	"p1 fire 7"	},
	
	{"P2 Coin",			BIT_DIGITAL,	Pgm2Joy[5] + 7,	"p2 coin"	},
	{"P2 Start",		BIT_DIGITAL,	Pgm2Joy[5] + 3,	"p2 start"	},
	{"P2 Up",			BIT_DIGITAL,	Pgm2Joy[1] + 2,	"p2 up"		},
	{"P2 Down",			BIT_DIGITAL,	Pgm2Joy[1] + 3,	"p2 down"	},
	{"P2 Left",			BIT_DIGITAL,	Pgm2Joy[1] + 4,	"p2 left"	},
	{"P2 Right",		BIT_DIGITAL,	Pgm2Joy[1] + 5,	"p2 right"	},
	{"P2 Button 1",		BIT_DIGITAL,	Pgm2Joy[1] + 6,	"p2 fire 1"	},
	{"P2 Button 2",		BIT_DIGITAL,	Pgm2Joy[1] + 7,	"p2 fire 2"	},
	{"P2 Button 3",		BIT_DIGITAL,	Pgm2Joy[2] + 0,	"p2 fire 3"	},
	{"P2 Button 4",		BIT_DIGITAL,	Pgm2Joy[2] + 1,	"p2 fire 4"	},
	//{"P2 Button 5",		BIT_DIGITAL,	Pgm2Joy[2] + 2,	"p2 fire 5"	},
	//{"P2 Button 6",		BIT_DIGITAL,	Pgm2Joy[2] + 3,	"p2 fire 6"	},
	//{"P2 Button 7",		BIT_DIGITAL,	Pgm2Joy[5] + 1,	"p2 fire 7"	},
	
	{"P3 Coin",			BIT_DIGITAL,	Pgm2Joy[6] + 0,	"p3 coin"	},
	{"P3 Start",		BIT_DIGITAL,	Pgm2Joy[5] + 4,	"p3 start"	},
	{"P3 Up",			BIT_DIGITAL,	Pgm2Joy[2] + 4,	"p3 up"		},
	{"P3 Down",			BIT_DIGITAL,	Pgm2Joy[2] + 5,	"p3 down"	},
	{"P3 Left",			BIT_DIGITAL,	Pgm2Joy[2] + 6,	"p3 left"	},
	{"P3 Right",		BIT_DIGITAL,	Pgm2Joy[2] + 7,	"p3 right"	},
	{"P3 Button 1",		BIT_DIGITAL,	Pgm2Joy[3] + 0,	"p3 fire 1"	},
	{"P3 Button 2",		BIT_DIGITAL,	Pgm2Joy[3] + 1,	"p3 fire 2"	},
	{"P3 Button 3",		BIT_DIGITAL,	Pgm2Joy[3] + 2,	"p3 fire 3"	},
	{"P3 Button 4",		BIT_DIGITAL,	Pgm2Joy[3] + 3,	"p3 fire 4"	},
	//{"P3 Button 5",		BIT_DIGITAL,	Pgm2Joy[3] + 4,	"p3 fire 5"	},
	//{"P3 Button 6",		BIT_DIGITAL,	Pgm2Joy[3] + 5,	"p3 fire 6"	},
	//{"P3 Button 7",		BIT_DIGITAL,	Pgm2Joy[6] + 6,	"p3 fire 7"	},
	
	{"P4 Coin",			BIT_DIGITAL,	Pgm2Joy[6] + 1,	"p4 coin"	},
	{"P4 Start",		BIT_DIGITAL,	Pgm2Joy[5] + 5,	"p4 start"	},
	{"P4 Up",			BIT_DIGITAL,	Pgm2Joy[4] + 0,	"p4 up"		},
	{"P4 Down",			BIT_DIGITAL,	Pgm2Joy[4] + 1,	"p4 down"	},
	{"P4 Left",			BIT_DIGITAL,	Pgm2Joy[4] + 2,	"p4 left"	},
	{"P4 Right",		BIT_DIGITAL,	Pgm2Joy[4] + 3,	"p4 right"	},
	{"P4 Button 1",		BIT_DIGITAL,	Pgm2Joy[4] + 4,	"p4 fire 1"	},
	{"P4 Button 2",		BIT_DIGITAL,	Pgm2Joy[4] + 5,	"p4 fire 2"	},
	{"P4 Button 3",		BIT_DIGITAL,	Pgm2Joy[4] + 6,	"p4 fire 3"	},
	{"P4 Button 4",		BIT_DIGITAL,	Pgm2Joy[4] + 7,	"p4 fire 4"	},
	//{"P4 Button 5",		BIT_DIGITAL,	Pgm2Joy[3] + 6,	"p4 fire 5"	},
	//{"P4 Button 6",		BIT_DIGITAL,	Pgm2Joy[3] + 7,	"p4 fire 6"	},
	//{"P4 Button 7",		BIT_DIGITAL,	Pgm2Joy[6] + 7,	"p4 fire 7"	},
	
	{"Reset",			BIT_DIGITAL,	&Pgm2Reset,		"reset"		},
	{"Test Key P1 & P2",BIT_DIGITAL,	Pgm2Joy[6] + 2,	"diag"		},
	{"Test Key P3 & P4",BIT_DIGITAL,	Pgm2Joy[6] + 3,	"diag2"		},
	{"Service P1 & P2",	BIT_DIGITAL,	Pgm2Joy[6] + 4,	"service"	},
	{"Service P3 & P4",	BIT_DIGITAL,	Pgm2Joy[6] + 5,	"service2"	},

	{"DIP Switch",		BIT_DIPSWITCH,	Pgm2Input + 7,	"dip"		},
	{"Region Switch",	BIT_DIPSWITCH,	Pgm2Input + 8,	"dip"		},
};

STDINPUTINFO(Pgm2)

#define DIPSW_IDX	(((sizeof(Pgm2InputList))/(sizeof(BurnInputInfo)))-2)
#define REGSW_IDX	(((sizeof(Pgm2InputList))/(sizeof(BurnInputInfo)))-1)

static struct BurnDIPInfo Pgm2DIPList[] = {
	// nFlags == 0xFF 表示第 nInput 个 BurnInputInfo 的 DIP 内存初始值为 nSetting
	{DIPSW_IDX,	0xFF, 0xFF,	0x00, NULL				},
	// nInput,nFlags,nMask,nSetting,szText

	// nFlags == 0xFE 表示当前 DIP 开关有 nSetting 个选项，详见 inpdipsw.cpp
	{0,			0xFE, 0,	2,    "Service Mode"	},
	{DIPSW_IDX,	0x01, 0x01,	0x00, "Off"				},
	{DIPSW_IDX,	0x01, 0x01,	0x01, "On"				},

	{0,			0xFE, 0,	2,    "Music"			},
	{DIPSW_IDX,	0x01, 0x02,	0x00, "On"				},
	{DIPSW_IDX,	0x01, 0x02,	0x02, "Off"				},

	{0,			0xFE, 0,	2,    "Voice"			},
	{DIPSW_IDX,	0x01, 0x04,	0x00, "On"				},
	{DIPSW_IDX,	0x01, 0x04,	0x04, "Off"				},

	{0,			0xFE, 0,	2,    "Free"			},
	{DIPSW_IDX,	0x01, 0x08,	0x00, "Off"				},
	{DIPSW_IDX,	0x01, 0x08,	0x08, "On"				},

	{0,			0xFE, 0,	2,    "Stop"			},
	{DIPSW_IDX,	0x01, 0x10,	0x00, "Off"				},
	{DIPSW_IDX,	0x01, 0x10,	0x10, "On"				},

	{0,			0xFE, 0,	2,    "Debug"			},
	{DIPSW_IDX,	0x01, 0x80,	0x00, "Off"				},
	{DIPSW_IDX,	0x01, 0x80,	0x80, "On"				},


	{REGSW_IDX,	0xFF, 0xFF,	0x00, NULL				},
	{0,			0xFE, 0,	6,    "Region (Custom)"	},

	// 西游 2 等版本的地区代码有差异，待完善
	{REGSW_IDX,	0x01, 0x0F,	0x00, "China"			},
	{REGSW_IDX,	0x01, 0x0F,	0x01, "Taiwan"			},
	{REGSW_IDX,	0x01, 0x0F,	0x02, "Japan"			},
	{REGSW_IDX,	0x01, 0x0F,	0x03, "Korea"			},
	{REGSW_IDX,	0x01, 0x0F,	0x04, "Hong Kong"		},
	{REGSW_IDX,	0x01, 0x0F,	0x05, "Overseas"		},
};

STDDIPINFO(Pgm2)

// -----------------------------------------------------------------------------
//   Rom Loading 忽略 Crc 校验值
// -----------------------------------------------------------------------------

// 西游释厄传 2 / Xīyóu shì è zhuàn 2 / Oriental Legend 2

#define ORLEG2_ROMS( Driver, Program )											\
																				\
static struct BurnRomInfo Driver##RomDesc[] = {									\
	{ "orleg2_core_v100.rom",		0x0004000, 0, 1 | BRF_PRG | BRF_ESS },		\
																				\
	{ Program,						0x0800000, 0, 2 | BRF_PRG | BRF_ESS },		\
																				\
	{ "orleg2_blank_card.pg2",		0x0000108, 0, 3 | BRF_PRG | BRF_ESS },		\
																				\
	{ "orleg2_text.bin",			0x0400000, 0, 4 | BRF_GRA },				\
																				\
	{ "orleg2_map.bin",				0x1000000, 0, 5 | BRF_GRA },				\
																				\
	{ "orleg2_sprite_mask.bin",		0x2000000, 0, 6 | BRF_GRA },				\
																				\
	{ "orleg2_sprite_colour.bin",	0x4000000, 0, 7 | BRF_GRA },				\
																				\
	{ "orleg2_wave.bin",			0x1000000, 0, 8 | BRF_SND },				\
																				\
	{ "orleg2_nvram.bin",			0x0010000, 0, 9 | BRF_PRG | BRF_ESS },		\
};																				\
																				\
STD_ROM_PICK(Driver)															\
STD_ROM_FN(Driver)

ORLEG2_ROMS(orleg2101,	"orleg2_main_v101.rom")
ORLEG2_ROMS(orleg2103,	"orleg2_main_v103.rom")
ORLEG2_ROMS(orleg2,		"orleg2_main_v104.rom")	// External Rom (V104 08-03-03 13:25:37)

static void checking_rom_orleg2()
{
	u32 *const src = (u32 *)m_maincpu;
	src[0x00002DB0 / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip encryption_do_367C()
}

static s32 orleg2Init()
{
	pPgm2InitCallback = checking_rom_orleg2;
	nPgm2RegionHackAddress = 0x00003CB8;

	return Pgm2Init();
}

struct BurnDriver BurnDrvorleg2101 = {
	"orleg2_101", "orleg2", NULL, NULL, "2007",
	"Oriental Legend 2 / Xi You Shi E Zhuan 2 (V101, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u897f\u6e38\u91ca\u5384\u4f20 2\0Oriental Legend 2 (V101, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, orleg2101RomInfo, orleg2101RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	orleg2Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};

struct BurnDriver BurnDrvorleg2103 = {
	"orleg2_103", "orleg2", NULL, NULL, "2007",
	"Oriental Legend 2 / Xi You Shi E Zhuan 2 (V103, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u897f\u6e38\u91ca\u5384\u4f20 2\0Oriental Legend 2 (V103, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, orleg2103RomInfo, orleg2103RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	orleg2Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};

struct BurnDriver BurnDrvorleg2 = {
	"orleg2", NULL, NULL, NULL, "2007",
	"Oriental Legend 2 / Xi You Shi E Zhuan 2 (V104, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u897f\u6e38\u91ca\u5384\u4f20 2\0Oriental Legend 2 (V104, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, orleg2RomInfo, orleg2RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	orleg2Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};


// 三国战纪 2 盖世英雄 / Sānguó zhàn jì 2 Gàishì yīngxióng / Knights of Valour 2 New Legend

#define KOV2NL_ROMS( Driver, Program )											\
																				\
static struct BurnRomInfo Driver##RomDesc[] = {									\
	{ "kov2nl_core_v100.rom",		0x0004000, 0, 1 | BRF_PRG | BRF_ESS },		\
																				\
	{ Program,						0x0800000, 0, 2 | BRF_PRG | BRF_ESS },		\
																				\
	{ "kov2nl_blank_card.pg2",		0x0000108, 0, 3 | BRF_PRG | BRF_ESS },		\
																				\
	{ "kov2nl_text.bin",			0x0400000, 0, 4 | BRF_GRA },				\
																				\
	{ "kov2nl_map.bin",				0x1000000, 0, 5 | BRF_GRA },				\
																				\
	{ "kov2nl_sprite_mask.bin",		0x2000000, 0, 6 | BRF_GRA },				\
																				\
	{ "kov2nl_sprite_colour.bin",	0x4000000, 0, 7 | BRF_GRA },				\
																				\
	{ "kov2nl_wave.bin",			0x2000000, 0, 8 | BRF_SND },				\
																				\
	{ "kov2nl_nvram.bin",			0x0010000, 0, 9 | BRF_PRG | BRF_ESS },		\
};																				\
																				\
STD_ROM_PICK(Driver)															\
STD_ROM_FN(Driver)

KOV2NL_ROMS(kov2nl300,	"kov2nl_main_v300.rom")
KOV2NL_ROMS(kov2nl301,	"kov2nl_main_v301.rom")
KOV2NL_ROMS(kov2nl,		"kov2nl_main_v302.rom")	// External Rom (V302 08-12-03 15:27:34)

static void checking_rom_kov2nl()
{
	u32 *const src = (u32 *)m_maincpu;
	src[0x00000DA8 / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip encryption_do_1674()
}

static s32 kov2nlInit()
{
	pPgm2InitCallback = checking_rom_kov2nl;
	nPgm2RegionHackAddress = 0x00001CBC;

	return Pgm2Init();
}

struct BurnDriver BurnDrvkov2nl300 = {
	"kov2nl_300", "kov2nl", NULL, NULL, "2008",
	"Knights of Valour 2 New Legend / Sanguo Zhan Ji 2 Gaishi Yingxiong (V300, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 2 - \u76d6\u4e16\u82f1\u96c4\0Knights of Valour 2 New Legend (V300, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov2nl300RomInfo, kov2nl300RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov2nlInit, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};

struct BurnDriver BurnDrvkov2nl301 = {
	"kov2nl_301", "kov2nl", NULL, NULL, "2008",
	"Knights of Valour 2 New Legend / Sanguo Zhan Ji 2 Gaishi Yingxiong (V301, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 2 - \u76d6\u4e16\u82f1\u96c4\0Knights of Valour 2 New Legend (V301, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov2nl301RomInfo, kov2nl301RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov2nlInit, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};

struct BurnDriver BurnDrvkov2nl = {
	"kov2nl", NULL, NULL, NULL, "2008",
	"Knights of Valour 2 New Legend / Sanguo Zhan Ji 2 Gaishi Yingxiong (V302, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 2 - \u76d6\u4e16\u82f1\u96c4\0Knights of Valour 2 New Legend (V302, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov2nlRomInfo, kov2nlRomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov2nlInit, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	448, 224, 4, 3
};


// 拳皇 ’98 - 终极之战 英雄 / Quán Huáng '98 Zhōng Jí Zhī Zhàn Yīng Xióng / King of Fighters '98: Ultimate Match Hero

static struct BurnRomInfo kof98umhRomDesc[] = {
	{ "kof98umh_core_v100cn.rom",	0x00004000, 0, 1 | BRF_PRG | BRF_ESS },	// 1 Internal Rom (Core V100 China)

	{ "kof98umh_main_v100cn.rom",	0x01000000, 0, 2 | BRF_PRG | BRF_ESS },	// 2 External Rom (V100 09-08-23 17:52:03)

	{ "kof98umh_text.bin",			0x00400000, 0, 4 | BRF_GRA },			// 3 TX Layer Tiles 解压

	{ "kof98umh_map.bin",			0x02000000, 0, 5 | BRF_GRA },			// 4 BG Layer Tiles 解压

	{ "kof98umh_sprite_mask.bin",	0x08000000, 0, 6 | BRF_GRA },			// 5 Sprite Bit Mask 四字节交换、解密

	{ "kof98umh_sprite_colour.bin",	0x20000000, 0, 7 | BRF_GRA },			// 6 Sprite Color Indexs 解密

	{ "kof98umh_wave.bin",			0x08000000, 0, 8 | BRF_SND },			// 7 Samples 二字节交换

	{ "kof98umh_nvram.bin",			0x00010000, 0, 9 | BRF_PRG | BRF_ESS },	// 8 NV RAM
};

STD_ROM_PICK(kof98umh)
STD_ROM_FN(kof98umh)

static void checking_rom_kof98umh()
{
	u32 *const src = (u32 *)m_maincpu;
	src[0x00000B40 / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip encryption_do_17E0()
}

static s32 kof98umhInit()
{
	pPgm2InitCallback = checking_rom_kof98umh;
	nPgm2RegionHackAddress = 0x00001E5C;

	return Pgm2Init();
}

struct BurnDriver BurnDrvkof98umh = {
	"kof98umh", NULL, NULL, NULL, "2009",
	"The King of Fighters '98: Ultimate Match HERO (V100, China)\0", NULL, "IGS / SNK Playmore / New Channel", "PolyGameMaster2",
	L"\u62f3\u7687 \u201998 - \u7ec8\u6781\u4e4b\u6218 \u82f1\u96c4\0The King of Fighters '98: Ultimate Match HERO (V100, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_IGS_PGM2, GBF_VSFIGHT, FBF_KOF,
	NULL, kof98umhRomInfo, kof98umhRomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kof98umhInit, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	320, 240, 4, 3
};


// 怒首領蜂 大往生 魂 / ドドンパッチ大王生ホーン / DoDonPachi Dai-Ou-Jou Tamashii

static struct BurnRomInfo ddpdojtRomDesc[] = {
	{ "ddpdoj_core_v100cn.rom",		0x0004000, 0, 1 | BRF_PRG | BRF_ESS },	// 1 Internal Rom (Core V100 China)
	// map(0x10000000, 0x101fffff).ram().share("romboard_ram");
	{ "ddpdoj_board_v201cn.rom",	0x0200000, 0, 2 | BRF_PRG | BRF_ESS },	// 2 romboard_ram
	// map(0x10200000, 0x103fffff).rom().region("mainrom", 0);
	{ "ddpdoj_main_v201cn.rom",		0x0200000, 0, 2 | BRF_PRG | BRF_ESS },	// 2 External Rom (V201 10-03-27 17:45:12)

	{ "ddpdoj_text.bin",			0x0400000, 0, 4 | BRF_GRA },			// 3 TX Layer Tiles 解压

	{ "ddpdoj_map.bin",				0x2000000, 0, 5 | BRF_GRA },			// 4 BG Layer Tiles 解压

	{ "ddpdoj_sprite_mask.bin",		0x1000000, 0, 6 | BRF_GRA },			// 5 Sprite Bit Mask 四字节交换、解密

	{ "ddpdoj_sprite_colour.bin",	0x2000000, 0, 7 | BRF_GRA },			// 6 Sprite Color Indexs 解密

	{ "ddpdoj_wave.bin",			0x1000000, 0, 8 | BRF_SND },			// 7 Samples 二字节交换

	{ "ddpdoj_nvram.bin",			0x0010000, 0, 9 | BRF_PRG | BRF_ESS },	// 8 NV RAM
};

STD_ROM_PICK(ddpdojt)
STD_ROM_FN(ddpdojt)

static void checking_rom_ddpdojt()
{
	u32 *const src = (u32 *)m_maincpu;

	src[0x00000B5C / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip memcpy_B04()
	src[0x00000B94 / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip encryption_do_19B8()
}

static s32 ddpdojtInit()
{
	pPgm2InitCallback = checking_rom_ddpdojt;
	nPgm2RegionHackAddress = 0x0000201C;

	return Pgm2Init();
}

struct BurnDriver BurnDrvddpdojt = {
	"ddpdojt", NULL, NULL, NULL, "2010",
	"DoDonPachi Dai-Ou-Jou Tamashii (V201, China)\0", NULL, "IGS / Cave", "PolyGameMaster2",
	L"\u6012\u9996\u9818\u8702 \u5927\u5f80\u751f \u9b42\0DoDonPachi Dai-Ou-Jou Tamashii (V201, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_ORIENTATION_VERTICAL | BDF_HISCORE_SUPPORTED, 2, HARDWARE_IGS_PGM2, GBF_VERSHOOT, 0,
	NULL, ddpdojtRomInfo, ddpdojtRomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	ddpdojtInit, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	224, 448, 3, 4
};


// 三国战纪 3 / Sānguó zhàn jì 3 / Knights of Valour 3

#define KOV3_ROMS( Driver, Program )											\
																				\
static struct BurnRomInfo Driver##RomDesc[] = {									\
	{ "kov3_core_v100.rom",		0x0004000, 0, 1 | BRF_PRG | BRF_ESS },			\
																				\
	{ Program,					0x0800000, 0, 2 | BRF_PRG | BRF_ESS },			\
																				\
	{ "kov3_blank_card.pg2",	0x0000108, 0, 3 | BRF_PRG },					\
																				\
	{ "kov3_text.bin",			0x0400000, 0, 4 | BRF_GRA },					\
																				\
	{ "kov3_map.bin",			0x2000000, 0, 5 | BRF_GRA },					\
																				\
	{ "kov3_sprite_mask.bin",	0x4000000, 0, 6 | BRF_GRA },					\
																				\
	{ "kov3_sprite_colour.bin",	0x8000000, 0, 7 | BRF_GRA },					\
																				\
	{ "kov3_wave.bin",			0x4000000, 0, 8 | BRF_SND },					\
																				\
	{ "kov3_nvram.bin",			0x0010000, 0, 9 | BRF_PRG | BRF_ESS },			\
};																				\
																				\
STD_ROM_PICK(Driver)															\
STD_ROM_FN(Driver)

KOV3_ROMS(kov3100,	"kov3_main_v100.rom")
KOV3_ROMS(kov3101,	"kov3_main_v101.rom")
KOV3_ROMS(kov3102,	"kov3_main_v102.rom")
KOV3_ROMS(kov3,		"kov3_main_v104.rom")	// External Rom (V104 11-12-09 14:29:14)

static void checking_rom_kov3()
{
	u32* const src = (u32 *)m_maincpu;

	src[0x00002A8C / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip decrypt_kov3_module_1C94()
	src[0x00002A90 / 4] = BURN_ENDIAN_SWAP_INT32(0x00000000);	// skip encryption_do_322C()
}

static s32 kov3Init()
{
	pPgm2InitCallback = checking_rom_kov3;

	return Pgm2Init();
}

struct BurnDriver BurnDrvkov3100 = {
	"kov3_100", "kov3", NULL, NULL, "2011",
	"Knights of Valour 3 / Sanguo Zhan Ji 3 (V100, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 3\0Knights of Valour 3 (V100, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov3100RomInfo, kov3100RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov3Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	512, 240, 4, 3
};

struct BurnDriver BurnDrvkov3101 = {
	"kov3_101", "kov3", NULL, NULL, "2011",
	"Knights of Valour 3 / Sanguo Zhan Ji 3 (V101, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 3\0Knights of Valour 3 (V101, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov3101RomInfo, kov3101RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov3Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	512, 240, 4, 3
};

struct BurnDriver BurnDrvkov3102 = {
	"kov3_102", "kov3", NULL, NULL, "2011",
	"Knights of Valour 3 / Sanguo Zhan Ji 3 (V102, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 3\0Knights of Valour 3 (V102, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov3102RomInfo, kov3102RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov3Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	512, 240, 4, 3
};

struct BurnDriver BurnDrvkov3 = {
	"kov3", NULL, NULL, NULL, "2011",
	"Knights of Valour 3 / Sanguo Zhan Ji 3 (V104, China)\0", NULL, "IGS", "PolyGameMaster2",
	L"\u4e09\u56fd\u6218\u7eaa 3\0Knights of Valour 3 (V104, China)\0", NULL, NULL, NULL,
	BDF_GAME_WORKING, kMaxPlayer, HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD, GBF_SCRFIGHT, 0,
	NULL, kov3RomInfo, kov3RomName, NULL, NULL, NULL, NULL, Pgm2InputInfo, Pgm2DIPInfo,
	kov3Init, Pgm2Exit, Pgm2Frame, Pgm2Draw, Pgm2Scan, &BurnRecalc, nPgm2PaletteEntries,
	512, 240, 4, 3
};

