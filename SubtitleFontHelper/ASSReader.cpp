
#include "ASSReader.h"
#include "FontDatabase.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <codecvt>
#include <algorithm>
#include <string>
#include <cassert>
#include <vector>

#include "Common.h"

enum class FileEncoding {
	None,
	UTF8,
	UTF16BE,
	UTF16LE,
	UTF32BE,
	UTF32LE,
	Local
};

/**
	@brief check ass file encoding
	@param in - file stream
	@return length of BOM and encoding
*/
std::pair<size_t, FileEncoding> DetermineEncoding(const char* buffer, size_t len);


static std::u32string GetUnicodeFileContents(const std::wstring& filename);
static std::u32string GetUnicodeFileContents(const std::string& filename);

std::pair<size_t, FileEncoding> DetermineEncoding(const char* buffer, size_t len)
{
	size_t bom_len = -1;
	FileEncoding enc = FileEncoding::None;
	if (len < 4)return std::make_pair(-1, FileEncoding::None);
	// check if there's a BOM
	if (std::memcmp(buffer, "\xEF\xBB\xBF", 3) == 0) {
		enc = FileEncoding::UTF8;
		bom_len = 3;
	}
	else if (std::memcmp(buffer, "\xFE\xFF", 2) == 0) {
		enc = FileEncoding::UTF16BE;
		bom_len = 2;
	}
	else if (std::memcmp(buffer, "\xFF\xFE", 2) == 0) {
		enc = FileEncoding::UTF16LE;
		bom_len = 2;
	}
	else if (std::memcmp(buffer, "\x00\x00\xFE\xFF", 4) == 0) {
		enc = FileEncoding::UTF32BE;
		bom_len = 4;
	}
	else if (std::memcmp(buffer, "\xFF\xFE\x00\x00", 4) == 0) {
		enc = FileEncoding::UTF32LE;
		bom_len = 4;
	}
	else if (std::memcmp(buffer, "[Scr", 4) == 0) {
		enc = FileEncoding::UTF8;
		bom_len = 0;
	}
	else if (std::memcmp(buffer, "\x00\x00\x00\x5B", 4) == 0) {
		enc = FileEncoding::UTF32BE;
		bom_len = 0;
	}
	else if (std::memcmp(buffer, "\x5B\x00\x00\x00", 4) == 0) {
		enc = FileEncoding::UTF32LE;
		bom_len = 0;
	}
	else if (std::memcmp(buffer, "\x00\x5B\x00\x53", 4) == 0) {
		enc = FileEncoding::UTF16BE;
		bom_len = 0;
	}
	else if (std::memcmp(buffer, "\x5B\x00\x53\x00", 4) == 0) {
		enc = FileEncoding::UTF16LE;
		bom_len = 0;
	}
	else {
		enc = FileEncoding::Local;
		bom_len = 0;
	}
	return std::make_pair(bom_len, enc);
}

