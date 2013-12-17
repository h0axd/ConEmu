/*
Copyright (c) 1996 Eugene Roshal
Copyright (c) 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <windows.h>
#include <TlHelp32.h>
#include "Execute.h"
#include "WinObjects.h"

// ��������� ����� �� �������� GetFileInfo, �������� ����������� ���������� � ���� PE-������

// ���������� ��������� IMAGE_SUBSYSTEM_* ���� ������� ��������
// ��� ������ �� ��������� IMAGE_SUBSYTEM_UNKNOWN ��������
// "���� �� �������� �����������".
// ��� DOS-���������� ��������� ��� ���� �������� �����.

// 17.12.2010 Maks
// ���� GetImageSubsystem ������ true - �� ����� ����� ��������� ��������� ��������
// IMAGE_SUBSYSTEM_WINDOWS_CUI    -- Win Console (32/64)
// IMAGE_SUBSYSTEM_DOS_EXECUTABLE -- DOS Executable (ImageBits == 16)

//#define IMAGE_SUBSYSTEM_DOS_EXECUTABLE  255

struct IMAGE_HEADERS
{
	DWORD Signature;
	IMAGE_FILE_HEADER FileHeader;
	union
	{
		IMAGE_OPTIONAL_HEADER32 OptionalHeader32;
		IMAGE_OPTIONAL_HEADER64 OptionalHeader64;
	};
};

bool GetImageSubsystem(const wchar_t *FileName,DWORD& ImageSubsystem,DWORD& ImageBits/*16/32/64*/)
{
	bool Result = false;
	ImageSubsystem = IMAGE_SUBSYSTEM_UNKNOWN;
	ImageBits = 32;
	HANDLE hModuleFile = CreateFile(FileName,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
	if (hModuleFile != INVALID_HANDLE_VALUE)
	{
		IMAGE_DOS_HEADER DOSHeader;
		DWORD ReadSize;
		if (ReadFile(hModuleFile,&DOSHeader,sizeof(DOSHeader),&ReadSize,NULL))
		{
			if (DOSHeader.e_magic != IMAGE_DOS_SIGNATURE)
			{
				const wchar_t *pszExt = wcsrchr(FileName, L'.');
				if (lstrcmpiW(pszExt, L".com") == 0)
				{
					ImageSubsystem = IMAGE_SUBSYSTEM_DOS_EXECUTABLE;
					ImageBits = 16;
					Result = true;
				}
			}
			else
			{
				ImageSubsystem = IMAGE_SUBSYSTEM_DOS_EXECUTABLE;
				ImageBits = 16;
				Result = true;
				if (SetFilePointer(hModuleFile,DOSHeader.e_lfanew,NULL,FILE_BEGIN))
				{
					IMAGE_HEADERS PEHeader;
					if (ReadFile(hModuleFile,&PEHeader,sizeof(PEHeader),&ReadSize,NULL))
					{
						if (PEHeader.Signature == IMAGE_NT_SIGNATURE)
						{
							switch (PEHeader.OptionalHeader32.Magic)
							{
							case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
								{
									ImageSubsystem = PEHeader.OptionalHeader32.Subsystem;
									ImageBits = 32;
								}
								break;
							case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
								{
									ImageSubsystem = PEHeader.OptionalHeader64.Subsystem;
									ImageBits = 64;
								}
								break;
							/*default:
								{
									// unknown magic
								}*/
							}
						}
						else if ((WORD)PEHeader.Signature == IMAGE_OS2_SIGNATURE)
						{
							ImageBits = 32;
							/*
							NE,  ���...  � ��� ���������� ��� ��� ������?

							Andrzej Novosiolov <andrzej@se.kiev.ua>
							AN> ��������������� �� ����� "Target operating system" NE-���������
							AN> (1 ���� �� �������� 0x36). ���� ��� Windows (�������� 2, 4) - �������������
							AN> GUI, ���� OS/2 � ������ �������� (��������� ��������) - ������������� �������.
							*/
							BYTE ne_exetyp = reinterpret_cast<PIMAGE_OS2_HEADER>(&PEHeader)->ne_exetyp;
							if (ne_exetyp==2||ne_exetyp==4)
							{
								ImageSubsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
							}
							else
							{
								ImageSubsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
							}
						}
						/*else
						{
							// unknown signature
						}*/
					}
					/*else
					{
						// ������ ����� � ������� ���������� ��������� ;-(
					}*/
				}
				/*else
				{
					// ������ ������� ���� ���� � �����, �.�. dos_head.e_lfanew ������
					// ������� � �������������� ����� (�������� ��� ������ ���� DOS-����)
				}*/
			}
			/*else
			{
				// ��� �� ����������� ���� - � ���� ���� ��������� MZ, ��������, NLM-������
				// TODO: ����� ����� ��������� POSIX �������, �������� "/usr/bin/sh"
			}*/
		}
		/*else
		{
			// ������ ������
		}*/
		CloseHandle(hModuleFile);
	}
	/*else
	{
		// ������ ��������
	}*/
	return Result;
}



/* ******************************************* */
/* PE File Info (kernel32.dll -> LoadLibraryW) */
/* ******************************************* */
struct IMAGE_MAPPING
{
	union
	{
		LPBYTE ptrBegin;
		PIMAGE_DOS_HEADER pDos;
	};
	LPBYTE ptrEnd;
	IMAGE_HEADERS* pHdr;
};
static bool ValidateMemory(LPVOID ptr, DWORD_PTR nSize, IMAGE_MAPPING* pImg)
{
	if ((ptr == NULL) || (((LPBYTE)ptr) < pImg->ptrBegin))
		return false;
	if ((((LPBYTE)ptr) + nSize) >= pImg->ptrEnd)
		return false;
	return true;
}

//================================================================================
//
// Given an RVA, look up the section header that encloses it and return a
// pointer to its IMAGE_SECTION_HEADER
//
PIMAGE_SECTION_HEADER GetEnclosingSectionHeader(DWORD rva, IMAGE_MAPPING* pImg)
{
	// IMAGE_FIRST_SECTION doesn't need 32/64 versions since the file header is the same either way.
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(pImg->pHdr);
	unsigned i;

	for (i = 0; i < pImg->pHdr->FileHeader.NumberOfSections; i++, section++)
	{
		// This 3 line idiocy is because Watcom's linker actually sets the
		// Misc.VirtualSize field to 0.  (!!! - Retards....!!!)
		DWORD size = section->Misc.VirtualSize;
		if (0 == size)
			size = section->SizeOfRawData;

		// Is the RVA within this section?
		if ( (rva >= section->VirtualAddress) && 
			(rva < (section->VirtualAddress + size)))
			return section;
	}

	return NULL;
}

LPVOID GetPtrFromRVA(DWORD rva, IMAGE_MAPPING* pImg)
{
	if (!pImg || !pImg->ptrBegin || !pImg->pHdr)
	{
		_ASSERTE(pImg!=NULL && pImg->ptrBegin!=NULL && pImg->pHdr!=NULL);
		return NULL;
	}

	PIMAGE_SECTION_HEADER pSectionHdr;
	INT delta;

	pSectionHdr = GetEnclosingSectionHeader(rva, pImg);
	if (!pSectionHdr)
		return NULL;

	delta = (INT)(pSectionHdr->VirtualAddress - pSectionHdr->PointerToRawData);
	return (LPVOID)(pImg->ptrBegin + rva - delta);
}

int ParseExportsSection(IMAGE_MAPPING* pImg)
{
	PIMAGE_EXPORT_DIRECTORY pExportDir;
	//PIMAGE_SECTION_HEADER header;
	//INT delta; 
	//PSTR pszFilename;
	DWORD i;
	PDWORD pdwFunctions;
	PWORD pwOrdinals;
	DWORD *pszFuncNames;
	DWORD exportsStartRVA; //, exportsEndRVA;
	LPCSTR pszFuncName;

	//exportsStartRVA = GetImgDirEntryRVA(pNTHeader,IMAGE_DIRECTORY_ENTRY_EXPORT);
	//exportsEndRVA = exportsStartRVA +
	//	GetImgDirEntrySize(pNTHeader, IMAGE_DIRECTORY_ENTRY_EXPORT);
	if (pImg->pHdr->OptionalHeader64.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		exportsStartRVA = pImg->pHdr->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
		//exportDirSize = hdr.OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
		//pExportDirAddr = GetPtrFromRVA(exportsStartRVA, (PIMAGE_NT_HEADERS64)&hdr, mi.modBaseAddr);
	}
	else
	{
		exportsStartRVA = pImg->pHdr->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
		//exportDirSize = hdr.OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
		//pExportDirAddr = GetPtrFromRVA(exportsStartRVA, (PIMAGE_NT_HEADERS32)&hdr, mi.modBaseAddr);
	}

	// Get the IMAGE_SECTION_HEADER that contains the exports.  This is
	// usually the .edata section, but doesn't have to be.
	//header = GetEnclosingSectionHeader( exportsStartRVA, pNTHeader );
	//if ( !header )
	//	return -201;

	//delta = (INT)(header->VirtualAddress - header->PointerToRawData);

	pExportDir = (PIMAGE_EXPORT_DIRECTORY)GetPtrFromRVA(exportsStartRVA, pImg);
	if (!pExportDir || !ValidateMemory(pExportDir, sizeof(IMAGE_EXPORT_DIRECTORY), pImg))
		return -201;

	//pszFilename = (PSTR)GetPtrFromRVA( pExportDir->Name, pNTHeader, pImageBase );

	pdwFunctions =	(PDWORD)GetPtrFromRVA(pExportDir->AddressOfFunctions, pImg);
	pwOrdinals =	(PWORD) GetPtrFromRVA(pExportDir->AddressOfNameOrdinals, pImg);
	pszFuncNames =	(DWORD*)GetPtrFromRVA(pExportDir->AddressOfNames, pImg);

	if (!pdwFunctions || !pwOrdinals || !pszFuncNames)
		return -202;

	for (i=0; i < pExportDir->NumberOfFunctions; i++, pdwFunctions++)
	{
		DWORD entryPointRVA = *pdwFunctions;

		if ( entryPointRVA == 0 )   // Skip over gaps in exported function
			continue;               // ordinals (the entrypoint is 0 for
									// these functions).

		// See if this function has an associated name exported for it.
		for ( unsigned j=0; j < pExportDir->NumberOfNames; j++ )
		{
			if ( pwOrdinals[j] == i )
			{
				pszFuncName = (LPCSTR)GetPtrFromRVA(pszFuncNames[j], pImg);
				if (pszFuncName)
				{
					if (strcmp(pszFuncName, "LoadLibraryW"))
					{
						// �����
						return entryPointRVA;
					}
				}
			}
		}
	}

	return -203;
}

static int FindLoadLibrary(LPCWSTR asKernel32)
{
	int nLoadLibraryOffset = 0;
	MWow64Disable wow; wow.Disable(); // ��������� � Win64 ��������. ���� �� ����� - ������ �� ������.

	HANDLE hMapping = NULL, hKernel = NULL;
	LPBYTE ptrMapping = NULL;
	LARGE_INTEGER nFileSize;
	
	hKernel = CreateFile(asKernel32, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (!hKernel || (hKernel == INVALID_HANDLE_VALUE))
		nLoadLibraryOffset = -101;
	else if (!GetFileSizeEx(hKernel, &nFileSize) || nFileSize.HighPart)
		nLoadLibraryOffset = -102;
	else if (nFileSize.LowPart < (sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_HEADERS)))
		nLoadLibraryOffset = -103;
	else if (!(hMapping = CreateFileMapping(hKernel, NULL, PAGE_READONLY, 0,0, NULL)) || (hMapping == INVALID_HANDLE_VALUE))
		nLoadLibraryOffset = -104;
	else if (!(ptrMapping = (LPBYTE)MapViewOfFile(hMapping, FILE_MAP_READ, 0,0,0)))
		nLoadLibraryOffset = -105;
	else // �������
	{
		IMAGE_MAPPING img;
		img.pDos = (PIMAGE_DOS_HEADER)ptrMapping;
		img.pHdr = (IMAGE_HEADERS*)(ptrMapping + img.pDos->e_lfanew);
		img.ptrEnd = (ptrMapping + nFileSize.LowPart);
		
		if (img.pDos->e_magic != IMAGE_DOS_SIGNATURE)
			nLoadLibraryOffset = -110; // ������������ ��������� - ������ ���� 'MZ'
		else if (!ValidateMemory(img.pHdr, sizeof(*img.pHdr), &img))
			nLoadLibraryOffset = -111;
		else if (img.pHdr->Signature != IMAGE_NT_SIGNATURE)
			nLoadLibraryOffset = -112;
		else if (img.pHdr->OptionalHeader32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
			 &&  img.pHdr->OptionalHeader64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			nLoadLibraryOffset = -113;
		else // OK, ���� ��������� ��������� ������
		{
			nLoadLibraryOffset = ParseExportsSection(&img);
		}
	}

	// ��������� �����������
	if (ptrMapping)
		UnmapViewOfFile(ptrMapping);
	if (hMapping && (hMapping != INVALID_HANDLE_VALUE))
		CloseHandle(hMapping);
	if (hKernel && (hKernel != INVALID_HANDLE_VALUE))
		CloseHandle(hKernel);

	// Found result
	return nLoadLibraryOffset;
}

// ���������� ����� ��������� LoadLibraryW ��� ����������� ��������
int FindKernelAddress(HANDLE ahProcess, DWORD anPID, DWORD* pLoadLibrary)
{
	int iRc = -100;
	*pLoadLibrary = NULL;

	int nBits = 0;
	SIZE_T hdrReadSize;
	IMAGE_DOS_HEADER dos;
	IMAGE_HEADERS hdr;
	MODULEENTRY32 mi = {sizeof(MODULEENTRY32)};
	// Must be TH32CS_SNAPMODULE32 for spy 32bit from 64bit process
	// ������� ������� ��� Native, ���� ������� �������� ������ �������� - ����������� snapshoot
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, anPID);
	if (!hSnap || hSnap == INVALID_HANDLE_VALUE)
		iRc = -1;
	else
	{
		WARNING("�� ���� ���������, 32-������ ������� �� ����� �������� ���������� � 64-������!");
		// ������ ����� ���������� �������� ��������
		if (!Module32First(hSnap, &mi))
			iRc = -2;
		else if (!ReadProcessMemory(ahProcess, mi.modBaseAddr, &dos, sizeof(dos), &hdrReadSize))
			iRc = -3;
		else if (dos.e_magic != IMAGE_DOS_SIGNATURE)
			iRc = -4; // ������������ ��������� - ������ ���� 'MZ'
		else if (!ReadProcessMemory(ahProcess, mi.modBaseAddr+dos.e_lfanew, &hdr, sizeof(hdr), &hdrReadSize))
			iRc = -5;
		else if (hdr.Signature != IMAGE_NT_SIGNATURE)
			iRc = -6;
		else if (hdr.OptionalHeader32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
			 &&  hdr.OptionalHeader64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			iRc = -7;
		else
		{
			TODO("������������, ��� ����� ����������� ���������� IMAGE_OS2_SIGNATURE?");
			nBits = (hdr.OptionalHeader32.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) ? 32 : 64;
			#ifdef WIN64
			// ���� ahProcess - 64 ����, �� ����� ����������� snapshoot � ������ TH32CS_SNAPMODULE32
			// � ��������, �� ���� �������� �� ������, �.�. ConEmuC.exe - 32������.
			if (nBits == 32)
			{
				CloseHandle(hSnap);
				hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE32, anPID);
				if (!hSnap || hSnap == INVALID_HANDLE_VALUE)
				{
					iRc = -8;
					hSnap = NULL;
				}
				else if (!Module32First(hSnap, &mi))
				{
					iRc = -9;
					CloseHandle(hSnap);
					hSnap = NULL;
				}
			}
			#endif
			if (hSnap != NULL)
			{
				iRc = (hdr.OptionalHeader32.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) ? -20 : -21;
				do {
					if (lstrcmpi(mi.szModule, L"kernel32.dll") == 0)
					{
						//if (!ReadProcessMemory(ahProcess, mi.modBaseAddr, &dos, sizeof(dos), &hdrReadSize))
						//	iRc = -23;
						//else if (dos.e_magic != IMAGE_DOS_SIGNATURE)
						//	iRc = -24; // ������������ ��������� - ������ ���� 'MZ'
						//else if (!ReadProcessMemory(ahProcess, mi.modBaseAddr+dos.e_lfanew, &hdr, sizeof(hdr), &hdrReadSize))
						//	iRc = -25;
						//else if (hdr.Signature != IMAGE_NT_SIGNATURE)
						//	iRc = -26;
						//else if (hdr.OptionalHeader32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
						//	&&  hdr.OptionalHeader64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
						//	iRc = -27;
						//else
						iRc = 0;
						break;
					}
				} while (Module32Next(hSnap, &mi));
			}
		}
		if (hSnap)
			CloseHandle(hSnap);
	}

	// ���� kernel32.dll ����� � �������������� ��������
	if (iRc == 0 && nBits)
	{
		BOOL lbNeedLoad = FALSE;
		static DWORD nLoadLibraryW32 = 0;
		static DWORD nLoadLibraryW64 = 0;
		DWORD_PTR ptr = 0;

		lbNeedLoad = (nBits == 64) ? (nLoadLibraryW32 == 0) : (nLoadLibraryW64 == 0);
		if (lbNeedLoad)
		{
			iRc = FindLoadLibrary(mi.szExePath);
			if (iRc > 0)
			{
				if (nBits == 64)
					nLoadLibraryW64 = iRc;
				else
					nLoadLibraryW32 = iRc;
				lbNeedLoad = FALSE; // OK
			}
			//LPVOID pExportDirAddr;
			//DWORD exportsStartRVA;
			//DWORD exportDirSize;
			//if (nBits == 64)
			//{
			//	exportsStartRVA = hdr.OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
			//	exportDirSize = hdr.OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
			//	pExportDirAddr = GetPtrFromRVA(exportsStartRVA, (PIMAGE_NT_HEADERS64)&hdr, mi.modBaseAddr);
			//}
			//else
			//{
			//	exportsStartRVA = hdr.OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
			//	exportDirSize = hdr.OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
			//	pExportDirAddr = GetPtrFromRVA(exportsStartRVA, (PIMAGE_NT_HEADERS32)&hdr, mi.modBaseAddr);
			//}
			//if (!pExportDirAddr)
			//	iRc = -30;
			//else if (!ReadProcessMemory(ahProcess, pExportDirAddr, 
		}

		if (lbNeedLoad)
		{
			// �� �������
			if (iRc == 0)
				iRc = -40;
		}
		else
		{
			ptr = ((DWORD_PTR)mi.modBaseAddr) + ((nBits == 64) ? nLoadLibraryW64 : nLoadLibraryW32);
			if (ptr != (DWORD)ptr)
			{
				// BaseAddress ���� ��� 64-������� Kernel32 ���, � �������� � 32-������ ���������,
				// �� ��� �� ����� ���������, � ���� �� "����" - �� ���������� ������.
				iRc = -41;
			}
			else
				*pLoadLibrary = (DWORD)ptr;
		}
	}

	return iRc;
}
