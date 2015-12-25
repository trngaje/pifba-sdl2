#include "cps.h"
#include "gp2xsdk.h"
#include "cache.h"
// CPS (general)

unsigned char UpperReserved = 0;
int Cps = 0;							// 1 = CPS1, 2 = CPS2, 3 = CPS Changer
int Cps1Qs = 0;

int nCPS68KClockspeed = 0;
int nCpsCycles = 0;						// 68K Cycles per frame
int	nCpsZ80Cycles;

unsigned char *CpsGfx =NULL; unsigned int nCpsGfxLen =0; // All the graphics
unsigned char *CpsGfxSeg[2] = {NULL};
unsigned char *CpsRom =NULL; unsigned int nCpsRomLen =0; // Program Rom (as in rom)
unsigned char *CpsCode=NULL; unsigned int nCpsCodeLen=0; // Program Rom (decrypted)
unsigned char *CpsZRom=NULL; unsigned int nCpsZRomLen=0; // Z80 Roms
char *CpsQSam=NULL; unsigned int nCpsQSamLen=0;	// QSound Sample Roms
unsigned char *CpsAd  =NULL; unsigned int nCpsAdLen  =0; // ADPCM Data
unsigned char *CpsStar=NULL;
unsigned int nCpsGfxScroll[4]={0,0,0,0}; // Offset to Scroll tiles
unsigned int nCpsGfxMask=0;	  // Address mask

// Separate out the bits of a byte
inline static unsigned int Separate(unsigned int b)
{
	unsigned int a = b;									// 00000000 00000000 00000000 11111111
	a  =((a & 0x000000F0) << 12) | (a & 0x0000000F);	// 00000000 00001111 00000000 00001111
	a = ((a & 0x000C000C) <<  6) | (a & 0x00030003);	// 00000011 00000011 00000011 00000011
	a = ((a & 0x02020202) <<  3) | (a & 0x01010101);	// 00010001 00010001 00010001 00010001

	return a;
}

// Precalculated table of the Separate function
static unsigned int SepTable[256];

static int SepTableCalc()
{
	static int bDone = 0;
	if (bDone) {
		return 0;										// Already done it
	}

	for (int i = 0; i < 256; i++) {
		SepTable[i] = Separate(255 - i);
	}

	bDone = 1;											// done it
	return 0;
}

// Allocate space and load up a rom
static int LoadUp(unsigned char** pRom, int* pnRomLen, int nNum)
{
	unsigned char *Rom;
	struct BurnRomInfo ri;

	ri.nLen = 0;
	BurnDrvGetRomInfo(&ri, nNum);	// Find out how big the rom is
	if (ri.nLen <= 0) {
		return 1;
	}

	// Load the rom
	Rom = (unsigned char*)malloc(ri.nLen);
	if (Rom == NULL) {
		return 1;
	}

	if (BurnLoadRom(Rom,nNum,1)) {
		free(Rom);
		return 1;
	}

	// Success
	*pRom = Rom; *pnRomLen = ri.nLen;
	return 0;
}

// ----------------------------CPS1--------------------------------
// Load 1 rom and interleave in the CPS style:
// rom  : aa bb
// --ba --ba --ba --ba --ba --ba --ba --ba 8 pixels (four bytes)
//                                                  (skip four bytes)

static int CpsLoadOne(unsigned char* Tile, int nNum, int nWord, int nShift)
{
	unsigned char *Rom = NULL; int nRomLen=0;
	unsigned char *pt = NULL, *pr = NULL;
	int i;

	LoadUp(&Rom, &nRomLen, nNum);
	if (Rom == NULL) {
		return 1;
	}

	nRomLen &= ~1;								// make sure even

	for (i = 0, pt = Tile, pr = Rom; i < nRomLen; pt += 8) {
		unsigned int Pix;						// Eight pixels
		unsigned char b;
		b = *pr++; i++; Pix = SepTable[b];
		if (nWord) {
			b = *pr++; i++; Pix |= SepTable[b] << 1;
		}

		Pix <<= nShift;
		*((unsigned int *)pt) |= Pix;
	}

	free(Rom);
	return 0;
}

