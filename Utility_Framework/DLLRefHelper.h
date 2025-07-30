#pragma once
#include <Windows.h>                   //GetVersionEx, ReadProcessMemory
#include <winternl.h>                   //PROCESS_BASIC_INFORMATION
#include <VersionHelpers.h>

inline bool ReadMem(void* addr, void* buf, int size)
{
	BOOL b = ReadProcessMemory(GetCurrentProcess(), addr, buf, size, nullptr);
	return b != FALSE;
}

#ifdef _WIN64
#define BITNESS 1
#else
#define BITNESS 0
#endif

typedef NTSTATUS(NTAPI* pfuncNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

inline int GetModuleLoadCount(HMODULE hDll)
{
	// Not supported by earlier versions of windows.
	if (!::IsWindows8OrGreater())
		return 0;

	PROCESS_BASIC_INFORMATION pbi = { 0 };

	HMODULE hNtDll = LoadLibraryA("ntdll.dll");
	if (!hNtDll)
		return 0;

	pfuncNtQueryInformationProcess pNtQueryInformationProcess = (pfuncNtQueryInformationProcess)GetProcAddress(hNtDll, "NtQueryInformationProcess");
	bool b = pNtQueryInformationProcess != nullptr;
	if (b) b = NT_SUCCESS(pNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), nullptr));
	FreeLibrary(hNtDll);

	if (!b)
		return 0;

	char* LdrDataOffset = (char*)(pbi.PebBaseAddress) + offsetof(PEB, Ldr);
	char* addr;
	PEB_LDR_DATA LdrData;

	if (!ReadMem(LdrDataOffset, &addr, sizeof(void*)) || !ReadMem(addr, &LdrData, sizeof(LdrData)))
		return 0;

	LIST_ENTRY* head = LdrData.InMemoryOrderModuleList.Flink;
	LIST_ENTRY* next = head;

	do {
		LDR_DATA_TABLE_ENTRY LdrEntry;
		LDR_DATA_TABLE_ENTRY* pLdrEntry = CONTAINING_RECORD(head, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

		if (!ReadMem(pLdrEntry, &LdrEntry, sizeof(LdrEntry)))
			return 0;

		if (LdrEntry.DllBase == (void*)hDll)
		{
			//  
			//  http://www.geoffchappell.com/studies/windows/win32/ntdll/structs/ldr_data_table_entry.htm
			//
			int offDdagNode = (0x14 - BITNESS) * sizeof(void*);   // See offset on LDR_DDAG_NODE *DdagNode;

			ULONG count = 0;
			char* addrDdagNode = ((char*)pLdrEntry) + offDdagNode;

			//
			//  http://www.geoffchappell.com/studies/windows/win32/ntdll/structs/ldr_ddag_node.htm
			//  See offset on ULONG LoadCount;
			//
			if (!ReadMem(addrDdagNode, &addr, sizeof(void*)) || !ReadMem(addr + 3 * sizeof(void*), &count, sizeof(count)))
				return 0;

			return (int)count;
		} //if

		head = LdrEntry.InMemoryOrderLinks.Flink;
	} while (head != next);

	return 0;
} //GetModuleLoadCount