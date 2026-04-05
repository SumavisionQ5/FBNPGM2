// license:BSD-3-Clause
// copyright-holders:David Haywood, Miodrag Milanovic

/*********************************************************************

		pgm2_memcard.h

		PGM2 Memory card functions.
		(based on ng_memcard.h)

*********************************************************************/

#pragma once

#ifndef MAME_IGS_PGM2_MEMCARD_H
#define MAME_IGS_PGM2_MEMCARD_H


#include "mame_stuff.h"	// typedef
// src/burner/win32/memcard.cpp

#define popmessage(...)	// 待补充

// ----------------------------------------------------------------------------
// E-Z pgm2_memcard logic
// ----------------------------------------------------------------------------

class Pgm2Memcard
{
public:
	// 插入记忆卡 std::pair<std::error_condition, std::string> pgm2_memcard_device::call_load()
	int call_load(TCHAR *szFilename)
	{
		m_authenticated = false;

		FILE *m_file = _tfopen(szFilename, _T("rb"));
		if (m_file == NULL) { return IMAGE_ERROR_INTERNAL; }
		
		fseek(m_file, 0, SEEK_END);
		if ( ftell(m_file) != 0x108 ) {	// length()
			bprintf(0, _T("Incorrect memory card file size (must be 264 bytes)\n"));
			return IMAGE_ERROR_INVALIDLENGTH;
		}

		fseek(m_file, 0, SEEK_SET);

		if ( fread(m_memcard_data,    1, 0x100, m_file) != 0x100 )
			return IMAGE_ERROR_UNSPECIFIED;
		if ( fread(m_protection_data, 1,     4, m_file) != 4 )
			return IMAGE_ERROR_UNSPECIFIED;
		if ( fread(m_security_data,   1,     4, m_file) != 4 )
			return IMAGE_ERROR_UNSPECIFIED;

		fclose(m_file);
		
		m_memcard_inserted = true;	// 完成插入

		return STD_ERROR_CONDITION;
	}

	// 退出记忆卡 void pgm2_memcard_device::call_unload()
	int call_unload(TCHAR *szFilename)
	{
		m_authenticated = false;

		FILE *m_file = _tfopen(szFilename, _T("wb"));
		if (m_file == NULL) { return IMAGE_ERROR_INTERNAL; }

		fseek(m_file,0, SEEK_SET);

		fwrite(m_memcard_data,    1, 0x100, m_file);
		fwrite(m_protection_data, 1,     4, m_file);
		fwrite(m_security_data,   1,     4, m_file);

		fclose(m_file);
		
		m_memcard_inserted = false;	// 完成退出

		return STD_ERROR_CONDITION;
	}

	// 创建记忆卡 std::pair<std::error_condition, std::string> pgm2_memcard_device::call_create(int format_type, util::option_resolution *format_options)
	int call_create(TCHAR *szFilename)
	{
		m_authenticated = false;

		FILE *m_file = _tfopen(szFilename, _T("wb"));
		if (m_file == NULL) { return IMAGE_ERROR_INTERNAL; }

		// cards must contain valid defaults for each game / region or they don't work?
		extern u8 *m_default_card;
		if (!m_default_card) { return IMAGE_ERROR_INTERNAL; }

		//// 写入内存
		//memcpy(m_memcard_data,    m_default_card,         0x100);
		//memcpy(m_protection_data, m_default_card + 0x100,     4);
		//memcpy(m_security_data,   m_default_card + 0x104,     4);

		// 写入文件
		size_t ret = fwrite(m_default_card, 1, 0x108, m_file);
		if ( ret != 0x108 ) { ret = IMAGE_ERROR_UNSPECIFIED; }

		fclose(m_file);
		
		//m_memcard_inserted = true;	// 完成插入

		return STD_ERROR_CONDITION;
	}

	/* returns the index of the current memory card, or -1 if none */
	int present() { return m_memcard_inserted ? 0 : -1; }

	void auth(u8 p1, u8 p2, u8 p3)
	{
		if (m_security_data[0] & 7)
		{
			if (m_security_data[1] == p1 && m_security_data[2] == p2 && m_security_data[3] == p3) {
				m_authenticated = true;
				m_security_data[0] = 7;
			}
			else {
				m_authenticated = false;
				m_security_data[0] >>= 1; // hacky
				if (m_security_data[0] & 7)
					popmessage("Wrong IC Card password !!!\n");
				else
					popmessage("Wrong IC Card password, card was locked!!!\n");
			}
		}
	}

	u8 read(u32 offset)
	{
		return m_memcard_data[offset];
	}
	void write(u32 offset, u8 data)
	{
		if (m_authenticated && (offset >= 0x20 || (m_protection_data[offset>>3] & (1 <<(offset & 7)))))
		{
			m_memcard_data[offset] = data;
		}
	}

	u8 read_prot(u32 offset)
	{
		return m_protection_data[offset];
	}
	void write_prot(u32 offset, u8 data)
	{
		if (m_authenticated)
			m_protection_data[offset] &= data;
	}

	u8 read_sec(u32 offset)
	{
		if (!m_authenticated)
			return 0xff; // guess
		return m_security_data[offset];
	}
	void write_sec(u32 offset, u8 data)
	{
		if (m_authenticated)
			m_security_data[offset] = data;
	}

	void device_start()
	{
		SCAN_VAR(m_memcard_data);
	}

private:
	bool m_memcard_inserted = false;
	bool m_authenticated = false;

	u8 m_memcard_data[0x100];
	u8 m_protection_data[4];
	u8 m_security_data[4];

	enum { STD_ERROR_CONDITION = 0, IMAGE_ERROR_INTERNAL, IMAGE_ERROR_UNSPECIFIED, IMAGE_ERROR_INVALIDLENGTH };
	// std::make_pair(std::error_condition(), std::string());
	// std::make_pair(image_error::INTERNAL, std::string());
	// std::make_pair(image_error::UNSPECIFIED, std::string());
};


#endif // MAME_IGS_PGM2_MEMCARD_H
