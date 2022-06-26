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
#include "Detour.h"

#include "FontQuery.pb.h"

namespace sfh
{
	std::wstring GetCurrentProcessUserSid()
	{
		auto hToken = GetCurrentProcessToken();
		PTOKEN_USER user;
		std::unique_ptr<char[]> buffer;
		DWORD returnLength;
		wil::unique_hlocal_string ret;
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

		void CheckNewVerison()
		{
			uint32_t newVerison = InterlockedCompareExchange(m_versionMem.get(), 0, 0);
			if (newVerison != m_lastKnownVersion)
			{
				m_lastKnownVersion = newVerison;
				m_cache.clear();
			}
		}

		bool IsQueryNeeded(const wchar_t* str)
		{
			if (!m_good)return true;
			std::lock_guard lg(m_lock);
			CheckNewVerison();
			if (m_cache.find(str) != m_cache.end())
				return false;
			return true;
		}

		void AddToCache(const wchar_t* str)
		{
			if (!m_good)return;
			std::lock_guard lg(m_lock);
			CheckNewVerison();
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

	std::string WideToUtf8String(const std::wstring& wStr)
	{
		std::string ret;
		const int length = WideCharToMultiByte(
			CP_UTF8,
			WC_ERR_INVALID_CHARS,
			wStr.c_str(),
			static_cast<int>(wStr.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (length <= 0)
			throw std::runtime_error("WideCharToMultiByte failed");
		ret.resize(length);
		WideCharToMultiByte(
			CP_UTF8,
			WC_ERR_INVALID_CHARS,
			wStr.c_str(),
			static_cast<int>(wStr.size()),
			ret.data(),
			length,
			nullptr,
			nullptr);
		return ret;
	}

	std::wstring Utf8ToWideString(const std::string& str)
	{
		std::wstring ret;
		const int length = MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			str.c_str(),
			static_cast<int>(str.size()),
			nullptr,
			0);
		if (length <= 0)
			throw std::runtime_error("MultiByteToWideChar failed");
		ret.resize(length);
		MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			str.c_str(),
			static_cast<int>(str.size()),
			ret.data(),
			length);
		return ret;
	}

	FontQueryResponse QueryFont(const wchar_t* str)
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

		FontQueryRequest request;
		request.set_version(1);
		request.set_querystring(WideToUtf8String(str));
		std::ostringstream oss;
		request.SerializeToOstream(&oss);
		std::string requestBuffer = std::move(oss).str();

		auto requestLength = static_cast<uint32_t>(requestBuffer.size());
		WritePipe(pipe.get(), &requestLength, sizeof(uint32_t));
		WritePipe(pipe.get(), requestBuffer.data(), static_cast<DWORD>(requestLength));

		uint32_t responseLength;
		ReadPipe(pipe.get(), &responseLength, sizeof(uint32_t));
		std::vector<char> responseBuffer(responseLength);
		ReadPipe(pipe.get(), responseBuffer.data(), responseLength);

		FontQueryResponse response;
		if (!response.ParseFromArray(responseBuffer.data(), responseLength))
			throw std::runtime_error("bad response");

		return response;
	}

	void TryLoad(const wchar_t* query, const FontQueryResponse& response)
	{
		struct EnumInfo
		{
			const FontQueryResponse* response;
			std::vector<char> maskedFace;
		};

		wil::unique_hdc_window hDC = wil::GetWindowDC(HWND_DESKTOP);
		LOGFONTW lf{};
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, query);

		EnumInfo enumInfo;
		enumInfo.response = &response;
		enumInfo.maskedFace.assign(response.fonts_size(), 0);

		Detour::Original::EnumFontFamiliesExW(
			hDC.get(), &lf, [](const LOGFONT* lpelfe, const TEXTMETRIC* lpntme, DWORD dwFontType, LPARAM lParam)-> int
			{
				EnumInfo& info = *reinterpret_cast<EnumInfo*>(lParam);
				auto faceName = WideToUtf8String(lpelfe->lfFaceName);
				for (int i = 0; i < info.response->fonts_size(); ++i)
				{
					if (info.maskedFace[i])continue;
					auto& face = info.response->fonts()[i];
					if ((std::ranges::find(face.familyname(), faceName) != face.familyname().end()
							|| face.ispsoutline()
							&& std::ranges::find(face.postscriptname(), faceName) != face.postscriptname().end()
							|| std::ranges::find(face.gdifullname(), faceName) != face.
							gdifullname().end())
						&& (!!face.oblique() == !!lpelfe->lfItalic && face.weight() == lpelfe->lfWeight)
					)
					{
						info.maskedFace[i] = 1;
					}
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&enumInfo), 0);
		for (int i = 0; i < response.fonts_size(); ++i)
		{
			if (enumInfo.maskedFace[i])continue;
			auto path = Utf8ToWideString(response.fonts()[i].path());
			AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
			EventLog::GetInstance().LogDllLoadFont(GetCurrentProcessId(), GetCurrentThreadId(), path.c_str());
		}
	}

	void QueryAndLoad(const wchar_t* query)
	{
		try
		{
			if (query == nullptr)
				return;
			// strip GDI added prefix '@'
			if (*query == L'@')
				++query;
			// skip empty string
			if (*query == L'\0')
				return;
			if (!QueryCache::GetInstance().IsQueryNeeded(query))
				return;
			auto response = QueryFont(query);

			std::vector<std::wstring> paths;
			for (int i = 0; i < response.fonts_size(); ++i)
			{
				auto& font = response.fonts()[i];
				auto path = Utf8ToWideString(font.path());
				paths.emplace_back(std::move(path));
			}
			QueryCache::GetInstance().AddToCache(query);
			std::vector<const wchar_t*> logData;
			for (auto& s : paths)
			{
				logData.push_back(s.c_str());
			}
			if (logData.empty())
			{
				EventLog::GetInstance().LogDllQueryNoResult(GetCurrentProcessId(), GetCurrentThreadId(), query);
			}
			else
			{
				EventLog::GetInstance().LogDllQuerySuccess(GetCurrentProcessId(), GetCurrentThreadId(), query, logData);
			}

			TryLoad(query, response);
		}
		catch (std::exception& e)
		{
			EventLog::GetInstance().LogDllQueryFailure(GetCurrentProcessId(), GetCurrentThreadId(), query,
			                                           AnsiStringToWideString(e.what()).c_str());
			// ignore exceptions
		}
	}

	void QueryAndLoad(const char* query)
	{
		if (query == nullptr)
			return;
		if (*query == '\0')
			return;
		try
		{
			auto wstr = AnsiStringToWideString(query);
			QueryAndLoad(wstr.c_str());
		}
		catch (...)
		{
		}
	}
}
