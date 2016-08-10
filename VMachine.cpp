
#include <stdio.h>
#include <iostream>
#include <string.h>
#include "system.h"
#include "VMachine.h"
#include "MKGenException.h"

#if defined(WINDOWS)
#include <conio.h>
#endif

using namespace std;

namespace MKBasic {

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
 
/*
 *--------------------------------------------------------------------
 * Method:		VMachine()
 * Purpose:		Default class constructor.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */ 
VMachine::VMachine()
{
	InitVM();
}

/*
 *--------------------------------------------------------------------
 * Method:		VMachine()
 * Purpose:		Custom class constructor.
 * Arguments:	romfname - name of the ROM definition file
 *						ramfname - name of the RAM definition file
 * Returns:		n/a
 *--------------------------------------------------------------------
 */ 
VMachine::VMachine(string romfname, string ramfname)
{
	InitVM();
	LoadROM(romfname);
	LoadRAM(ramfname);
}

/*
 *--------------------------------------------------------------------
 * Method:		~VMachine()
 * Purpose:		Class destructor.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
VMachine::~VMachine()
{
	delete mpDisp;
	delete mpCPU;
	delete mpROM;
	delete mpRAM;
}

/*
 *--------------------------------------------------------------------
 * Method:		InitVM()
 * Purpose:		Initialize class.
 * Arguments:	n/a
 * Returns:		n/a
f *--------------------------------------------------------------------
 */
void VMachine::InitVM()
{
	mOpInterrupt = false;
	mpRAM = new Memory();

	mOldStyleHeader = false;
	mError = VMERR_OK;
	mAutoExec = false;	
	mAutoReset = false;
	mCharIOAddr = CHARIO_ADDR;
	mCharIOActive = mCharIO = false;
	mGraphDispActive = false;
	if (NULL == mpRAM) {
		throw MKGenException("Unable to initialize VM (RAM).");
	}
	mRunAddr = mpRAM->Peek16bit(0xFFFC);	// address under RESET vector
	mpROM = new Memory();
	if (NULL == mpROM) {
		throw MKGenException("Unable to initialize VM (ROM).");
	}	
	mpCPU = new MKCpu(mpRAM);
	if (NULL == mpCPU) {
		throw MKGenException("Unable to initialize VM (CPU).");
	}
	mpDisp = new Display();
	if (NULL == mpDisp) {
		throw MKGenException("Unable to initialize VM (Display).");
	}		
}

#if defined(WINDOWS)

/*
 *--------------------------------------------------------------------
 * Method:		ClearScreen()
 * Purpose:		Clear the working are of the VM - DOS. 
 *            This is not a part of virtual display emulation.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ClearScreen()
{
  HANDLE                     hStdOut;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  DWORD                      count;
  DWORD                      cellCount;
  COORD                      homeCoords = { 0, 0 };

  hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
  if (hStdOut == INVALID_HANDLE_VALUE) return;

  /* Get the number of cells in the current buffer */
  if (!GetConsoleScreenBufferInfo( hStdOut, &csbi )) return;
  cellCount = csbi.dwSize.X *csbi.dwSize.Y;

  /* Fill the entire buffer with spaces */
  if (!FillConsoleOutputCharacter(
    hStdOut,
    (TCHAR) ' ',
    cellCount,
    homeCoords,
    &count
    )) return;

  /* Fill the entire buffer with the current colors and attributes */
  if (!FillConsoleOutputAttribute(
    hStdOut,
    csbi.wAttributes,
    cellCount,
    homeCoords,
    &count
    )) return;

  /* Move the cursor home */
  SetConsoleCursorPosition( hStdOut, homeCoords );
}

/*
 *--------------------------------------------------------------------
 * Method:		ScrHome()
 * Purpose:		Bring the console cursor to home position - DOS.
 *            This is not a part of virtual display emulation.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ScrHome()
{
  HANDLE                     hStdOut;
  COORD                      homeCoords = { 0, 0 };

  hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
  if (hStdOut == INVALID_HANDLE_VALUE) return;

  /* Move the cursor home */
  SetConsoleCursorPosition( hStdOut, homeCoords );
}

#endif

#if defined(LINUX)

