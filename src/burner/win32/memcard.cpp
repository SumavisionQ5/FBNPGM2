// Memory card support module

#include "burner.h"
#include "./drv/pgm2/pgm2_memcard.h"

extern Pgm2Memcard pgm2_memcard[];
static int nCurrentMemoryCard = 0/*-1*/;

static TCHAR szMemoryCardFile[kMaxMemcard][MAX_PATH];

int nMemoryCardStatus[kMaxMemcard]{};
int nMemoryCardSize[kMaxMemcard];

static int nMinVersion;
static bool bMemCardFC1Format;

static int MakeOfn()
{
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hScrnWnd;
	ofn.lpstrFile = szMemoryCardFile[nCurrentMemoryCard];
	ofn.nMaxFile = sizeof(szMemoryCardFile[nCurrentMemoryCard]);
	ofn.lpstrInitialDir = _T(".");
	ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = _T("fc");

	return 0;
}

static int MemCardRead(TCHAR* szFilename, unsigned char* pData, int nSize)
{
	const char* szHeader  = "FB1 FC1 ";				// File + chunk identifier
	char szReadHeader[8] = "";

	bMemCardFC1Format = false;

	FILE* fp = _tfopen(szFilename, _T("rb"));
	if (fp == NULL) {
		return 1;
	}

	fread(szReadHeader, 1, 8, fp);					// Read identifiers
	if (memcmp(szReadHeader, szHeader, 8) == 0) {

		// FB Alpha memory card file

		int nChunkSize = 0;
		int nVersion = 0;

		bMemCardFC1Format = true;

		fread(&nChunkSize, 1, 4, fp);				// Read chunk size
		if (nSize < nChunkSize - 32) {
			fclose(fp);
			return 1;
		}

		fread(&nVersion, 1, 4, fp);					// Read version
		if (nVersion < nMinVersion) {
			fclose(fp);
			return 1;
		}
		fread(&nVersion, 1, 4, fp);
#if 0
		if (nVersion < nBurnVer) {
			fclose(fp);
			return 1;
		}
#endif

		fseek(fp, 0x0C, SEEK_CUR);					// Move file pointer to the start of the data block

		fread(pData, 1, nChunkSize - 32, fp);		// Read the data
	} else {

		// MAME or old FB Alpha memory card file

		unsigned char* pTemp = (unsigned char*)malloc(nSize >> 1);

		memset(pData, 0, nSize);
		fseek(fp, 0x00, SEEK_SET);

		if (pTemp) {
			fread(pTemp, 1, nSize >> 1, fp);

			for (int i = 1; i < nSize; i += 2) {
				pData[i] = pTemp[i >> 1];	// 911013A2 > 1300A200 91001000
			}

			free(pTemp);
			pTemp = NULL;
		}
	}

	fclose(fp);

	return 0;
}

static int MemCardWrite(TCHAR* szFilename, unsigned char* pData, int nSize)
{
	FILE* fp = _tfopen(szFilename, _T("wb"));
	if (fp == NULL) {
		return 1;
	}

	if (bMemCardFC1Format) {

		// FB Alpha memory card file

		const char* szFileHeader  = "FB1 ";				// File identifier
		const char* szChunkHeader = "FC1 ";				// Chunk identifier
		const int nZero = 0;

		int nChunkSize = nSize + 32;					// 追加 FC 头部

		fwrite(szFileHeader, 1, 4, fp);
		fwrite(szChunkHeader, 1, 4, fp);

		fwrite(&nChunkSize, 1, 4, fp);					// Chunk size

		fwrite(&nBurnVer, 1, 4, fp);					// Version of FBA this was saved from
		fwrite(&nMinVersion, 1, 4, fp);					// Min version of FBA data will work with

		fwrite(&nZero, 1, 4, fp);						// Reserved
		fwrite(&nZero, 1, 4, fp);						//
		fwrite(&nZero, 1, 4, fp);						//

		fwrite(pData, 1, nSize, fp);					// Write the data
	} else {

		// MAME or old FB Alpha memory card file

		unsigned char* pTemp = (unsigned char*)malloc(nSize >> 1);
		if (pTemp) {
			for (int i = 1; i < nSize; i += 2) {
				pTemp[i >> 1] = pData[i];
			}

			fwrite(pTemp, 1, nSize >> 1, fp);

			free(pTemp);
			pTemp = NULL;
		}
	}

	fclose(fp);

	return 0;
}

