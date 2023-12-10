// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include "AttachDetour.h"
#include "EventLog.h"

#include <clocale>

#include <wil/win32_helpers.h>

#include <delayimp.h>
#include <filesystem>

DWORD WINAPI DelayedAttach(LPVOID lpThreadParameter);

DWORD attachThreadId = 0;

HMODULE LoadDll(const char* name)
{
	HMODULE thisDll = NULL;
	if (GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LoadDll),
		&thisDll) == 0) {
		return NULL;
	}
	auto path = wil::GetModuleFileNameW<wil::unique_hlocal_string>(thisDll);
	auto dir = std::filesystem::path(path.get()).parent_path();
	auto newpath = dir / name;
	return LoadLibraryW(newpath.wstring().c_str());
}

FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	//if the failure was failure to load the designated dll
	if (dliNotify == dliFailLoadLib && pdli->dwLastError == ERROR_MOD_NOT_FOUND)
	{
		//return the successfully loaded back-up lib,
		//or 0, the LoadLibrary fails here
		HMODULE lib = LoadDll(pdli->szDll);
		return (FARPROC)lib;
	}
	return 0;
}

ExternC const PfnDliHook __pfnDliFailureHook2 = DelayLoadHook;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD ul_reason_for_call,
	LPVOID lpReserved
)
{
	try
	{
		switch (ul_reason_for_call)
		{
		case DLL_PROCESS_ATTACH:
			if (sfh::IsDetourNeeded())
			{
				wil::unique_handle hThread(CreateThread(
					nullptr,
					0,
					DelayedAttach,
					nullptr,
					0,
					nullptr));
				if (!hThread.is_valid())
					return FALSE;
			}
			break;
		case DLL_THREAD_ATTACH:
			if (attachThreadId == GetCurrentThreadId())
			{
				if (!sfh::AttachDetour())
					return FALSE;
				sfh::EventLog::GetInstance().LogDllAttach(GetCurrentProcessId());
			}
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
	catch (...)
	{
		return FALSE;
	}
}

DWORD WINAPI DummyThread(LPVOID lpThreadParameter)
{
	return 0;
}

namespace sfh
{
	namespace Detour
	{
		void LoadFunctionPointers();
	}
}

DWORD WINAPI DelayedAttach(LPVOID lpThreadParameter)
{
	// check required dll
	sfh::Detour::LoadFunctionPointers();
	// create thread to acquire loader lock
	wil::unique_handle hThread(CreateThread(
		nullptr,
		0,
		DummyThread,
		nullptr,
		CREATE_SUSPENDED,
		&attachThreadId));
	if (!hThread.is_valid())
		return 1;
	ResumeThread(hThread.get());
	return 0;
}


wil::unique_hlocal_string GetDllSelfPath()
{
	HMODULE hModule;
	THROW_LAST_ERROR_IF(
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<wchar_t*>(DllMain), &hModule) == FALSE);
	auto ret = wil::GetModuleFileNameW<wil::unique_hlocal_string>(hModule);
	return ret;
}

namespace sfh
{
	std::wstring AnsiStringToWideString(const char* str);
}

extern "C" {
#ifdef _WIN64
#pragma comment(linker, "/export:InjectProcess=InjectProcess")
#else
#pragma comment(linker, "/export:InjectProcess=_InjectProcess@16")
#endif
	void CALLBACK InjectProcess(HWND hWnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow)
	{
		_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
		setlocale(LC_ALL, "");
		DWORD processId = 0;
		try
		{
			processId = std::stoul(lpszCmdLine);

			wil::unique_handle hProcess(OpenProcess(
				PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
				FALSE,
				processId));
			THROW_LAST_ERROR_IF(!hProcess.is_valid());

			auto selfPath = GetDllSelfPath();
			size_t bufferSize = (wcslen(selfPath.get()) + 1) * sizeof(wchar_t);

			LPVOID remoteBuf = VirtualAllocEx(
				hProcess.get(),
				nullptr,
				bufferSize,
				MEM_COMMIT,
				PAGE_READWRITE);
			THROW_LAST_ERROR_IF(!remoteBuf);

			WriteProcessMemory(
				hProcess.get(),
				remoteBuf, selfPath.get(),
				bufferSize,
				nullptr);

			HMODULE hModuleKernel32 = nullptr;
			THROW_LAST_ERROR_IF(
				GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					L"kernel32.dll",
					&hModuleKernel32) == FALSE);
			auto threadProc = reinterpret_cast<LPTHREAD_START_ROUTINE>(
				GetProcAddress(hModuleKernel32, "LoadLibraryW"));

			wil::unique_handle hThread(CreateRemoteThread(
				hProcess.get(),
				nullptr,
				0,
				threadProc,
				remoteBuf,
				0,
				nullptr));
			THROW_LAST_ERROR_IF(!hThread.is_valid());

			WaitForSingleObject(hThread.get(), INFINITE);
			// VirtualFreeEx(hProcess.get(), remoteBuf, 0, MEM_RELEASE);
			sfh::EventLog::GetInstance().LogDllInjectProcessSuccess(processId);
		}
		catch (std::exception& e)
		{
			sfh::EventLog::GetInstance().LogDllInjectProcessFailure(
				processId, sfh::AnsiStringToWideString(e.what()).c_str());
		}
	}
}