/*
 *--------------------------------------------------------------------
 * Method:		ClearScreen()
 * Purpose:		Clear the working are of the VM - Linux. 
 *            This is not a part of virtual display emulation.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ClearScreen()
{
   system("clear");
}

/*
 *--------------------------------------------------------------------
 * Method:		ScrHome()
 * Purpose:		Bring the console cursor to home position - Linux.
 *            This is not a part of virtual display emulation.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ScrHome()
{
   cout << "\033[1;1H";
}

#endif

/*
 *--------------------------------------------------------------------
 * Method:		ShowDisp()
 * Purpose:		Show the emulated virtual text display device contents.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ShowDisp()
{
	if (mCharIOActive) {
			ScrHome();
			mpDisp->ShowScr();
	}	
}

/*
 *--------------------------------------------------------------------
 * Method:		Run()
 * Purpose:		Run VM until software break instruction.
 * Arguments:	n/a
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Run()
{
	Regs *cpureg = NULL;

	mOpInterrupt = false;
	ClearScreen();
	ShowDisp();
	while (true) {
		cpureg = Step();
		if (cpureg->CyclesLeft == 0 && mCharIO) {
			ShowDisp();
		}
		if (cpureg->SoftIrq || mOpInterrupt)
			break;
	}

	ShowDisp();	
	
	return cpureg;
}

/*
 *--------------------------------------------------------------------
 * Method:		Run()
 * Purpose:		Run VM from specified address until software break 
 *						instruction.
 * Arguments:	addr - start execution address
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Run(unsigned short addr)
{
	mRunAddr = addr;
	return Run();
}

/*
 *--------------------------------------------------------------------
 * Method:		Exec()
 * Purpose:		Run VM from current address until last RTS (if enabled).
 *            NOTE: Stack must be empty for last RTS to be trapped.
 * Arguments:	n/a
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Exec()
{
	Regs *cpureg = NULL;

	mOpInterrupt = false;
	ClearScreen();
	ShowDisp();
	while (true) {
		cpureg = Step();
		if (cpureg->CyclesLeft == 0 && mCharIO) {
			cout << mpDisp->GetLastChar();
			cout << flush;
		}
		if (cpureg->LastRTS || mOpInterrupt) break;
	}

	ShowDisp();	
	
	return cpureg;
}

/*
 *--------------------------------------------------------------------
 * Method:		Exec()
 * Purpose:		Run VM from specified address until RTS.
 * Arguments:	addr - start execution address
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Exec(unsigned short addr)
{
	mRunAddr = addr;
	return Exec();
}

/*
 *--------------------------------------------------------------------
 * Method:		Step()
 * Purpose:		Execute single opcode.
 * Arguments:	n/a
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Step()
{
	unsigned short addr = mRunAddr;
	Regs *cpureg = NULL;	
	
	cpureg = mpCPU->ExecOpcode(addr);
	if (mGraphDispActive) mpRAM->GraphDisp_ReadEvents();
	addr = cpureg->PtrAddr;
	mRunAddr = addr;
	
	if (cpureg->CyclesLeft == 0 && mCharIOActive && !mOpInterrupt) {
		char c = -1;
		mCharIO = false;

		while ((c = mpRAM->GetCharOut()) != -1) {
			mOpInterrupt = mOpInterrupt || (c == OPINTERRUPT);
			if (!mOpInterrupt) {
				mpDisp->PutChar(c);
				mCharIO = true;				
			}
		}
	}
	
	return cpureg;
}

/*
 *--------------------------------------------------------------------
 * Method:		Step()
 * Purpose:		Execute single opcode.
 * Arguments:	addr (unsigned short) - opcode address
 * Returns:		Pointer to CPU registers and flags.
 *--------------------------------------------------------------------
 */