static int __cdecl MemCardDoGetSize(struct BurnArea* pba)
{
	nMemoryCardSize[nCurrentMemoryCard] = pba->nLen;

	return 0;
}

static int MemCardGetSize()
{
	BurnAcb = MemCardDoGetSize;
	BurnAreaScan(ACB_MEMCARD/* | ACB_MEMCARD_ACTION*/, &nMinVersion);

	return 0;
}

int	MemCardCreate(int nCard)
{
	//if (nCurrentMemoryCard != -1) {
	//	return 0;
	//}

	nCurrentMemoryCard = nCard;

	TCHAR szFilter[1024];
	int nRet;

	_stprintf(szFilter, FBALoadStringEx(hAppInst, IDS_DISK_FILE_CARD, true), _T(APP_TITLE));
	memcpy(szFilter + _tcslen(szFilter), _T(" (*.fc)\0*.fc\0\0"), 14 * sizeof(TCHAR));

	_stprintf (szMemoryCardFile[nCurrentMemoryCard], _T("memorycard"));
	MakeOfn();
	ofn.lpstrTitle = FBALoadStringEx(hAppInst, IDS_MEMCARD_CREATE, true);
	ofn.lpstrFilter = szFilter;
	ofn.Flags |= OFN_OVERWRITEPROMPT;	// 覆写

	int bOldPause = bRunPause;
	bRunPause = 1;
	nRet = GetSaveFileName(&ofn);
	bRunPause = bOldPause;

	if (nRet == 0) {
		return 1;
	}

	if ( BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD) )
	{
		pgm2_memcard[nCurrentMemoryCard].call_create(szMemoryCardFile[nCurrentMemoryCard]);
	}
	else
	{
		unsigned char* pCard;

		MemCardGetSize();

		pCard = (unsigned char*)malloc(nMemoryCardSize[nCurrentMemoryCard]);
		memset(pCard, 0, nMemoryCardSize[nCurrentMemoryCard]);

		bMemCardFC1Format = true;

		if (MemCardWrite(szMemoryCardFile[nCurrentMemoryCard], pCard, nMemoryCardSize[nCurrentMemoryCard])) {
			return 1;
		}

		if (pCard) {
			free(pCard);
			pCard = NULL;
		}
	}

	nMemoryCardStatus[nCurrentMemoryCard] = 1;	// 已选择记忆卡
	MenuEnableItems();

	// nCurrentMemoryCard = -1;

	return 0;
}

int	MemCardSelect(int nCard)
{
	//if (nCurrentMemoryCard != -1) {
	//	return 0;
	//}

	nCurrentMemoryCard = nCard;

	TCHAR szFilter[1024];	// 存储文件过滤器字符串
	TCHAR* pszTemp = szFilter;
	int nRet;

	pszTemp += _stprintf(pszTemp, FBALoadStringEx(hAppInst, IDS_DISK_ALL_CARD, true));
	memcpy(pszTemp, _T(" (*.fc, *.bin, MEMCARD.\?\?\?)\0*.fc;*.bin;MEMCARD.\?\?\?\0"), 51 * sizeof(TCHAR));
	pszTemp += 51;

	pszTemp += _stprintf(pszTemp, FBALoadStringEx(hAppInst, IDS_DISK_FILE_CARD, true), _T(APP_TITLE));
	memcpy(pszTemp, _T(" (*.fc)\0*.fc\0"), 13 * sizeof(TCHAR));
	pszTemp += 13;
	
	/* PGM2 Memory Card Files (*.pg2, *.mem, *.bin) */
	pszTemp += _stprintf(pszTemp, FBALoadStringEx(hAppInst, IDS_DISK_FILE_CARD, true), _T("PGM2"));
	memcpy(pszTemp, _T(" (*.pg2, *.mem, *.bin)\0*.pg2;*.mem;*.bin\0"), 41 * sizeof(TCHAR));
	pszTemp += 41;
	
	/* \? 和 \0 都表示一个字符，所以双引号内总共有 14+1+11+1+1 个字符，最末的 \0 结束文件过滤器 */
	pszTemp += _stprintf(pszTemp, FBALoadStringEx(hAppInst, IDS_DISK_FILE_CARD, true), _T("MAME"));
	memcpy(pszTemp, _T(" (MEMCARD.\?\?\?)\0MEMCARD.\?\?\?\0\0"), 28 * sizeof(TCHAR));

	MakeOfn();
	ofn.lpstrTitle = FBALoadStringEx(hAppInst, IDS_MEMCARD_SELECT, true);
	ofn.lpstrFilter = szFilter;

	int bOldPause = bRunPause;
	bRunPause = 1;
	nRet = GetOpenFileName(&ofn);
	bRunPause = bOldPause;

	if (nRet == 0) {
		return 1;
	}

	if ( BurnDrvGetHardwareCode() != (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD) )
	{
		MemCardGetSize();

		if (nMemoryCardSize[nCurrentMemoryCard] <= 0) {
			return 1;
		}
	}

	nMemoryCardStatus[nCurrentMemoryCard] = 1;
	MenuEnableItems();

	// nCurrentMemoryCard = -1;

	return 0;
}

