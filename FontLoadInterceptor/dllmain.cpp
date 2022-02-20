// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include "AttachDetour.h"
#include <clocale>

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		setlocale(LC_ALL, "");
		if (sfh::IsDetourNeeded())
		{
			if (!sfh::AttachDetour())
				return FALSE;
		}
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		if (sfh::IsDetourNeeded())
		{
			sfh::DetachDetour();
		}
		break;
	}
	return TRUE;
}
