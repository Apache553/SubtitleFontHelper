#include "pch.h"

#include "AttachDetour.h"
#include "Detour.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <type_traits>
#include <stdexcept>

#include <wil/resource.h>
#include <wil/win32_helpers.h>
#include <detours\detours.h>

#include <TlHelp32.h>

namespace sfh
{
	namespace Detour
	{
		std::vector<std::pair<void**, void*>> g_detouredFunctions;

		namespace Original
		{
			HFONT(WINAPI* CreateFontA)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement,
				_In_ int cOrientation,
				_In_ int cWeight, _In_ DWORD bItalic,
				_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
				_In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
				_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily,
				_In_opt_ LPCSTR pszFaceName);
			HFONT(WINAPI* CreateFontW)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement,
				_In_ int cOrientation,
				_In_ int cWeight, _In_ DWORD bItalic,
				_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
				_In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
				_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily,
				_In_opt_ LPCWSTR pszFaceName);

			HFONT(WINAPI* CreateFontIndirectA)(_In_ CONST LOGFONTA* lplf);
			HFONT(WINAPI* CreateFontIndirectW)(_In_ CONST LOGFONTW* lplf);

			HFONT(WINAPI* CreateFontIndirectExA)(_In_ CONST ENUMLOGFONTEXDVA*);
			HFONT(WINAPI* CreateFontIndirectExW)(_In_ CONST ENUMLOGFONTEXDVW*);

			int (WINAPI* EnumFontFamiliesA)(_In_ HDC hdc, _In_opt_ LPCSTR lpLogfont, _In_ FONTENUMPROCA lpProc,
				_In_ LPARAM lParam);
			int (WINAPI* EnumFontFamiliesW)(_In_ HDC hdc, _In_opt_ LPCWSTR lpLogfont, _In_ FONTENUMPROCW lpProc,
				_In_ LPARAM lParam);

			int (WINAPI* EnumFontFamiliesExA)(_In_ HDC hdc, _In_ LPLOGFONTA lpLogfont, _In_ FONTENUMPROCA lpProc,
				_In_ LPARAM lParam, _In_ DWORD dwFlags);
			int (WINAPI* EnumFontFamiliesExW)(_In_ HDC hdc, _In_ LPLOGFONTW lpLogfont, _In_ FONTENUMPROCW lpProc,
				_In_ LPARAM lParam, _In_ DWORD dwFlags);
		}

		void LoadFunctionPointers()
		{
			HMODULE hGdi32 = LoadLibraryW(L"Gdi32.dll");
			if (hGdi32)
			{
#define LoadAddress(Mod, Func) sfh::Detour::Original::Func = reinterpret_cast<decltype(sfh::Detour::Original::Func)>(GetProcAddress(Mod, #Func))
				LoadAddress(hGdi32, CreateFontA);
				LoadAddress(hGdi32, CreateFontW);
				LoadAddress(hGdi32, CreateFontIndirectA);
				LoadAddress(hGdi32, CreateFontIndirectW);
				LoadAddress(hGdi32, CreateFontIndirectExA);
				LoadAddress(hGdi32, CreateFontIndirectExW);
				LoadAddress(hGdi32, EnumFontFamiliesA);
				LoadAddress(hGdi32, EnumFontFamiliesW);
				LoadAddress(hGdi32, EnumFontFamiliesExA);
				LoadAddress(hGdi32, EnumFontFamiliesExW);
#undef LoadAddress
			}
		}
	}
}

namespace
{
	std::vector<wil::unique_handle> RetrieveThreadHandles()
	{
		wil::unique_tool_help_snapshot snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
		std::vector<wil::unique_handle> threadHandles;
		if (!snapshot.is_valid())
			throw std::runtime_error("unable to take thread snapshot");
		DWORD processId = GetCurrentProcessId();
		DWORD currentThreadId = GetCurrentThreadId();
		THREADENTRY32 threadEntry;
		threadEntry.dwSize = sizeof(threadEntry);
		if (Thread32First(snapshot.get(), &threadEntry))
		{
			// enumerate all thread in current process
			// dying threads and new threads will be blocked until our DllMain returns
			// because of loader lock. we take snapshot in DllMain, so no thread will
			// be leaked. the purpose of collecting all threads of the process is to
			// avoid the case that a thread is just executing codes that detour will
			// modify, new/old combined code will definitely cause problem
			do
			{
				if (threadEntry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(threadEntry.
					th32OwnerProcessID)
					&& threadEntry.th32OwnerProcessID == processId
					&& threadEntry.th32ThreadID != currentThreadId)
				{
					wil::unique_handle hThread(
						OpenThread(
							THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
							// Detours will need those access
							FALSE,
							threadEntry.th32ThreadID));
					if (hThread.is_valid())
						threadHandles.emplace_back(std::move(hThread));
				}
				threadEntry.dwSize = sizeof(threadEntry);
			} while (Thread32Next(snapshot.get(), &threadEntry));
		}
		else
		{
			throw std::runtime_error("unable to enumerate threads");
		}
		return threadHandles;
	}

	template <typename Original, typename Detour>
	void DetourAttachHelper(Original& ppPointer, Detour& pDetour)
	{
		if (::DetourAttach(reinterpret_cast<void**>(&ppPointer),
			reinterpret_cast<void*>(pDetour)) == NO_ERROR)
		{
			sfh::Detour::g_detouredFunctions.emplace_back(reinterpret_cast<void**>(&ppPointer),
				reinterpret_cast<void*>(pDetour));
		}
	}
}

bool sfh::IsDetourNeeded()
{
	constexpr const wchar_t rundll32Name[] = L"rundll32.exe";
	constexpr const size_t rundll32NameLength = std::extent_v<decltype(rundll32Name)> - 1;
	if (getenv("__NO_DETOUR") != nullptr)
		return false;
	auto path = wil::GetModuleFileNameW<wil::unique_hlocal_string>();
	auto length = wcslen(path.get());
	if (length < rundll32NameLength)
		return false;
	if (_wcsicmp(path.get() + length - rundll32NameLength, rundll32Name) == 0)
		return false;
	return true;
}

bool sfh::AttachDetour()
{
	auto threads = RetrieveThreadHandles();
	DetourTransactionBegin();
	for (auto& thread : threads)
	{
		DetourUpdateThread(thread.get());
	}
	DetourUpdateThread(GetCurrentThread());
#define CheckAttach(Func) if(Detour::Original::Func != nullptr)DetourAttachHelper(Detour::Original::Func, Detour::Func)
	CheckAttach(CreateFontA);
	CheckAttach(CreateFontW);
	CheckAttach(CreateFontIndirectA);
	CheckAttach(CreateFontIndirectW);
	CheckAttach(CreateFontIndirectExA);
	CheckAttach(CreateFontIndirectExW);
	CheckAttach(EnumFontFamiliesA);
	CheckAttach(EnumFontFamiliesW);
	CheckAttach(EnumFontFamiliesExA);
	CheckAttach(EnumFontFamiliesExW);
#undef CheckAttach
	if (DetourTransactionCommit() != NO_ERROR)
	{
		return false;
	}
	return true;
}

bool sfh::DetachDetour()
{
	auto threads = RetrieveThreadHandles();
	DetourTransactionBegin();
	for (auto& thread : threads)
	{
		DetourUpdateThread(thread.get());
	}
	DetourUpdateThread(GetCurrentThread());
	for (auto& routine : Detour::g_detouredFunctions)
	{
		DetourDetach(routine.first, routine.second);
	}
	if (DetourTransactionCommit() != NO_ERROR)
	{
		return false;
	}
	return true;
}