static int CpsLoadOneHack160(unsigned char *Tile, int nNum, int nWord, int nOffset)
{
	int i = 0;
	unsigned char *Rom1 = NULL, *Rom2 = NULL;
	int nRomLen1 = 0, nRomLen2 = 0;
	unsigned char *pt = NULL, *pr = NULL;

	LoadUp(&Rom1, &nRomLen1, nNum);
	if (Rom1 == NULL) {
		return 1;
	}
	LoadUp(&Rom2, &nRomLen2, nNum + 1);
	if (Rom2 == NULL) {
		return 1;
	}

	for (i = 0, pt = Tile, pr = Rom1 + (0x80000 * nOffset); i < 0x80000; pt += 8) {
		unsigned int Pix;		// Eight pixels
		unsigned char b;
		b = *pr++; i++; Pix = SepTable[b];
		if (nWord) {
			b = *pr++; i++; Pix |= SepTable[b] << 1;
		}

		Pix <<= 0;
		*((unsigned int *)pt) |= Pix;
	}

	for (i = 0, pt = Tile, pr = Rom2 + (0x80000 * nOffset); i < 0x80000; pt += 8) {
		unsigned int Pix;		// Eight pixels
		unsigned char b;
		b = *pr++; i++; Pix = SepTable[b];
		if (nWord) {
			b = *pr++; i++; Pix |= SepTable[b] << 1;
		}

		Pix <<= 2;
		*((unsigned int *)pt) |= Pix;
	}

	free(Rom2);
	free(Rom1);
	return 0;
}

static int CpsLoadOnePang(unsigned char *Tile,int nNum,int nWord,int nShift)
{
	int i=0;
	unsigned char *Rom = NULL; int nRomLen = 0;
	unsigned char *pt = NULL, *pr = NULL;

	LoadUp(&Rom, &nRomLen, nNum);
	if (Rom == NULL) {
		return 1;
	}

	nRomLen &= ~1; // make sure even

	for (i = 0x100000, pt = Tile, pr = Rom + 0x100000; i < nRomLen; pt += 8) {
		unsigned int Pix; // Eight pixels
		unsigned char b;
		b = *pr++; i++; Pix = SepTable[b];
		if (nWord) {
			b = *pr++; i++; Pix |= SepTable[b] << 1;
		}

		Pix <<= nShift;
		*((unsigned int *)pt) |= Pix;
	}

	free(Rom);
	return 0;
}

int CpsLoadTiles(unsigned char* Tile, int nStart)
{
	// left  side of 16x16 tiles
	CpsLoadOne(Tile,     nStart    , 1, 0);
	CpsLoadOne(Tile,     nStart + 1, 1, 2);
	// right side of 16x16 tiles
	CpsLoadOne(Tile + 4, nStart + 2, 1, 0);
	CpsLoadOne(Tile + 4, nStart + 3, 1, 2);
	return 0;
}

int CpsLoadTilesPang(unsigned char* Tile, int nStart)
{
	CpsLoadOne(    Tile,     nStart,     1, 0);
	CpsLoadOne(    Tile,     nStart + 1, 1, 2);
	CpsLoadOnePang(Tile + 4, nStart,     1, 0);
	CpsLoadOnePang(Tile + 4, nStart + 1, 1, 2);

	return 0;
}

int CpsLoadTilesHack160(unsigned char* Tile, int nStart)
{
	CpsLoadOneHack160(Tile + 0 + 0x000000, nStart, 1, 0);
	CpsLoadOneHack160(Tile + 4 + 0x000000, nStart, 1, 1);
	CpsLoadOneHack160(Tile + 0 + 0x200000, nStart, 1, 2);
	CpsLoadOneHack160(Tile + 4 + 0x200000, nStart, 1, 3);
	return 0;
}