std::u32string GetUnicodeFileContents(const std::wstring& filename)
{
	std::u32string ret;
	FILE* fd = _wfopen(filename.c_str(), L"rb");
	if (fd == nullptr)return ret;
	std::vector<char> buffer;
	fseek(fd, 0, SEEK_END);
	size_t length = ftell(fd);
	rewind(fd);
	buffer.assign(length, '\0');
	size_t read_len = fread(buffer.data(), sizeof(char), length, fd);
	if (read_len < length) {
		fclose(fd);
		return ret;
	}
	fclose(fd);
	auto enc = DetermineEncoding(buffer.data(), buffer.size());
	switch (enc.second) {
	case FileEncoding::Local:
	case FileEncoding::None:
		break;
	case FileEncoding::UTF8:
	{
		std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cvt;
		ret = cvt.from_bytes(buffer.data() + enc.first, buffer.data() + buffer.size());
		break;
	}
	case FileEncoding::UTF16LE:
	{
		std::wstring_convert<std::codecvt_utf16<char32_t, 0x10FFFF, std::little_endian>, char32_t> cvt;
		ret = cvt.from_bytes(buffer.data() + enc.first, buffer.data() + buffer.size());
		break;
	}
	case FileEncoding::UTF16BE:
	{
		std::wstring_convert<std::codecvt_utf16<char32_t>, char32_t> cvt;
		ret = cvt.from_bytes(buffer.data() + enc.first, buffer.data() + buffer.size());
		break;
	}
	case FileEncoding::UTF32LE:
	{
		size_t code_length = buffer.size() - enc.first;
		for (size_t i = 0; i < code_length; i += sizeof(char32_t)) {
			ret.push_back(*reinterpret_cast<char32_t*>(&buffer.data()[i + enc.first]));
		}
		break;
	}
	case FileEncoding::UTF32BE:
	{
		size_t code_length = buffer.size() - enc.first;
		for (size_t i = 0; i < code_length; i += sizeof(char32_t)) {
			ret.push_back(_byteswap_ulong(*reinterpret_cast<char32_t*>(&buffer.data()[i + enc.first])));
		}
		break;
	}
	}
	return ret;
}

std::u32string GetUnicodeFileContents(const std::string& filename)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
	return GetUnicodeFileContents(cvt.from_bytes(filename));
}

static inline std::u32string GetUntilChar(const std::u32string& str, size_t& pos, char32_t ch) {
	std::u32string ret;
	while (pos < str.size()) {
		if (str[pos] != ch) {
			ret.push_back(str[pos]);
			++pos;
		}
		else {
			++pos;
			break;
		}
	}
	return ret;
}

bool ASSParser::OpenFile(const std::string& filename)
{
	return OpenFile(UTF8ToStdWString(filename));
}

bool ASSParser::OpenFile(const std::wstring& filename)
{
	fonts_list.clear();
	std::u32string content = GetUnicodeFileContents(filename);
	if (content.empty())return false;

	content.erase(std::remove(content.begin(), content.end(), U'\r'), content.end());
	size_t pos = 0;

	while (pos < content.size()) {
		auto line = GetUntilChar(content, pos, U'\n');
		if (line.find(U"Style:") == 0) {
			size_t first_comma = line.find(U',');
			if (first_comma != std::u32string::npos) {
				size_t second_comma = line.find(U',', first_comma + 1);
				if (second_comma != std::u32string::npos) {
					fonts_list.push_back(
						GetUndecoratedFontName(
							TrimString(
								UTF32ToWString(line.substr(first_comma + 1, second_comma - first_comma - 1)))));
					if (fonts_list.back().empty())fonts_list.pop_back();
				}
			}
		}
		else if (line.find(U"Dialogue:") == 0) {
			size_t brace_beg_pos = line.find(U'{');
			size_t brace_end_pos = line.find(U'}', brace_beg_pos);
			while (brace_beg_pos != std::u32string::npos && brace_end_pos != std::u32string::npos) {
				size_t fn_command_pos = line.find(U"\\fn", brace_beg_pos);
				while (fn_command_pos < brace_end_pos) {
					size_t end_pos = line.find_first_of(U"\\}", fn_command_pos + 1);
					if (end_pos > brace_end_pos)break;
					fonts_list.push_back(
						GetUndecoratedFontName(
							TrimString(
								UTF32ToWString(line.substr(fn_command_pos + 3, end_pos - fn_command_pos - 3)))));
					if (fonts_list.back().empty())fonts_list.pop_back();
					fn_command_pos = line.find(U"\\fn", fn_command_pos + 1);
				}
				brace_beg_pos = line.find(U'{', brace_end_pos);
				brace_end_pos = line.find(U'}', brace_beg_pos);
			}
		}
	}
	std::sort(fonts_list.begin(), fonts_list.end());
	fonts_list.erase(std::unique(fonts_list.begin(), fonts_list.end()), fonts_list.end());
	return true;
}

const std::vector<std::wstring>& ASSParser::GetReferencedFonts()
{
	return fonts_list;
}
