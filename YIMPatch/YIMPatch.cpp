//	Copyright (c) 2018 Chet Simpson & The Giblet Initiative
//	
//	This file is subject to the terms and conditions defined in
//	file 'LICENSE.MD', which is part of this source code package.
//
#include "stdafx.h"
#include <CodeHook/cpuIA32.h>
#include <iostream>
#include <string>


void Hook_WinINet();
void Hook_CreateFile();
void Hook_Winsock();
void ApplyCopyPatcha();


void InitializeConsole()
{
	AllocConsole();
	SetConsoleTitle(L"Messenger Debug");
	freopen("conin$", "r", stdin); 
	freopen("conout$", "w", stdout); 
	freopen("conout$", "w", stderr);
}


void InstallHooks()
{
	if(!GetModuleHandle(L"Yahoo~1.exe") && !GetModuleHandle(L"YahooMessenger.exe") && !GetModuleHandle(L"ypager.exe"))
	{
		MessageBoxA(nullptr, "Unable to patch non-Yahoo! Messenger application", "Patch Error", MB_OK);
		return;
	}

	InitializeConsole();
	ApplyCopyPatcha();
	Hook_Winsock();
	Hook_CreateFile();
	Hook_WinINet();
}




BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
	((void)hModule);
	((void)reserved);

	if(reason == DLL_PROCESS_ATTACH)
	{
		InstallHooks();
	}

	return TRUE;
}