int CpsLoadTilesByte(unsigned char* Tile, int nStart)
{
	CpsLoadOne(Tile,     nStart + 0, 0, 0);
	CpsLoadOne(Tile,     nStart + 1, 0, 1);
	CpsLoadOne(Tile,     nStart + 2, 0, 2);
	CpsLoadOne(Tile,     nStart + 3, 0, 3);
	CpsLoadOne(Tile + 4, nStart + 4, 0, 0);
	CpsLoadOne(Tile + 4, nStart + 5, 0, 1);
	CpsLoadOne(Tile + 4, nStart + 6, 0, 2);
	CpsLoadOne(Tile + 4, nStart + 7, 0, 3);
	return 0;
}

int CpsLoadStars(unsigned char* pStar, int nStart)
{
	unsigned char* pTemp[2] = { NULL, NULL};
	int nLen;

	for (int i = 0; i < 2; i++) {
		if (LoadUp(&pTemp[i], &nLen, nStart + (i << 1))) {
			free(pTemp[0]);
			free(pTemp[1]);
		}
	}

	for (int i = 0; i < 0x1000; i++) {
		pStar[i] = pTemp[0][i << 1];
		pStar[0x01000 + i] = pTemp[1][i << 1];
	}

	free(pTemp[0]);
	free(pTemp[1]);

	return 0;
}

// ----------------------------CPS2--------------------------------
// Load 1 rom and interleve in the CPS2 style:
// rom  : aa bb -- -- (4 bytes)
// --ba --ba --ba --ba --ba --ba --ba --ba 8 pixels (four bytes)
//                                                  (skip four bytes)

// memory 000000-100000 are in even word fields of first 080000 section
// memory 100000-200000 are in  odd word fields of first 080000 section
// i = ABCD nnnn nnnn nnnn nnnn n000
// s = 00AB Cnnn nnnn nnnn nnnn nnD0

inline static void Cps2Load100000(unsigned char* Tile, unsigned char* Sect, int nShift)
{
	unsigned char *pt, *pEnd, *ps;
	pt = Tile; pEnd = Tile + 0x100000; ps = Sect;

	do {
		unsigned int Pix;				// Eight pixels
		Pix  = SepTable[ps[0]];
		Pix |= SepTable[ps[1]] << 1;
		Pix <<= nShift;
		*((unsigned int*)pt) |= Pix;

		pt += 8; ps += 4;
	}
	while (pt < pEnd);
}

static int Cps2LoadOne(unsigned char* Tile, int nNum, int nWord, int nShift)
{
	unsigned char *Rom = NULL; int nRomLen = 0;
	unsigned char *pt, *pr;

	LoadUp(&Rom, &nRomLen, nNum);
	if (Rom == NULL) {
		return 1;
	}

	if (nWord == 0) {
		unsigned char*Rom2 = NULL; int nRomLen2 = 0;
		unsigned char*Rom3 = Rom;

		LoadUp(&Rom2, &nRomLen2, nNum + 1);
		if (Rom2 == NULL) {
			return 1;
		}

		nRomLen <<= 1;
		Rom = (unsigned char*)malloc(nRomLen);
		if (Rom == NULL) {
			free(Rom2);
			free(Rom3);
			return 1;
		}

		for (int i = 0; i < nRomLen2; i++) {
			Rom[(i << 1) + 0] = Rom3[i];
			Rom[(i << 1) + 1] = Rom2[i];
		}

		free(Rom2);
		free(Rom3);
	}

	// Go through each section
	pt = Tile; pr = Rom;
	for (int b = 0; b < nRomLen >> 19; b++) {
		Cps2Load100000(pt, pr,     nShift); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, nShift); pt += 0x100000;
		pr += 0x80000;
	}

	free(Rom);

	return 0;
}

int Cps2LoadTiles(unsigned char* Tile, int nStart)
{
	// left  side of 16x16 tiles
	Cps2LoadOne(Tile,     nStart,     1, 0);
	Cps2LoadOne(Tile,     nStart + 1, 1, 2);
	// right side of 16x16 tiles
	Cps2LoadOne(Tile + 4, nStart + 2, 1, 0);
	Cps2LoadOne(Tile + 4, nStart + 3, 1, 2);

	return 0;
}