Regs *VMachine::Step(unsigned short addr)
{
	mRunAddr = addr;
	return Step();
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadROM()
 * Purpose:		Load data from memory definition file to the memory.
 * Arguments:	romfname - name of the ROM file definition
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::LoadROM(string romfname)
{
	LoadMEM(romfname, mpROM);
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadRAM()
 * Purpose:		Load data from memory definition file to the memory.
 * Arguments:	ramfname - name of the RAM file definition
 * Returns:		int - error code
 *--------------------------------------------------------------------
 */
int VMachine::LoadRAM(string ramfname)
{
	int err = 0;
	eMemoryImageTypes memimg_type = GetMemoryImageType(ramfname);
	switch (memimg_type) {
		case MEMIMG_VM65DEF:		err = LoadMEM(ramfname, mpRAM); break;
		case MEMIMG_INTELHEX:		err = LoadRAMHex(ramfname); break;
		case MEMIMG_BIN:
		default:	// unknown type, try to read as binary
							// and hope for the best
			err = LoadRAMBin(ramfname);
			break;
	}
	mError = err;
	return err;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetMemoryImageType()
 * Purpose:		Detect format of memory image file.
 * Arguments:	ramfname - name of the RAM file definition
 * Returns:		eMemoryImageTypes - code of the memory image format
 *--------------------------------------------------------------------
 */
eMemoryImageTypes VMachine::GetMemoryImageType(string ramfname)
{
	eMemoryImageTypes ret = MEMIMG_UNKNOWN;
	char buf[256] = {0};
	FILE *fp = NULL;
	int n = 0;

	if ((fp = fopen(ramfname.c_str(), "rb")) != NULL) {
		memset(buf, 0, 256);
		while (0 == feof(fp) && 0 == ferror(fp)) {
			unsigned char val = fgetc(fp);
			buf[n++] = val;
			if (n >= 256) break;
		}
		fclose(fp);
	}
	bool possibly_intelhex = true;
	for (int i=0; i<256; i++) {
		char *pc = buf+i;
		if (isspace(buf[i])) continue;
		if (i < 256-9 // 256 less the length of the longest expected keyword
				&&
				(!strncmp(pc, "ADDR", 4)
				|| !strncmp(pc, "ORG", 3)
				|| !strncmp(pc, "IOADDR", 6)
				|| !strncmp(pc, "ROMBEGIN", 8)
				|| !strncmp(pc, "ROMEND", 6)
				|| !strncmp(pc, "ENROM", 5)
				|| !strncmp(pc, "ENIO", 4)
				|| !strncmp(pc, "EXEC", 4)
				|| !strncmp(pc, "RESET", 5)
				|| !strncmp(pc, "ENGRAPH", 7)
				|| !strncmp(pc, "GRAPHADDR", 9))
			 )
		{
			ret = MEMIMG_VM65DEF;
			break;
		}
		if (buf[i] != ':'
			  && buf[i] != '0'
			  && buf[i] != '1'
			  && buf[i] != '2'
			  && buf[i] != '3'
			  && buf[i] != '4'
			  && buf[i] != '5'
			  && buf[i] != '6'
			  && buf[i] != '7'
			  && buf[i] != '8'
			  && buf[i] != '9'
			  && tolower(buf[i]) != 'a'
			  && tolower(buf[i]) != 'b'
			  && tolower(buf[i]) != 'c'
			  && tolower(buf[i]) != 'd'
			  && tolower(buf[i]) != 'e'
			  && tolower(buf[i]) != 'f') 
		{
			possibly_intelhex = false;
		}
	}
	if (ret == MEMIMG_UNKNOWN && possibly_intelhex)
		ret = MEMIMG_INTELHEX;

	return ret;	
}

/*
 *--------------------------------------------------------------------
 * Method:		HasHdrData()
 * Purpose:		Check for header in the binary memory image.
 * Arguments:	File pointer.
 * Returns:		true if magic keyword found at the beginning of the
 *						memory image file, false otherwise
 *--------------------------------------------------------------------
 */
bool VMachine::HasHdrData(FILE *fp)
{
	bool ret = false;
	int n = 0, l = strlen(HDRMAGICKEY);
	char buf[20];

	memset(buf, 0, 20);
	
	rewind(fp);
	while (0 == feof(fp) && 0 == ferror(fp)) {
		unsigned char val = fgetc(fp);
		buf[n] = val;
		n++;
		if (n >= l) break;
	}
	ret = (0 == strncmp(buf, HDRMAGICKEY, l));

	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		HasOldHdrData()
 * Purpose:		Check for previous version header in the binary memory
 *            image.
 * Arguments:	File pointer.
 * Returns:		true if magic keyword found at the beginning of the
 *						memory image file, false otherwise
 *--------------------------------------------------------------------
 */
bool VMachine::HasOldHdrData(FILE *fp)
{
	bool ret = false;
	int n = 0, l = strlen(HDRMAGICKEY_OLD);
	char buf[20];

	memset(buf, 0, 20);
	
	rewind(fp);
	while (0 == feof(fp) && 0 == ferror(fp)) {
		unsigned char val = fgetc(fp);
		buf[n] = val;
		n++;
		if (n >= l) break;
	}
	ret = (0 == strncmp(buf, HDRMAGICKEY_OLD, l));

	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadHdrData()
 * Purpose:		Load data from binary image header.
 * Arguments:	File pointer.
 * Returns:		bool, true if success, false if error
 *
 * Details:
 *    Header of the binary memory image consists of magic keyword
 * string followed by the 128 bytes of data (unsigned char values). 
 * It has following format:
 *
 * MAGIC_KEYWORD
 * aabbccddefghijklmm[remaining unused bytes]
 *
 * Where:
 *    MAGIC_KEYWORD - text string indicating header, may vary between
 *                    versions thus rendering headers from previous
 *                    versions incompatible - currently: "SNAPSHOT2"
 *                    NOTE: Previous version of header is currently
 *                          recognized and can be read, the magic
 *                          keyword of previous version: "SNAPSHOT".
 *                          Old header had only 15 bytes of data.
 *                          This backwards compatibility will be
 *                          removed in next version as the new
 *                          format of header with 128 bytes for
 *                          data leaves space for expansion without
 *                          the need of changing file format.
 *    aa - low and hi bytes of execute address (PC)
 *    bb - low and hi bytes of char IO address
 *    cc - low and hi bytes of ROM begin address
 *    dd - low and hi bytes of ROM end address
 *    e - 0 if char IO is disabled, 1 if enabled
 *    f - 0 if ROM is disabled, 1 if enabled
 *    g - value in CPU Acc (accumulator) register
 *    h - value in CPU X (X index) register
 *    i - value in CPU Y (Y index) register
 *    j - value in CPU PS (processor status/flags)
 *    k - value in CPU SP (stack pointer) register
 *    l - 0 if generic graphics display device is disabled,
 *        1 if graphics display is enabled
 *    mm - low and hi bytes of graphics display base address
 *
 * NOTE:
 *   If magic keyword was detected, this part is already read and file
 *   pointer position is at the 1-st byte of data. Therefore this
 *   method does not have to read and skip the magic keyword.
 *--------------------------------------------------------------------
 */
bool VMachine::LoadHdrData(FILE *fp)
{
	int n = 0, l = 0, hdrdtlen = HDRDATALEN;
	unsigned short rb = 0, re = 0;
	Regs r;
	bool ret = false;

	if (mOldStyleHeader) hdrdtlen = HDRDATALEN_OLD;
	while (0 == feof(fp) && 0 == ferror(fp) && n < hdrdtlen) {
		unsigned char val = fgetc(fp);
		switch (n)
		{
			case 1:		mRunAddr = l + 256 * val;
								break;
			case 3:		mCharIOAddr = l + 256 * val;
								break;
			case 5:		rb = l + 256 * val;
								break;
			case 7: 	re = l + 256 * val;
								break;
			case 8: 	mCharIOActive = (val != 0);
								break;
			case 9: 	if (val != 0) {
									mpRAM->EnableROM(rb, re);
								} else {
									mpRAM->SetROM(rb, re);
								}
								break;
			case 10:	r.Acc = val;
								break;
			case 11:	r.IndX = val;
								break;
			case 12:	r.IndY = val;
								break;
			case 13:	r.Flags = val;
								break;
			case 14:	r.PtrStack = val;
								ret = true;
								break;
			case 15:	mGraphDispActive = (val != 0);
								break;
			case 17:	if (mGraphDispActive) SetGraphDisp(l + 256 * val);
								else DisableGraphDisp();
								break;
			default: 	break;
		}
		l = val;
		n++;
	}
	if (ret) {
		r.PtrAddr = mRunAddr;
		mpCPU->SetRegs(r);
	}

	return ret;
}

// Macro to save header data: v - value, fp - file pointer, n - data counter (dec)
#define SAVE_HDR_DATA(v,fp,n) {fputc(v, fp); n--;}

/*
 *--------------------------------------------------------------------
 * Method:		SaveHdrData()
 * Purpose:		Save header data to binary file (memory snapshot).
 * Arguments:	File pointer, must be opened for writing in binary mode.
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::SaveHdrData(FILE *fp)
{
	char buf[20] = {0};
	int n = HDRDATALEN;

	strcpy(buf, HDRMAGICKEY);
	for (unsigned int i = 0; i < strlen(HDRMAGICKEY); i++) {
		fputc(buf[i], fp);
	}
	Regs *reg = mpCPU->GetRegs();
	unsigned char lo = 0, hi = 0;
	lo = (unsigned char) (reg->PtrAddr & 0x00FF);
	hi = (unsigned char) ((reg->PtrAddr & 0xFF00) >> 8);
	SAVE_HDR_DATA(lo,fp,n);
	SAVE_HDR_DATA(hi,fp,n);
	lo = (unsigned char) (mCharIOAddr & 0x00FF);
	hi = (unsigned char) ((mCharIOAddr & 0xFF00) >> 8);
	SAVE_HDR_DATA(lo,fp,n);
	SAVE_HDR_DATA(hi,fp,n);	
	lo = (unsigned char) (GetROMBegin() & 0x00FF);
	hi = (unsigned char) ((GetROMBegin() & 0xFF00) >> 8);
	SAVE_HDR_DATA(lo,fp,n);
	SAVE_HDR_DATA(hi,fp,n);	
	lo = (unsigned char) (GetROMEnd() & 0x00FF);
	hi = (unsigned char) ((GetROMEnd() & 0xFF00) >> 8);
	SAVE_HDR_DATA(lo,fp,n);
	SAVE_HDR_DATA(hi,fp,n);	
	lo = (mCharIOActive ? 1 : 0);
	SAVE_HDR_DATA(lo,fp,n);	
	lo = (IsROMEnabled() ? 1 : 0);
	SAVE_HDR_DATA(lo,fp,n);	
	Regs *pregs = mpCPU->GetRegs();
	if (pregs != NULL) {
		SAVE_HDR_DATA(pregs->Acc,fp,n);
		SAVE_HDR_DATA(pregs->IndX,fp,n);
		SAVE_HDR_DATA(pregs->IndY,fp,n);
		SAVE_HDR_DATA(pregs->Flags,fp,n);
		SAVE_HDR_DATA(pregs->PtrStack,fp,n);
	}
	lo = (mGraphDispActive ? 1 : 0);
	SAVE_HDR_DATA(lo,fp,n);
	lo = (unsigned char) (GetGraphDispAddr() & 0x00FF);
	hi = (unsigned char) ((GetGraphDispAddr() & 0xFF00) >> 8);
	SAVE_HDR_DATA(lo,fp,n);
	SAVE_HDR_DATA(hi,fp,n);		
	// fill up the unused slots of header data with 0-s
	for (int i = n; i > 0; i--) fputc(0, fp);
}

/*
 *--------------------------------------------------------------------
 * Method:		SaveSnapshot()
 * Purpose:		Save current state of the VM and memory image.
 * Arguments: String - file name.
 * Returns:		int, 0 if successful, greater then 0 if not (# of bytes
 *            not written).
 *--------------------------------------------------------------------
 */
int VMachine::SaveSnapshot(string fname)
{
	FILE *fp = NULL;
	int ret = MAX_8BIT_ADDR+1;

	if ((fp = fopen(fname.c_str(), "wb")) != NULL) {
		SaveHdrData(fp);
		for (int addr = 0; addr < MAX_8BIT_ADDR+1; addr++) {
			if (addr != mCharIOAddr && addr != mCharIOAddr+1) {
				unsigned char b = mpRAM->Peek8bitImg((unsigned short)addr);
				if (EOF != fputc(b, fp)) ret--;
				else break;
			} else {
				if (EOF != fputc(0, fp)) ret--;
				else break;
			}
		}
		fclose(fp);
	}
	if (0 != ret) mError = VMERR_SAVE_SNAPSHOT;

	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadRAMBin()
 * Purpose:		Load data from binary image file to the memory.
 * Arguments:	ramfname - name of the RAM file definition
 * Returns:		int - error code
 *            MEMIMGERR_OK - OK
 * 						MEMIMGERR_RAMBIN_EOF
 *             - WARNING: Unexpected EOF (image shorter than 64kB).
 *						MEMIMGERR_RAMBIN_OPEN
 *             - WARNING: Unable to open memory image file.
 *						MEMIMGERR_RAMBIN_HDR
 *             - WARNING: Problem with binary image header.
 *						MEMIMGERR_RAMBIN_NOHDR
 *             - WARNING: No header found in binary image.
 *						MEMIMGERR_RAMBIN_HDRANDEOF
 *             - WARNING: Problem with binary image header and
 *                        Unexpected EOF (image shorter than 64kB).
 *						MEMIMGERR_RAMBIN_NOHDRANDEOF
 *             - WARNING: No header found in binary image and
 *                        Unexpected EOF (image shorter than 64kB).
 * TO DO:
 *  - Add fixed size header to binary image with emulator
 *    configuration data. Presence of the header will be detected
 *    by magic key at the beginning. Header should also include
 *    snapshot info, so the program can continue from the place
 *    where it was frozen/saved.
 *--------------------------------------------------------------------
 */
int VMachine::LoadRAMBin(string ramfname)
{
	FILE *fp = NULL;
	unsigned short addr = 0x0000;
	int n = 0;
	Memory *pm = mpRAM;
	int ret = MEMIMGERR_RAMBIN_OPEN;

	mOldStyleHeader = false;	
	if ((fp = fopen(ramfname.c_str(), "rb")) != NULL) {
		if (HasHdrData(fp) || (mOldStyleHeader = HasOldHdrData(fp))) {
			ret = (LoadHdrData(fp) ? MEMIMGERR_OK : MEMIMGERR_RAMBIN_HDR);
		} else {
			ret = MEMIMGERR_RAMBIN_NOHDR;
			rewind(fp);
		}
		// temporarily disable emulation facilities to allow
		// proper memory image initialization
		bool tmp1 = mCharIOActive, tmp2 = mpRAM->IsROMEnabled();
		DisableCharIO();
		DisableROM();
		while (0 == feof(fp) && 0 == ferror(fp)) {
			unsigned char val = fgetc(fp);
			pm->Poke8bitImg(addr, val);
			addr++; n++;
		}
		fclose(fp);
		// restore emulation facilities status
		if (tmp1) SetCharIO(mCharIOAddr, false);
		if (tmp2) EnableROM();
		if (n <= 0xFFFF) {
			switch (ret) {

				case MEMIMGERR_OK: 
					ret = MEMIMGERR_RAMBIN_EOF; 
					break;

				case MEMIMGERR_RAMBIN_HDR: 
					ret = MEMIMGERR_RAMBIN_HDRANDEOF; 
					break;

				case MEMIMGERR_RAMBIN_NOHDR: 
					ret = MEMIMGERR_RAMBIN_NOHDRANDEOF; 
					break;

				default: break;
			}
		}
	}
	mError = ret;

	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadRAMHex()
 * Purpose:		Load data from Intel HEX file format to memory.
 * Arguments:	hexfname - name of the HEX file
 * Returns:		int, MEMIMGERR_OK if OK, otherwise error code:
 *               MEMIMGERR_INTELH_OPEN - unable to open file
 *							 MEMIMGERR_INTELH_SYNTAX - syntax error
 *							 MEMIMGERR_INTELH_FMT - hex format error
 *--------------------------------------------------------------------
 */
int VMachine::LoadRAMHex(string hexfname)
{
		char line[256] = {0};
		FILE *fp = NULL;
		int ret = 0;
		unsigned int addr = 0;

		bool tmp1 = mCharIOActive, tmp2 = mpRAM->IsROMEnabled();
		DisableCharIO();
		DisableROM();
		if ((fp = fopen(hexfname.c_str(), "r")) != NULL) {
			while (0 == feof(fp) && 0 == ferror(fp)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				if (line[0] == ':') {
					if (0 == strcmp(line, HEXEOF)) {
						break;	// EOF, we are done here.
					}
					char blen[3] = {0,0,0};
					char baddr[5] = {0,0,0,0,0};
					char brectype[3] = {0,0,0};
					blen[0] = line[1];
					blen[1] = line[2];
					blen[2] = 0;
					baddr[0] = line[3];
					baddr[1] = line[4];
					baddr[2] = line[5];
					baddr[3] = line[6];
					baddr[4] = 0;
					brectype[0] = line[7];
					brectype[1] = line[8];
					brectype[2] = 0;
					unsigned int reclen = 0, rectype = 0;
					sscanf(blen, "%02x", &reclen);
					sscanf(baddr, "%04x", &addr);
					sscanf(brectype, "%02x", &rectype);
					if (reclen == 0 && rectype == 1) break;	// EOF, we are done here.
					if (rectype != 0) continue;	// not a data record, next!
					for (unsigned int i=9; i<reclen*2+9; i+=2,addr++) {
						if (i>=strlen(line)-3) {
							ret = MEMIMGERR_INTELH_FMT;	// hex format error
							break;
						}
						char dbuf[3] = {0,0,0};
						unsigned int byteval = 0;
						Memory *pm = mpRAM;
						dbuf[0] = line[i];
						dbuf[1] = line[i+1];
						dbuf[2] = 0;
						sscanf(dbuf, "%02x", &byteval);
						pm->Poke8bitImg(addr, (unsigned char)byteval&0x00FF);
					}
				} else {
					ret = MEMIMGERR_INTELH_SYNTAX;	// syntax error
					break;
				}
			}
			fclose(fp);
		} else {
			ret = MEMIMGERR_INTELH_OPEN;	// unable to open file
		}
		if (tmp1) SetCharIO(mCharIOAddr, false);
		if (tmp2) EnableROM();					

		mError = ret;

		return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadRAMDef()
 * Purpose:		Load RAM from VM65 memory definition file.
 * Arguments:	memfname - file name
 * Returns:		int - error code.
 *--------------------------------------------------------------------
 */
int VMachine::LoadRAMDef(string memfname)
{
	return LoadMEM(memfname, mpRAM);
}

/*
 *--------------------------------------------------------------------
 * Method:		LoadMEM()
 * Purpose:		Load data from VM65 memory definition file to the
 *            provided memory device.
 * Arguments: memfname - name of memory definition file
 *						pmem - pointer to memory object
 * Returns:		int - error code
 * Details:
 *    Format of the memory definition file:
 * [; comment]
 * [ADDR
 * address]
 * [data]
 * [ORG
 * address]
 * [data]
 * [IOADDR
 * address]
 * [ROMBEGIN
 * address]
 * [ROMEND
 * address]
 * [ENIO]
 * [ENROM]
 * [EXEC
 * addrress]
 * [ENGRAPH]
 * [GRAPHADDR
 * address]
 * [RESET]
 *
 * Where:
 * [] - optional token
 * ADDR - label indicating that starting address will follow in next
 *        line, it also defines run address
 * ORG - label indicating that the address counter will change to the
 *       value provided in next line
 * IOADDR - label indicating that char IO trap address will be defined
 *          in the next line
 * ROMBEGIN - label indicating that ROM begin address will be defined
 *            in the next line
 * ROMEND - label indicating that ROM end address will be defined
 *          in the next line
 * ENIO - label enabling char IO emulation
 * ENROM - label enabling ROM emulation
 * EXEC - label enabling auto-execute of code, address follows in the
 *        next line
 * ENGRAPH - enable generic graphics display device emulation with
 *           default base address
 * GRAPHADDR - label indicating that base address for generic graphics
 *             display device will follow in next line,
 *             also enables generic graphics device emulation, but
 *             with the customized base address
 * RESET     - initiate CPU reset sequence after loading memory definition file
 * address - decimal or hexadecimal (prefix $) address in memory
 * E.g:
 * ADDR
 * $200
 * 
 * or
 *
 * ADDR
 * 512
 * 
 * changes the default start address (256) to 512.
 *
 * ORG
 * 49152
 *
 * moves address counter to address 49152, following data will be
 * loaded from that address forward
 *
 * data - the multi-line stream of decimal of hexadecimal ($xx) values
 *        of size unsigned char (byte: 0-255) separated with spaces
 *        or commas. 
 * E.g.: 
 * $00 $00 $00 $00
 * $00 $00 $00 $00
 *
 * or
 *
 * $00,$00,$00,$00
 *
 * or
 *
 * 0 0 0 0
 *
 * or
 *
 * 0,0,0,0
 * 0 0 0 0 
 *--------------------------------------------------------------------
 */
int VMachine::LoadMEM(string memfname, Memory *pmem)
{
	FILE *fp = NULL;
	char line[256] = "\0";
	int lc = 0, errc = 0;
	unsigned short addr = 0, rombegin = 0, romend = 0;
	unsigned int nAddr, graphaddr = GRDISP_ADDR;
	bool enrom = false, enio = false, runset = false;
	bool ioset = false, execset = false, rombegset = false;
	bool romendset = false, engraph = false, graphset = false;
	Memory *pm = pmem;
	int err = MEMIMGERR_OK;
	
	if ((fp = fopen(memfname.c_str(), "r")) != NULL) {
		DisableROM();
		DisableCharIO();
		while (0 == feof(fp) && 0 == ferror(fp))
		{
			line[0] = '\0';			
			fgets(line, 256, fp);
			lc++;
			// change run address (can be done only once)
			if (0 == strncmp(line, "ADDR", 4)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!runset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						addr = nAddr;
					} else {
						addr = (unsigned short) atoi(line);				
					}
					mRunAddr = addr;
					runset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: Run address was already set. Ignoring..." << endl;
				}
				continue;
			}
			// change address counter
			if (0 == strncmp(line, "ORG", 3)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (*line == '$') {
					sscanf(line+1, "%04x", &nAddr);
					addr = nAddr;
				} else {
					addr = (unsigned short) atoi(line);				
				}	
				continue;
			}
			// define I/O emulation address (once)
			if (0 == strncmp(line, "IOADDR", 6)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!ioset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						mCharIOAddr = nAddr;
					} else {
						mCharIOAddr = (unsigned short) atoi(line);				
					}	
					ioset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: I/O address was already set. Ignoring..." << endl;
				}
				continue;
			}
			// define generic graphics display device base address (once)
			if (0 == strncmp(line, "GRAPHADDR", 9)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!graphset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						graphaddr = nAddr;
					} else {
						graphaddr = (unsigned short) atoi(line);				
					}	
					graphset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: graphics device base address was already set. Ignoring..." << endl;
				}
				continue;
			}			
			// enable character I/O emulation
			if (0 == strncmp(line, "ENIO", 4)) {
				enio = true;
				continue;
			}
			// enable generic graphics display emulation
			if (0 == strncmp(line, "ENGRAPH", 7)) {
				engraph = true;
				continue;
			}			
			// enable ROM emulation
			if (0 == strncmp(line, "ENROM", 5)) {
				enrom = true;
				continue;
			}			
			// auto execute from address
			if (0 == strncmp(line, "EXEC", 4)) {
				mAutoExec = true;
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!execset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						mRunAddr = nAddr;
					} else {
						mRunAddr = (unsigned short) atoi(line);				
					}	
					execset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: auto-exec address was already set. Ignoring..." << endl;
				}
				continue;
			}
			// auto reset
			if (0 == strncmp(line, "RESET", 5)) {
				mAutoReset = true;
				continue;
			}			
			// define ROM begin address
			if (0 == strncmp(line, "ROMBEGIN", 8)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!rombegset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						rombegin = nAddr;
					} else {
						rombegin = (unsigned short) atoi(line);
					}	
					rombegset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: ROM-begin address was already set. Ignoring..." << endl;
				}
				continue;
			}
			// define ROM end address
			if (0 == strncmp(line, "ROMEND", 6)) {
				line[0] = '\0';
				fgets(line, 256, fp);
				lc++;
				if (!romendset) {
					if (*line == '$') {
						sscanf(line+1, "%04x", &nAddr);
						romend = nAddr;
					} else {
						romend = (unsigned short) atoi(line);
					}	
					romendset = true;
				} else {
					err = MEMIMGERR_VM65_IGNPROCWRN;
					errc++;
					cout << "LINE #" << dec << lc << " WARNING: ROM-end address was already set. Ignoring..." << endl;
				}
				continue;
			}			
			if (';' == *line) continue; // skip comment lines
			char *s = strtok (line, " ,");
			while (NULL != s) {
				unsigned int nVal;
				if (*s == '$') {
					sscanf(s+1, "%02x", &nVal);
					pm->Poke8bitImg(addr++, (unsigned short)nVal);
				} else {
					pm->Poke8bitImg(addr++, (unsigned short)atoi(s));
				}
				s = strtok(NULL, " ,");
			}
		}
		fclose(fp);
		if (rombegin > MIN_ROM_BEGIN && romend > rombegin) {
			if (enrom)
				pm->EnableROM(rombegin, romend);
			else
				pm->SetROM(rombegin, romend);
		} else {
			if (enrom) EnableROM();
		}
		if (enio) {
			SetCharIO(mCharIOAddr, false);
		}
		if (engraph || graphset) {
			SetGraphDisp(graphaddr);
		}
	}
	else {
		err = MEMIMGERR_VM65_OPEN;
		cout << "WARNING: Unable to open memory definition file: " << memfname << endl;
		errc++;
	}	
	if (errc) {
		cout << "Found " << dec << errc << ((errc > 1) ? " problems." : " problem.") << endl;
		cout << "Press [ENTER] to continue...";
		getchar();		
	}	

	mError = err;

	return err;
}

