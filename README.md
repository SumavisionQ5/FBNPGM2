# PGM2 Driver for FinalBurn Neo
This project is ported from MAME 0.254, based on FBNeo 1.0.0.2 code. Compared to the original code, the main change is the reduction of ROM encryption/decryption content.

The code in this project is functional, but due to limited personal time and energy, QingTian(qt) cannot personally merge the current code into master. QingTian hopes that capable contributors can take over and complete the merge.

QingTian said he is just an amateur programming enthusiast, the code quality may be limited. Please bear with it.

demo: https://www.bilibili.com/video/BV1WCtce6ECT

Decrypted Roms: https://drive.google.com/drive/folders/1loZzk147WFaIIxPd04FHd3RNTos1cnHB?usp=sharing

## Addtion note:

```c++
// -----------------------------------------------------------------------------
//   Menu: card
// -----------------------------------------------------------------------------

.file "src/burn/burn.h"
.line 621

#define HARDWARE_IGS_PGM2								(HARDWARE_PREFIX_IGS_PGM | 0x00010000)
#define HARDWARE_IGS_USE_MEM_CARD						(0x0002)

.file "src/burner/win32/menu.cpp"

static int aMenuMemcard[kMaxMemcard][kMaxMemcardItem] = {
	{MENU_MEMCARD_CREATE,   MENU_MEMCARD_SELECT,   MENU_MEMCARD_EJECT,   MENU_MEMCARD_INSERT  },
	{MENU_MEMCARD_CREATE_2, MENU_MEMCARD_SELECT_2, MENU_MEMCARD_EJECT_2, MENU_MEMCARD_INSERT_2},
	{MENU_MEMCARD_CREATE_3, MENU_MEMCARD_SELECT_3, MENU_MEMCARD_EJECT_3, MENU_MEMCARD_INSERT_3},
	{MENU_MEMCARD_CREATE_4, MENU_MEMCARD_SELECT_4, MENU_MEMCARD_EJECT_4, MENU_MEMCARD_INSERT_4},
};
#define DISABLE_MENU_MEMCARD_ITEMS \
	do { \
		for (int i = 0; i < kMaxMemcard; i++) { \
			for (int j = 0; j < kMaxMemcardItem; j++) { \
				EnableMenuItem(hMenu, aMenuMemcard[i][j], MF_GRAYED | MF_BYCOMMAND); \
			} \
		} \
	} while (0)

.line 1362~1365
.line 1495~1498

		DISABLE_MENU_MEMCARD_ITEMS;

.line 1380~1395

		if ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOGEO) {
			EnableMenuItem(hMenu, MENU_INTERPOLATE_1,				MF_GRAYED | MF_BYCOMMAND);
			EnableMenuItem(hMenu, MENU_INTERPOLATE_3,				MF_GRAYED | MF_BYCOMMAND);

			if (!kNetGame) {
				EnableMenuItem(hMenu, MENU_MEMCARD_CREATE,			MF_ENABLED | MF_BYCOMMAND);
				EnableMenuItem(hMenu, MENU_MEMCARD_SELECT,			MF_ENABLED | MF_BYCOMMAND);
				if (nMemoryCardStatus[0] & 1) {
					if (nMemoryCardStatus[0] & 2) {
						EnableMenuItem(hMenu, MENU_MEMCARD_EJECT,	MF_ENABLED | MF_BYCOMMAND);
					} else {
						EnableMenuItem(hMenu, MENU_MEMCARD_INSERT,	MF_ENABLED | MF_BYCOMMAND);
					}
				}
			}
		}

		if ( BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD) )
		{
			for (int i = 0; i < kMaxMemcard; i++)
			{
				EnableMenuItem(hMenu, aMenuMemcard[i][0],			MF_ENABLED | MF_BYCOMMAND);
				EnableMenuItem(hMenu, aMenuMemcard[i][1],			MF_ENABLED | MF_BYCOMMAND);

				if (nMemoryCardStatus[i] & 1)
				{
					if (nMemoryCardStatus[i] & 2) {
						EnableMenuItem(hMenu, aMenuMemcard[i][2],	MF_ENABLED | MF_BYCOMMAND);
					} else {
						EnableMenuItem(hMenu, aMenuMemcard[i][3],	MF_ENABLED | MF_BYCOMMAND);
					}
				}
			}
		}

.file "src/burner/win32/scrn.cpp"

.line 1024

static void __fastcall handleMenuMemCard(int nItem, int nCard)
{
	if (!kNetGame &&
	 (((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOGEO) && (!nCard) ||
	   (BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD))))
	{
		switch (nItem)
		{
			case 0:
			case 1:
				if (bDrvOkay && UseDialogs())
				{
					InputSetCooperativeLevel(false, bAlwaysProcessKeyboardInput);
					AudBlankSound();
					MemCardEject(nCard);

					if (nItem == 0)	{ MemCardCreate(nCard); }
					if (nItem == 1) { MemCardSelect(nCard); }

					MemCardInsert(nCard);
					GameInpCheckMouse();
				}
				break;
			case 2:
				MemCardInsert(nCard);
				break;
			case 3:
				MemCardEject(nCard);
				break;
		}
	}
}

.line 1352~1387

		case MENU_MEMCARD_CREATE:	handleMenuMemCard(0, 0);	break;
		case MENU_MEMCARD_SELECT:	handleMenuMemCard(1, 0);	break;
		case MENU_MEMCARD_INSERT:	handleMenuMemCard(2, 0);	break;
		case MENU_MEMCARD_EJECT:	handleMenuMemCard(3, 0);	break;

		case MENU_MEMCARD_CREATE_2:	handleMenuMemCard(0, 1);	break;
		case MENU_MEMCARD_SELECT_2:	handleMenuMemCard(1, 1);	break;
		case MENU_MEMCARD_INSERT_2:	handleMenuMemCard(2, 1);	break;
		case MENU_MEMCARD_EJECT_2:	handleMenuMemCard(3, 1);	break;

		case MENU_MEMCARD_CREATE_3:	handleMenuMemCard(0, 2);	break;
		case MENU_MEMCARD_SELECT_3:	handleMenuMemCard(1, 2);	break;
		case MENU_MEMCARD_INSERT_3:	handleMenuMemCard(2, 2);	break;
		case MENU_MEMCARD_EJECT_3:	handleMenuMemCard(3, 2);	break;

		case MENU_MEMCARD_CREATE_4:	handleMenuMemCard(0, 3);	break;
		case MENU_MEMCARD_SELECT_4:	handleMenuMemCard(1, 3);	break;
		case MENU_MEMCARD_INSERT_4:	handleMenuMemCard(2, 3);	break;
		case MENU_MEMCARD_EJECT_4:	handleMenuMemCard(3, 3);	break;

		case MENU_MEMCARD_TOGGLE:
			if (bDrvOkay && !kNetGame &&
			 (((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOGEO) ||
			   (BurnDrvGetHardwareCode() == (HARDWARE_IGS_PGM2 | HARDWARE_IGS_USE_MEM_CARD))))
			{
				MemCardToggle();
			}
			break;

.file "src/burner/win32/app.rc"
.line 873

			MENUITEM SEPARATOR
			MENUITEM "Create new memory card...\tmemc2",	MENU_MEMCARD_CREATE_2
			MENUITEM "Select memory card...\tmemc2",		MENU_MEMCARD_SELECT_2
			MENUITEM "Insert memory card\tmemc2",			MENU_MEMCARD_INSERT_2
			MENUITEM "Eject memory card\tmemc2",			MENU_MEMCARD_EJECT_2
			MENUITEM SEPARATOR
			MENUITEM "Create new memory card...\tmemc3",	MENU_MEMCARD_CREATE_3
			MENUITEM "Select memory card...\tmemc3",		MENU_MEMCARD_SELECT_3
			MENUITEM "Insert memory card\tmemc3",			MENU_MEMCARD_INSERT_3
			MENUITEM "Eject memory card\tmemc3",			MENU_MEMCARD_EJECT_3
			MENUITEM SEPARATOR
			MENUITEM "Create new memory card...\tmemc4",	MENU_MEMCARD_CREATE_4
			MENUITEM "Select memory card...\tmemc4",		MENU_MEMCARD_SELECT_4
			MENUITEM "Insert memory card\tmemc4",			MENU_MEMCARD_INSERT_4
			MENUITEM "Eject memory card\tmemc4",			MENU_MEMCARD_EJECT_4

.file "src/burner/win32/resource.h"
.line 476

#define MENU_MEMCARD_CREATE_2				10040
#define MENU_MEMCARD_SELECT_2				10041
#define MENU_MEMCARD_INSERT_2				10042
#define MENU_MEMCARD_EJECT_2				10043
#define MENU_MEMCARD_CREATE_3				10045
#define MENU_MEMCARD_SELECT_3				10046
#define MENU_MEMCARD_INSERT_3				10047
#define MENU_MEMCARD_EJECT_3				10048
#define MENU_MEMCARD_CREATE_4				10050
#define MENU_MEMCARD_SELECT_4				10051
#define MENU_MEMCARD_INSERT_4				10052
#define MENU_MEMCARD_EJECT_4				10053

.file "src/burner/win32/burner_win32.h"
.line 469~474

constexpr int kMaxMemcard = 4, kMaxMemcardItem = 4;
extern int nMemoryCardStatus[];						// & 1 = file selected, & 2 = inserted

int	MemCardCreate(int nCard = 0);
int	MemCardSelect(int nCard = 0);
int	MemCardInsert(int nCard = 0);
int	MemCardEject (int nCard = 0);

.file "src/burner/win32/memcard.cpp"

```