int Cps2LoadTilesSIM(unsigned char* Tile, int nStart)
{
	Cps2LoadOne(Tile,     nStart,     0, 0);
	Cps2LoadOne(Tile,     nStart + 2, 0, 2);
	Cps2LoadOne(Tile + 4, nStart + 4, 0, 0);
	Cps2LoadOne(Tile + 4, nStart + 6, 0, 2);

	return 0;
}

// ----------------------------------------------------------------

// The file extension indicates the data contained in a file.
// it consists of 2 numbers optionally followed by a single letter.
// The letter indicates the version. The meaning for the nubmers
// is as follows:
// 01 - 02 : Z80 program
// 03 - 10 : 68K program (filenames ending with x contain the XOR table)
// 11 - 12 : QSound sample data
// 13 - nn : Graphics data

static unsigned int nGfxMaxSize;

static int CpsGetROMs(bool bLoad)
{
	char* pRomName;
	struct BurnRomInfo ri;

	unsigned char* CpsCodeLoad = CpsCode;
	unsigned char* CpsRomLoad = CpsRom;
	unsigned int CpsGfxLoad = 0;
	unsigned char* CpsZRomLoad = CpsZRom;
	unsigned char* CpsQSamLoad = (unsigned char*)CpsQSam;

	int nGfxNum = 0;

	if (bLoad) {
		if (!CpsCodeLoad || !CpsRomLoad || !CpsZRomLoad || !CpsQSamLoad) {
			return 1;
		}
	} else {
		nCpsCodeLen = nCpsRomLen = nCpsGfxLen = nCpsZRomLen = nCpsQSamLen = 0;

		nGfxMaxSize = 0;
		if (BurnDrvGetHardwareCode() & HARDWARE_CAPCOM_CPS2_SIMM) {
			nGfxMaxSize = ~0U;
		}
	}

	for (int i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {

		BurnDrvGetRomInfo(&ri, i);

		// SIMM Graphics ROMs
		if (BurnDrvGetHardwareCode() & HARDWARE_CAPCOM_CPS2_SIMM) {
			if ((ri.nType & BRF_GRA) && (ri.nType & 8)) {
				if (bLoad)
				{
					Cps2LoadTilesSIM(CpsGfx+CpsGfxLoad, i);
					CpsGfxLoad += ri.nLen * 8;
					i += 7;
				} else {
					nCpsGfxLen += ri.nLen;
				}
				continue;
			}
			// SIMM QSound sample ROMs
			if ((ri.nType & BRF_SND) && ((ri.nType & 15) == 13)) {
				if (bLoad) {
					BurnLoadRom(CpsQSamLoad, i, 1);
					BurnByteswap(CpsQSamLoad, ri.nLen);
					CpsQSamLoad += ri.nLen;
				} else {
					nCpsQSamLen += ri.nLen;
				}
				continue;
			}
			
			// Different interleave SIMM QSound sample ROMs
			if ((ri.nType & BRF_SND) && ((ri.nType & 15) == 15)) {
				if (bLoad) {
					BurnLoadRom(CpsQSamLoad + 1, i + 0, 2);
					BurnLoadRom(CpsQSamLoad + 0, i + 1, 2);
					i += 2;
				} else {
					nCpsQSamLen += ri.nLen;
				}
				continue;
			}
		}

		// 68K program ROMs
		if ((ri.nType & 7) == 1) {
			if (bLoad) {
				BurnLoadRom(CpsRomLoad, i, 1);
				CpsRomLoad += ri.nLen;
			} else {
				nCpsRomLen += ri.nLen;
			}
			continue;
		}
		// XOR tables
		if ((ri.nType & 7) == 2) {
			if (bLoad) {
				BurnLoadRom(CpsCodeLoad, i, 1);
				CpsCodeLoad += ri.nLen;
			} else {
				nCpsCodeLen += ri.nLen;
			}
			continue;
		}

		// Z80 program ROMs
		if ((ri.nType & 7) == 4) {
			if (bLoad) {
				BurnLoadRom(CpsZRomLoad, i, 1);
				CpsZRomLoad += ri.nLen;
			} else {
				nCpsZRomLen += ri.nLen;
			}
			continue;
		}

		// Normal Graphics ROMs
		if (ri.nType & BRF_GRA) {
			if (bLoad) {
				if (UpperReserved != 2)
					Cps2LoadTiles(CpsGfx+CpsGfxLoad, i);
				else
				{
					Cps2LoadTiles(GETCPSGFX(CpsGfxLoad), i);						
				}
				CpsGfxLoad += (nGfxMaxSize == ~0U ? ri.nLen : nGfxMaxSize) * 4;
				i += 3;
			} else {
				if (ri.nLen > nGfxMaxSize) {
					nGfxMaxSize = ri.nLen;
				}
				if (ri.nLen < nGfxMaxSize) {
					nGfxMaxSize = ~0U;
				}
				nCpsGfxLen += ri.nLen;
				nGfxNum++;
			}
			continue;
		}

		// QSound sample ROMs
		if (ri.nType & BRF_SND) {
			if (bLoad) {
				BurnLoadRom(CpsQSamLoad, i, 1);
				BurnByteswap(CpsQSamLoad, ri.nLen);
				CpsQSamLoad += ri.nLen;
			} else {
				nCpsQSamLen += ri.nLen;
			}
			continue;
		}
	}

	if (bLoad) {
#if 0
		for (unsigned int i = 0; i < nCpsCodeLen / 4; i++) {
			((unsigned int*)CpsCode)[i] ^= ((unsigned int*)CpsRom)[i];
		}
#endif
		cps2_decrypt_game_data();
		
		if (!nCpsCodeLen) return 1;
	} else {

		if (nGfxMaxSize != ~0U) {
			nCpsGfxLen = nGfxNum * nGfxMaxSize;
		}

#if 1 && defined FBA_DEBUG
		if (!nCpsCodeLen) {
			bprintf(PRINT_IMPORTANT, _T("  - 68K ROM size:\t0x%08X (Decrypted with key)\n"), nCpsRomLen);
		} else {
			bprintf(PRINT_IMPORTANT, _T("  - 68K ROM size:\t0x%08X (XOR table size: 0x%08X)\n"), nCpsRomLen, nCpsCodeLen);
		}
		bprintf(PRINT_IMPORTANT, _T("  - Z80 ROM size:\t0x%08X\n"), nCpsZRomLen);
		bprintf(PRINT_IMPORTANT, _T("  - Graphics data:\t0x%08X\n"), nCpsGfxLen);
		bprintf(PRINT_IMPORTANT, _T("  - QSound data:\t0x%08X\n"), nCpsQSamLen);
#endif

		if (/*!nCpsCodeLen ||*/ !nCpsRomLen || !nCpsGfxLen || !nCpsZRomLen || ! nCpsQSamLen) {
			return 1;
		}
	}

	return 0;
}

// ----------------------------------------------------------------

int CpsInit()
{
	int nMemLen, i;

	BurnSetRefreshRate(59.633333);

	if (!nCPS68KClockspeed) {
		if (!(Cps & 1)) {
			nCPS68KClockspeed = 11800000;
		} else {
			nCPS68KClockspeed = 8000000;
		}
	}
	nCPS68KClockspeed = nCPS68KClockspeed * 100 / nBurnFPS;

	if ( bBurnUseRomCache )
		nCpsCodeLen = BurnCacheBlockSize(2);		

	nMemLen = nCpsGfxLen + nCpsRomLen + nCpsCodeLen + nCpsZRomLen + nCpsQSamLen + nCpsAdLen;

	if (Cps1Qs == 1) {
		nMemLen += nCpsZRomLen * 2;
	}

		// Allocate Gfx, Rom and Z80 Roms
		CpsGfx = (unsigned char*)malloc(nMemLen);
		if (CpsGfx == NULL) {
			return 1;
		}
		memset(CpsGfx, 0, nMemLen);
		CpsRom  = CpsGfx + nCpsGfxLen;
		CpsCode = CpsRom + nCpsRomLen;
		if (Cps1Qs == 1) {
			CpsEncZRom = CpsCode + nCpsCodeLen;
			CpsZRom = CpsEncZRom + nCpsZRomLen * 2;
		} else {
			CpsZRom = CpsCode + nCpsCodeLen;
		}
		CpsQSam =(char*)(CpsZRom + nCpsZRomLen);
		CpsAd   =(unsigned char*)(CpsQSam + nCpsQSamLen);
		UpperReserved = 0;

	// Create Gfx addr mask
	for (i = 0; i < 31; i++) {
		if ((1 << i) >= (int)nCpsGfxLen) {
			break;
		}
	}
	nCpsGfxMask = (1 << i) - 1;

	// Offset to Scroll tiles
	if (!(Cps & 1)) {
		nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0x800000;
	} else {
		nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0;
	}

	if ( bBurnUseRomCache ) {
		BurnCacheRead(CpsRom, 0);
		BurnCacheRead(CpsZRom, 1);
		BurnCacheRead(CpsCode, 2);
		BurnCacheRead((unsigned char*)CpsQSam,3);
		BurnCacheRead(CpsGfxSeg[0],4);
		BurnCacheRead(CpsGfxSeg[1],5);
		BurnCacheRead(CpsAd,6);
	}
	else {
		if (nCpsZRomLen>=5) {
			// 77->cfff and rst 00 in case driver doesn't load
			CpsZRom[0] = 0x3E; CpsZRom[1] = 0x77;
			CpsZRom[2] = 0x32; CpsZRom[3] = 0xFF; CpsZRom[4] = 0xCF;
			CpsZRom[5] = 0xc7;
		}
	}

	// mprot
	CpsMProt[0] = 0x40;
	CpsMProt[1] = 0x42;
	CpsMProt[2] = 0x44;
	CpsMProt[3] = 0x46;

	// Board ID (ffight is the default)
	CpsBID[0] = 0x60;
	CpsBID[1] = 0x00;
	CpsBID[2] = 0x04;

	SepTableCalc();									  // Precalc the separate table

	CpsReset = 0; Cpi01A = Cpi01C = Cpi01E = 0;		  // blank other inputs

	nCpsLcReg = 0x66;								  // Layer controller usually at 0x66
	// Usual bits for layer enable
	CpsLayEn[1] = 2; CpsLayEn[2] = 4; CpsLayEn[3] = 8; CpsLayEn[4] = 0; CpsLayEn[5] = 0;

	return 0;
}

int Cps2Init()
{
	Cps = 2;

	if (CpsGetROMs(false)) {
		return 1;
	}

	CpsInit();

	if ( !bBurnUseRomCache )
		if (CpsGetROMs(true)) {
			return 1;
		}

	return CpsRunInit();
}

int CpsExit()
{
	if (!(Cps & 1)) {
		CpsRunExit();
	}

	CpsLayEn[1] = CpsLayEn[2] = CpsLayEn[3] = CpsLayEn[4] = CpsLayEn[5] = 0;
	nCpsLcReg = 0;
	nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0;
	nCpsGfxMask = 0;

	nCpsCodeLen = nCpsRomLen = nCpsGfxLen = nCpsZRomLen = nCpsQSamLen = nCpsAdLen = 0;
	CpsCode = CpsZRom = CpsAd = CpsStar = NULL;

	switch(UpperReserved)
	{
		case 0:
			free(CpsGfx);
		break;
		case 1:
		free(CpsRom);
		CpsRom = NULL;
		UpperFree(CpsGfx);
		break;
		case 2:
		free(CpsRom);
		CpsRom = NULL;
		free(CpsGfxSeg[1]);
		UpperFree(CpsGfxSeg[0]);
		UpperFree(CpsQSam);
		break;
	}
	CpsRom = NULL;
	CpsQSam = NULL;
	CpsGfx  = NULL;

	nCPS68KClockspeed = 0;
	Cps = 0;

	return 0;
}
