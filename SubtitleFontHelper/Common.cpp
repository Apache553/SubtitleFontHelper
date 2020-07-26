#include "Common.h"

#include <codecvt>
#include <cassert>
#include <vector>
#include <locale>

#include <Windows.h>
#include <Shlobj.h>
#include <Knownfolders.h>

const static char default_conf[] = u8R"_(<?xml version="1.0" encoding="UTF-8"?><ConfigFile></ConfigFile>)_" u8"\n";
constexpr size_t default_conf_len = sizeof(default_conf) - 1;

_DebugOutput dbgout;

std::wstring GetDefaultConfigFilename()
{
	REFKNOWNFOLDERID rfid = FOLDERID_LocalAppData;
	wchar_t* ret;
	if (SHGetKnownFolderPath(rfid, 0, NULL, &ret) != S_OK) {
		return std::wstring();
	}
	std::wstring ret_str(ret);
	CoTaskMemFree(ret);
	ret_str += L"\\SubtitleFontHelper.xml";
	HANDLE hf = CreateFileW(ret_str.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf != INVALID_HANDLE_VALUE) {
		WriteFile(hf, default_conf, default_conf_len, nullptr, NULL);
		CloseHandle(hf);
	}
	return ret_str;
}


std::string StdWStringToUTF8(const std::wstring& str)
{
	static std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
	return cvt.to_bytes(str);
}

std::wstring UTF8ToStdWString(const std::string& str)
{
	static std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
	return cvt.from_bytes(str);
}

std::wstring UTF16BEToStdWString(const char* ptr, size_t len) {
	static std::wstring_convert<std::codecvt_utf16<wchar_t>> cvt;

	if (len == -1) {
		return cvt.from_bytes(ptr);
	}
	else {
		// for some broken fonts, the length field might be odd
		if (len & 0x1)len -= 1;
		return cvt.from_bytes(ptr, ptr + len);
	}
}

std::wstring ASCIIToStdWString(const char* ptr, size_t len) {
	static std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>> cvt;
	if (len == -1) {
		return cvt.from_bytes(ptr);
	}
	else {
		return cvt.from_bytes(ptr, ptr + len);
	}
}

std::wstring UTF32ToWString(const std::u32string& str) {
	static std::wstring_convert<std::codecvt_utf16<char32_t, 0x10FFFF, std::little_endian>, char32_t> cvt;
	std::string buf = cvt.to_bytes(str);
	static_assert(sizeof(wchar_t) == 2, "wchar_t must be 2 bytes(utf16/ucs2)");
	assert(buf.size() % 2 == 0);
	return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()), buf.size() / 2);
}

std::wstring TrimString(const std::wstring& str)
{
	size_t offset = 0;
	std::locale loc("");
	std::wstring ret;
	for (size_t i = 0; i < str.size(); ++i) {
		if (std::isblank(str[i], loc)) {
			++offset;
		}
		else {
			break;
		}
	}
	ret = str.substr(offset);
	while (!str.empty()) {
		if (std::isblank(str.back(), loc)) {
			ret.pop_back();
		}
		else {
			break;
		}
	}
	return ret;
}

bool WriteAllToFile(const std::wstring& path, const std::string& content)
{
	FILE* fp = _wfopen(path.c_str(), L"wb");
	if (fp == nullptr)return false;
	size_t ret = fwrite(content.data(), sizeof(std::string::value_type), content.size(), fp);
	fclose(fp);
	if (ret != content.size()) {
		return false;
	}
	return true;
}

bool ReadAllFromFile(const std::wstring& path, std::string& bytes) {
	return GetFileMemoryBuffer(path, [&](void* mem,size_t len,const std::wstring& fn) {
		bytes.assign((char*)mem, len);
		});
}

bool GetFileMemoryBuffer(const std::wstring& filename, GetFileMemoryBufferCb cb) {
	HANDLE hfile = CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)return false;
	size_t file_size = -1;
	LARGE_INTEGER large_int;
	if (GetFileSizeEx(hfile, &large_int) != 0) {
		// success
		file_size = large_int.QuadPart;
	}
	HANDLE hmap = CreateFileMappingW(hfile, NULL, PAGE_READONLY, 0, 0, nullptr);
	if (hmap != NULL) {
		// do file mapping
		void* mapped_addr = MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, 0);
		if (mapped_addr == nullptr) {
			CloseHandle(hmap);
			hmap = NULL;
		}
		else {
			MEMORY_BASIC_INFORMATION info;
			VirtualQuery(mapped_addr, &info, sizeof(info));
			if (file_size == -1)file_size = info.RegionSize;
			cb(mapped_addr, file_size, filename);
			UnmapViewOfFile(mapped_addr);
		}
	}
	if (hmap == NULL) {
		std::vector<BYTE> buf;
		if (file_size != -1) {
			// success
			buf.reserve(file_size);
		}
		BYTE buffer[4096];
		DWORD read_bytes;
		SetFilePointer(hfile, 0, nullptr, FILE_BEGIN);
		while (ReadFile(hfile, buffer, 4096, &read_bytes, nullptr)) {
			if (read_bytes == 0)break;
			buf.insert(buf.end(), buffer, buffer + read_bytes);
		}
		cb(buf.data(), buf.size(), filename);
	}

	if (hmap)CloseHandle(hmap);
	if (hfile)CloseHandle(hfile);
	return true;
}

void WalkDirectory(std::wstring path, bool& flag, bool resursive, WalkCallback cb) {
	// path.size() must greater than 0

	const std::wstring mask = L"*";
	WIN32_FIND_DATAW data;
	if (path.back() != L'\\')path += L'\\';
	HANDLE hfind = FindFirstFileW((path + mask).c_str(), &data);
	if (hfind == INVALID_HANDLE_VALUE)return;
	do {
		if (std::wstring(L".") == data.cFileName)continue;
		if (std::wstring(L"..") == data.cFileName)continue;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && resursive) {
			WalkDirectory(path + data.cFileName, resursive, flag, cb);
		}
		else {
			cb(path + data.cFileName);
		}
	} while (FindNextFileW(hfind, &data) != 0 && flag);
	FindClose(hfind);
	return;
}

const char* TrueType_SFNTVer = "\x00\x01\x00\x00";
const char* OpenType_SFNTVer = "\x4F\x54\x54\x4F";
const char* TTC_Tag = "\x74\x74\x63\x66";

std::wstring DetectFontExtensionName(const char* mem, size_t len)
{
	if (len < 4)return std::wstring(L".broken");
	if (memcmp(mem, TrueType_SFNTVer, 4) == 0)return std::wstring(L".ttf");
	if (memcmp(mem, OpenType_SFNTVer, 4) == 0)return std::wstring(L".otf");
	if (memcmp(mem, TTC_Tag, 4) == 0)return std::wstring(L".ttc");
	return std::wstring(L".unknown");
}

_DebugOutput& _DebugOutput::operator <<(const std::wstring& str) {
#ifdef _DEBUG
	OutputDebugStringW(str.c_str());
#endif
	return *this;
}

_DebugOutput& _DebugOutput::operator <<(wchar_t ch) {
	wchar_t buffer[2] = { ch,0 };
#ifdef _DEBUG
	OutputDebugStringW(buffer);
#endif
	return *this;
}