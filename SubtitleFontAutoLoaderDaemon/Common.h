#pragma once

#include "pch.h"

namespace sfh
{
	std::string WideToUtf8String(const std::wstring& wStr);
	std::wstring Utf8ToWideString(const std::string& str);

	void ErrorMessageBox(const std::wstring& text, const std::wstring& caption, long hResult);

	std::string GetFileContent(const std::wstring& path);
	void SetFileContent(const std::wstring& path, const std::string_view& data);

	std::wstring GetCurrentProcessUserSid();

	[[noreturn]] inline void MarkUnreachable()
	{
		__assume(false);
	}

	extern bool g_debugOutputEnabled;
}
