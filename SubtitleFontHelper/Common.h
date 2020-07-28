
#pragma once

#include <string>
#include <functional>
#include <type_traits>
#include <cwctype>
#include <locale>
#include <algorithm>

struct CaseInsensitiveHasher {
	size_t operator()(const std::wstring& str)const;
};

struct CaseInsensitiveEqual {
	bool operator()(const std::wstring& str1, const std::wstring& str2)const;
};

std::wstring GetDefaultConfigFilename();

std::wstring GetSystem32Directory();

std::wstring GetFullPath(const std::wstring& path);

std::string StdWStringToUTF8(const std::wstring& str);
std::wstring UTF8ToStdWString(const std::string& str);

std::wstring UTF16BEToStdWString(const char* ptr, size_t len = (size_t)-1);

std::wstring ASCIIToStdWString(const char* ptr, size_t len = (size_t)-1);

std::wstring UTF32ToWString(const std::u32string& str);

std::wstring TrimString(const std::wstring& str);

bool WriteAllToFile(const std::wstring& path, const std::string& content);
bool ReadAllFromFile(const std::wstring& path, std::string& bytes);

typedef std::function<void(void* mem, size_t size, const std::wstring& path)> GetFileMemoryBufferCb;
bool GetFileMemoryBuffer(const std::wstring& filename, GetFileMemoryBufferCb cb);

typedef std::function<void(const std::wstring&)> WalkCallback;
void WalkDirectory(std::wstring path, bool& flag, bool resursive, WalkCallback cb);

std::wstring DetectFontExtensionName(const char* mem, size_t len);

struct _DebugOutput {
	template<typename T>
	auto operator <<(const T& val) -> decltype(std::to_wstring(val), void(), (*this)){
		*this << std::to_wstring(val);
		return *this;
	}

	_DebugOutput& operator <<(const std::wstring& str);

	_DebugOutput& operator <<(wchar_t ch);

	_DebugOutput() {}
	_DebugOutput(const _DebugOutput&) = delete;
	_DebugOutput(_DebugOutput&&) = delete;
	_DebugOutput& operator=(const _DebugOutput&) = delete;
	_DebugOutput& operator=(_DebugOutput&&) = delete;
};

extern _DebugOutput dbgout;