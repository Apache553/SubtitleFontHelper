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
#include <detours.h>

#include <TlHelp32.h>

namespace sfh
{
	namespace Detour
	{
		std::vector<std::pair<void**, void*>> g_detouredFunctions;

		namespace Original
		{
			decltype(::CreateFontA)* CreateFontA = ::CreateFontA;
			decltype(::CreateFontW)* CreateFontW = ::CreateFontW;

			decltype(::CreateFontIndirectA)* CreateFontIndirectA = ::CreateFontIndirectA;
			decltype(::CreateFontIndirectW)* CreateFontIndirectW = ::CreateFontIndirectW;

			decltype(::CreateFontIndirectExA)* CreateFontIndirectExA = ::CreateFontIndirectExA;
			decltype(::CreateFontIndirectExW)* CreateFontIndirectExW = ::CreateFontIndirectExW;

			decltype(::EnumFontFamiliesA)* EnumFontFamiliesA = ::EnumFontFamiliesA;
			decltype(::EnumFontFamiliesW)* EnumFontFamiliesW = ::EnumFontFamiliesW;

			decltype(::EnumFontFamiliesExA)* EnumFontFamiliesExA = ::EnumFontFamiliesExA;
			decltype(::EnumFontFamiliesExW)* EnumFontFamiliesExW = ::EnumFontFamiliesExW;
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
			}
			while (Thread32Next(snapshot.get(), &threadEntry));
		}
		else
		{
			throw std::runtime_error("unable to enumerate threads");
		}
		return threadHandles;
	}

	template <typename Original, typename Detour>
	void DetourAttach(Original& ppPointer, Detour& pDetour)
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
	DetourAttach(Detour::Original::CreateFontA, Detour::CreateFontA);
	DetourAttach(Detour::Original::CreateFontW, Detour::CreateFontW);
	DetourAttach(Detour::Original::CreateFontIndirectA, Detour::CreateFontIndirectA);
	DetourAttach(Detour::Original::CreateFontIndirectW, Detour::CreateFontIndirectW);
	DetourAttach(Detour::Original::CreateFontIndirectExA, Detour::CreateFontIndirectExA);
	DetourAttach(Detour::Original::CreateFontIndirectExW, Detour::CreateFontIndirectExW);
	DetourAttach(Detour::Original::EnumFontFamiliesA, Detour::EnumFontFamiliesA);
	DetourAttach(Detour::Original::EnumFontFamiliesW, Detour::EnumFontFamiliesW);
	DetourAttach(Detour::Original::EnumFontFamiliesExA, Detour::EnumFontFamiliesExA);
	DetourAttach(Detour::Original::EnumFontFamiliesExW, Detour::EnumFontFamiliesExW);
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