/*
 *--------------------------------------------------------------------
 * Method:		MemPeek8bit()
 * Purpose:		Read value from specified RAM address.
 * Arguments:	addr - RAM address (0..0xFFFF)
 * Returns:		unsigned short - value read from specified RAM address
 *--------------------------------------------------------------------
 */
unsigned short VMachine::MemPeek8bit(unsigned short addr)
{
	unsigned short ret = 0;
	
	ret = (unsigned short)mpRAM->Peek8bit(addr);
	
	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		MemPoke8bit()
 * Purpose:		Write value to specified RAM address.
 * Arguments:	addr - RAM address (0..0xFFFF)
 *            v - 8-bit byte value
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::MemPoke8bit(unsigned short addr, unsigned char v)
{
	mpRAM->Poke8bit(addr, v);
}

/*
 *--------------------------------------------------------------------
 * Method:		GetRegs()
 * Purpose:		Return pointer to CPU status register.
 * Arguments:	n/a
 * Returns:		pointer to status register
 *--------------------------------------------------------------------
 */
Regs *VMachine::GetRegs()
{
	return mpCPU->GetRegs();
}

/*
 *--------------------------------------------------------------------
 * Method:		SetCharIO()
 * Purpose:		Activates and sets an address of basic character I/O
 *            emulation (console).
 * Arguments:	addr - address of I/O area (0x0000..0xFFFF)
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::SetCharIO(unsigned short addr, bool echo)
{
	mCharIOAddr = addr;
	mCharIOActive = true;
	mpRAM->SetCharIO(addr, echo);
	mpDisp->ClrScr();
}

/*
 *--------------------------------------------------------------------
 * Method:		DisableCharIO()
 * Purpose:		Deactivates basic character I/O emulation (console).
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::DisableCharIO()
{
	mCharIOActive = false;
	mpRAM->DisableCharIO();
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharIOAddr()
 * Purpose:		Returns current address of basic character I/O area.
 * Arguments:	n/a
 * Returns:		address of I/O area
 *--------------------------------------------------------------------
 */
unsigned short VMachine::GetCharIOAddr()
{
	return mCharIOAddr;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharIOActive()
 * Purpose:		Returns status of character I/O emulation.
 * Arguments:	n/a
 * Returns:		true if I/O emulation active
 *--------------------------------------------------------------------
 */
bool VMachine::GetCharIOActive()
{
	return mCharIOActive;
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
void VMachine::SetGraphDisp(unsigned short addr)
{
	mGraphDispActive = true;
	mpRAM->SetGraphDisp(addr);
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
void VMachine::DisableGraphDisp()
{
	mGraphDispActive = false;
	mpRAM->DisableGraphDisp();
}

/*
 *--------------------------------------------------------------------
 * Method:		GetGraphDispAddr()
 * Purpose:		Return base address of graphics display.
 * Arguments:	n/a
 * Returns:		unsigned short - address ($0000 - $FFFF)
 *--------------------------------------------------------------------
 */
unsigned short VMachine::GetGraphDispAddr()
{
	return mpRAM->GetGraphDispAddr();
}

/*
 *--------------------------------------------------------------------
 * Method:		GetGraphDispActive()
 * Purpose:		Returns status of graphics display emulation.
 * Arguments:	n/a
 * Returns:		true if graphics display emulation is active
 *--------------------------------------------------------------------
 */
bool VMachine::GetGraphDispActive()
{
	return mGraphDispActive;
}

/*
 *--------------------------------------------------------------------
 * Method:		ShowIO()
 * Purpose:		Show contents of emulated char I/O.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::ShowIO()
{
	if (mCharIOActive)
		mpDisp->ShowScr();
}

/*
 *--------------------------------------------------------------------
 * Method:		IsAutoExec()
 * Purpose:		Return status of auto-execute flag.
 * Arguments:	n/a
 * Returns:		bool - true if auto-exec flag is enabled.
 *--------------------------------------------------------------------
 */
bool VMachine::IsAutoExec()
{
	return mAutoExec;
}

/*
 *--------------------------------------------------------------------
 * Method:		IsAutoReset()
 * Purpose:		Return status of auto-reset flag.
 * Arguments:	n/a
 * Returns:		bool - true if auto-exec flag is enabled.
 *--------------------------------------------------------------------
 */
bool VMachine::IsAutoReset()
{
	return mAutoReset;
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
void VMachine::EnableROM()
{
	mpRAM->EnableROM();
}
		
/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
void VMachine::DisableROM()
{
	mpRAM->DisableROM();
}
		
/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
void VMachine::SetROM(unsigned short start, unsigned short end)
{
	mpRAM->SetROM(start, end);
}
		
/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
void VMachine::EnableROM(unsigned short start, unsigned short end)
{
	mpRAM->EnableROM(start, end);
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
unsigned short VMachine::GetROMBegin()
{
	return mpRAM->GetROMBegin();
}
		
/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
unsigned short VMachine::GetROMEnd()
{
	return mpRAM->GetROMEnd();
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
bool VMachine::IsROMEnabled()
{
	return mpRAM->IsROMEnabled();
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */		
unsigned short VMachine::GetRunAddr()
{
	return mRunAddr;
}

/*
 *--------------------------------------------------------------------
 * Method:		SetOpInterrupt()
 * Purpose:		Set the flag indicating operator interrupt.
 * Arguments:	bool - new value of the flag
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void VMachine::SetOpInterrupt(bool opint)
{
	mOpInterrupt = opint;
}

/*
 *--------------------------------------------------------------------
 * Method:		IsOpInterrupt()
 * Purpose:		Return the flag indicating operator interrupt status.
 * Arguments:	n/a
 * Returns:		bool - true if operator interrupt flag was set
 *--------------------------------------------------------------------
 */
bool VMachine::IsOpInterrupt()
{
	return mOpInterrupt;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetExecHistory()
 * Purpose:		Return history of executed opcodes (last 20).
 * Arguments:	n/a
 * Returns:		queue<string>
 *--------------------------------------------------------------------
 */
queue<string> VMachine::GetExecHistory()
{
	return mpCPU->GetExecHistory();
}

/*
 *--------------------------------------------------------------------
 * Method:		Disassemble()
 * Purpose:		Disassemble code in memory. Return next instruction
 *            address.
 * Arguments:	addr - address in memory
 *            buf - character buffer for disassembled instruction
 * Returns:		unsigned short - address of next instruction
 *--------------------------------------------------------------------
 */
unsigned short VMachine::Disassemble(unsigned short addr, char *buf)
{
	return mpCPU->Disassemble(addr, buf);
}

/*
 *--------------------------------------------------------------------
 * Method:		Reset()
 * Purpose:		Reset VM and CPU (should never return except operator
 *            induced interrupt).
 * Arguments: n/a
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
void VMachine::Reset()
{
	mpCPU->Reset();
	Exec(mpCPU->GetRegs()->PtrAddr);
	mpCPU->mExitAtLastRTS = true;
}

/*
 *--------------------------------------------------------------------
 * Method:		Interrupt()
 * Purpose:		Send Interrupt ReQuest to CPU (set the IRQ line LOW).
 * Arguments: n/a
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
void VMachine::Interrupt()
{
	mpCPU->Interrupt();
}

/*
 *--------------------------------------------------------------------
 * Method:		GetLastError()
 * Purpose:		Return error code set by last operation. Reset error
 *            code to OK.
 * Arguments: n/a
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
int VMachine::GetLastError()
{
	int ret = mError;
	mError = MEMIMGERR_OK;
	return ret;
}

} // namespace MKBasic