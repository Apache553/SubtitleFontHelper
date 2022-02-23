#include "pch.h"

#include "RpcClient.h"

#include <mutex>
#include <cstring>
#include <string>
#include <cwchar>
#include <sddl.h>
#include <unordered_set>

#include <wil/resource.h>

#undef max

#include "EventLog.h"

namespace sfh
{
	std::wstring GetCurrentProcessUserSid()
	{
		auto hToken = GetCurrentProcessToken();
		PTOKEN_USER user;
		std::unique_ptr<char[]> buffer;
		DWORD returnLength;
		wil::unique_hlocal_string ret;;
		if (GetTokenInformation(
			hToken,
			TokenUser,
			nullptr,
			0,
			&returnLength) == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			buffer = std::make_unique<char[]>(returnLength);
			user = reinterpret_cast<PTOKEN_USER>(buffer.get());
		}
		else
		{
			MarkUnreachable();
		}
		THROW_LAST_ERROR_IF(GetTokenInformation(
			hToken,
			TokenUser,
			user,
			returnLength,
			&returnLength) == FALSE);
		THROW_LAST_ERROR_IF(ConvertSidToStringSidW(user->User.Sid, ret.put()) == FALSE);
		return ret.get();
	}

	class QueryCache
	{
	private:
		wil::unique_handle m_version;
		wil::unique_mapview_ptr<uint32_t> m_versionMem;
		uint32_t m_lastKnownVersion = std::numeric_limits<uint32_t>::max();
		bool m_good = false;

		std::unordered_set<std::wstring> m_cache;
		std::mutex m_lock;

		QueryCache()
		{
			try
			{
				std::wstring versionShmName = L"SubtitleFontAutoLoaderSHM-";
				versionShmName += GetCurrentProcessUserSid();
				m_version.reset(CreateFileMappingW(
					INVALID_HANDLE_VALUE,
					nullptr,
					PAGE_READWRITE,
					0, 4,
					versionShmName.c_str()));
				THROW_LAST_ERROR_IF(!m_version.is_valid());
				m_versionMem.reset(static_cast<uint32_t*>(MapViewOfFile(
					m_version.get(),
					FILE_MAP_WRITE,
					0, 0,
					sizeof(uint32_t))));
				THROW_LAST_ERROR_IF(m_versionMem.get() == nullptr);
			}
			catch (...)
			{
			}
			m_good = true;
		}

	public:
		static QueryCache& GetInstance()
		{
			static QueryCache instance;
			return instance;
		}

		bool IsQueryNeeded(const wchar_t* str)
		{
			if (!m_good)return true;
			std::lock_guard lg(m_lock);
			uint32_t newVerison = InterlockedCompareExchange(m_versionMem.get(), 0, 0);
			if (newVerison != m_lastKnownVersion)
			{
				m_lastKnownVersion = newVerison;
				m_cache.clear();
			}
			if (m_cache.find(str) != m_cache.end())
				return false;
			return true;
		}

		void AddToCache(const wchar_t* str)
		{
			if (!m_good)return;
			std::lock_guard lg(m_lock);
			uint32_t newVerison = InterlockedCompareExchange(m_versionMem.get(), 0, 0);
			if (newVerison != m_lastKnownVersion)
			{
				m_lastKnownVersion = newVerison;
				m_cache.clear();
			}
			m_cache.emplace(str);
		}
	};

	void WritePipe(HANDLE pipe, const void* src, DWORD size)
	{
		DWORD writeBytes;
		THROW_LAST_ERROR_IF(WriteFile(pipe, src, size, &writeBytes, nullptr) == FALSE);
		if (writeBytes != size)
			throw std::runtime_error("can't write much data");
	}

	void ReadPipe(HANDLE pipe, void* dst, DWORD size)
	{
		DWORD readBytes;
		THROW_LAST_ERROR_IF(ReadFile(pipe, dst, size, &readBytes, nullptr) == FALSE);
		if (readBytes != size)
			throw std::runtime_error("can't read enough data");
	}

	std::wstring AnsiStringToWideString(const char* str)
	{
		std::wstring ret;
		const int length = MultiByteToWideChar(
			CP_ACP,
			MB_ERR_INVALID_CHARS,
			str,
			-1,
			nullptr,
			0);
		if (length <= 0)
			throw std::runtime_error("MultiByteToWideChar failed");
		ret.resize(length);
		MultiByteToWideChar(
			CP_ACP,
			MB_ERR_INVALID_CHARS,
			str,
			-1,
			ret.data(),
			length);
		while (!ret.empty() && ret.back() == 0)
			ret.pop_back();
		return ret;
	}
}

void sfh::QueryAndLoad(const wchar_t* str)
{
	if (str == nullptr)
		return;
	// strip GDI added prefix '@'
	if (*str == L'@')
		++str;
	// skip empty string
	if (*str == L'\0')
		return;
	if (!QueryCache::GetInstance().IsQueryNeeded(str))
		return;
	try
	{
		std::wstring pipeName = LR"_(\\.\pipe\SubtitleFontAutoLoaderRpc-)_";
		pipeName += GetCurrentProcessUserSid();
		wil::unique_hfile pipe(CreateFileW(
			pipeName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr));
		if (!pipe.is_valid())
		{
			if (GetLastError() == ERROR_PIPE_BUSY)
			{
				// wait previous request finish
				THROW_LAST_ERROR_IF(WaitNamedPipeW(pipeName.c_str(), NMPWAIT_USE_DEFAULT_WAIT) == FALSE);
				pipe.reset(CreateFileW(
					pipeName.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					0,
					nullptr,
					OPEN_EXISTING,
					0,
					nullptr));
			}
			THROW_LAST_ERROR_IF(!pipe.is_valid());
		}

		auto length = static_cast<uint32_t>(wcslen(str));
		WritePipe(pipe.get(), &length, sizeof(uint32_t));
		WritePipe(pipe.get(), str, sizeof(wchar_t) * length);

		uint32_t responseCount;
		ReadPipe(pipe.get(), &responseCount, sizeof(uint32_t));
		std::vector<std::wstring> paths;
		for (uint32_t i = 0; i < responseCount; ++i)
		{
			uint32_t pathLength;
			ReadPipe(pipe.get(), &pathLength, sizeof(uint32_t));
			std::wstring path(pathLength, 0);
			ReadPipe(pipe.get(), path.data(), sizeof(wchar_t) * pathLength);
			AddFontResourceExW(path.data(), FR_PRIVATE, 0);
			paths.emplace_back(std::move(path));
		}
		QueryCache::GetInstance().AddToCache(str);
		std::vector<const wchar_t*> logData;
		for (auto& s : paths)
		{
			logData.push_back(s.c_str());
		}
		if (logData.empty())
		{
			EventLog::GetInstance().LogDllQueryNoResult(GetCurrentProcessId(), GetCurrentThreadId(), str);
		}
		else
		{
			EventLog::GetInstance().LogDllQuerySuccess(GetCurrentProcessId(), GetCurrentThreadId(), str, logData);
		}
	}
	catch (std::exception& e)
	{
		EventLog::GetInstance().LogDllQueryFailure(GetCurrentProcessId(), GetCurrentThreadId(), str,
		                                           AnsiStringToWideString(e.what()).c_str());
		// ignore exceptions
	}
}

void sfh::QueryAndLoad(const char* str)
{
	if (str == nullptr)
		return;
	if (*str == '\0')
		return;
	try
	{
		auto wstr = AnsiStringToWideString(str);
		QueryAndLoad(wstr.c_str());
	}
	catch (...)
	{
	}
}