static int __cdecl MemCardDoInsert(struct BurnArea* pba)
{
	if (MemCardRead(szMemoryCardFile[nCurrentMemoryCard], (unsigned char*)pba->Data, pba->nLen)) {
		return 1;
	}

	nMemoryCardStatus[nCurrentMemoryCard] |= 2;
	MenuEnableItems();

	return 0;
}

int	MemCardInsert(int nCard)
{
	//if (nCurrentMemoryCard != -1) {
	//	return 0;
	//}

	nCurrentMemoryCard = nCard;

	// bit0=1 已选择记忆卡、bit1=0 未插入记忆卡
	if ((nMemoryCardStatus[nCurrentMemoryCard] & 1) && (nMemoryCardStatus[nCurrentMemoryCard] & 2) == 0)
	{
		// MemCardDoInsert()
		if ( BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD) )
		{
			if (pgm2_memcard[nCurrentMemoryCard].call_load(szMemoryCardFile[nCurrentMemoryCard])) {
				return 1;
			}
			nMemoryCardStatus[nCurrentMemoryCard] |= 2;
			MenuEnableItems();
		}
		else
		{
			BurnAcb = MemCardDoInsert;
			BurnAreaScan(ACB_WRITE | ACB_MEMCARD/* | ACB_MEMCARD_ACTION*/, &nMinVersion);
		}
	}

	// nCurrentMemoryCard = -1;

	return 0;
}

static int __cdecl MemCardDoEject(struct BurnArea* pba)
{
	if (MemCardWrite(szMemoryCardFile[nCurrentMemoryCard], (unsigned char*)pba->Data, pba->nLen) == 0) {
		nMemoryCardStatus[nCurrentMemoryCard] &= ~2;
		MenuEnableItems();

		return 0;
	}

	return 1;
}

int	MemCardEject(int nCard)
{
	//if (nCurrentMemoryCard != -1) {
	//	return 0;
	//}

	nCurrentMemoryCard = nCard;

	// bit0=1 已选择记忆卡、bit1=1 已插入记忆卡
	if ((nMemoryCardStatus[nCurrentMemoryCard] & 1) && (nMemoryCardStatus[nCurrentMemoryCard] & 2))
	{
		// MemCardDoEject()
		if ( BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD) )
		{
			if (pgm2_memcard[nCurrentMemoryCard].call_unload(szMemoryCardFile[nCurrentMemoryCard])) {
				return 1;
			}
			nMemoryCardStatus[nCurrentMemoryCard] &= ~2;
			MenuEnableItems();
		}
		else
		{
			BurnAcb = MemCardDoEject;
			nMinVersion = 0;
			BurnAreaScan(ACB_READ | ACB_MEMCARD/* | ACB_MEMCARD_ACTION*/, &nMinVersion);
		}
	}

	// nCurrentMemoryCard = -1;

	return 0;
}

int	MemCardToggle()
{
	nCurrentMemoryCard = 0;

	if (nMemoryCardStatus[nCurrentMemoryCard] & 1) {
		if (nMemoryCardStatus[nCurrentMemoryCard] & 2) {
			return MemCardEject(nCurrentMemoryCard);
		} else {
			return MemCardInsert(nCurrentMemoryCard);
		}
	}

	return 1;
}
