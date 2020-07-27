
#include "FontDatabase.h"

#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <codecvt>
#include <algorithm>
#include <cstdint>
#include <locale>
#include <utility>
#include <functional>

#include <QXmlStreamWriter>
#include <QXmlStreamReader>

template <typename... Args>
auto std_min(Args&&... args) -> decltype(std::min(std::forward<Args>(args)...)) {
	return std::min(std::forward<Args>(args)...);
}

#include <Windows.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_IDS_H
#include FT_SFNT_NAMES_H

#include "Common.h"

static std::wstring GetFontSFNTName(const FT_SfntName& sfnt);
static std::wstring MultiByteToStdWString(UINT codepage, const char* ptr, size_t len);

static std::vector<FontItem> GetFontItemFromFile(const std::wstring& filename);
static std::vector<FontItem> GetFontItemFromMemory(void* mem, size_t size, const std::wstring& path = L"");

struct FreeTypeLibGuard_t {
	FT_Library lib;
	FreeTypeLibGuard_t() {
		FT_Error err = FT_Init_FreeType(&lib);
		if (err != 0) {
			MessageBoxW(NULL, L"Freetype2 Init Failed!", L"ERROR", MB_OK | MB_ICONERROR);
			exit(-1);
		}
	}
	~FreeTypeLibGuard_t() {
		FT_Done_FreeType(lib);
	}
} FreeTypeLibGuard;

template<size_t size>
class FixedByteBuffer {
private:
	std::function<size_t(char*, size_t)> underflow_func;
public:
	char buffer[size];
	size_t buffer_size;
	size_t buffer_pos;

	FixedByteBuffer(std::function<size_t(char*, size_t)> underflow_function) {
		underflow_func = underflow_function;
		buffer_size = underflow_func(buffer,size);
		buffer_pos = 0;
	}

	inline bool GetBytes(char* buf, size_t len, size_t& get_count) {
		get_count = 0;
		if (buffer_size == 0)return false;
		size_t will_copy = std_min(len, buffer_size - buffer_pos);
		std::memcpy(buf, buffer + buffer_pos, will_copy);
		buffer_pos += will_copy;
		if (buffer_pos == buffer_size) {
			buffer_size = underflow_func(buffer, size);
			buffer_pos = 0;
		}
		get_count = will_copy;
		return true;
	}

	inline bool GetOneByte(char& ch) {
		if (buffer_size == 0)return false;
		ch = buffer[buffer_pos++];
		if (buffer_pos == buffer_size) {
			buffer_size = underflow_func(buffer, size);
			buffer_pos = 0;
		}
		return true;
	}
};

class _impl_SystemFontManager {
private:
	HDC hdc;
	HFONT last_hfont;

	bool last_query_success;

	std::wstring last_query;
	std::vector<char> font_buffer;
public:
	_impl_SystemFontManager();
	~_impl_SystemFontManager();

	bool QuerySystemFont(const std::wstring& name, bool exact);
	bool ExportSystemFont(const std::wstring& name, const std::wstring& path, bool exact);
	std::pair<std::unique_ptr<char[]>, size_t> ExportSystemFontToMemory(const std::wstring& name, bool exact);
	void ClearState();
	bool QuerySystemFontNoExport(const std::wstring& name);
};

FontDatabase::FontDatabase()
{
}


FontDatabase::~FontDatabase()
{
}

bool FontDatabase::LoadDatabase(const std::wstring& path)
{
	bool success = false;
	dbgout << L"Loading index: " << path << L'\n';
	GetFileMemoryBuffer(path, [&](void* mem, size_t len, const std::wstring& fn) {
		QByteArray data = QByteArray::fromRawData((char*)mem, len);
		QXmlStreamReader reader(data);
		while (!reader.atEnd()) {
			QXmlStreamReader::TokenType token = reader.readNext();
			if (reader.hasError()) {
				dbgout << L"XML Parse Error" <<
					L'(' << reader.lineNumber() << L',' << reader.columnNumber() << L"): " << 
					reader.errorString().toStdWString() << L'\n';
				success = false;
				return;
			}
			switch (token) {
			case QXmlStreamReader::StartDocument:
				// ignore
				break;
			case QXmlStreamReader::StartElement:
				if (reader.name() == "Font") {
					auto attr = reader.attributes();
					FontItem item;
					item.name = attr.value("name").toString().toStdWString();
					item.path = attr.value("path").toString().toStdWString();
					if (!item.name.empty() && !item.path.empty()) {
						AddItem(std::move(item));
					}
				}
				break;
			case QXmlStreamReader::EndElement:
				break;
			case QXmlStreamReader::EndDocument:
				break;
			}
		}
		success = true;
		});
	if (success)dbgout << L"Success.\n";
	else dbgout << L"Failed.\n";
	return success;
}

void FontDatabase::AddItem(FontItem&& item)
{
	// find fontname first
	auto fntn_iter = fontname_map.find(item.name);
	// return if fontname exists
	if (fntn_iter != fontname_map.end())return;
	// else add it
	auto fnt_iter = fontpath_set.emplace(std::move(item.path));
	if (item.name.size() >= 32) {
		fontname_map.insert(std::make_pair(item.name.substr(0, 31), fnt_iter.first));
	}
	fontname_map.insert(std::make_pair(item.name, fnt_iter.first));
	return;
}

void FontDatabase::AddItem(const FontItem& item) {
	AddItem(FontItem(item));
}

FontItem FontDatabase::QueryFont(const std::wstring& name)
{
	auto fntn_iter = fontname_map.find(GetUndecoratedFontName(name));
	if (fntn_iter == fontname_map.end()) {
		throw std::out_of_range("nonexist font");
	}
	else {
		return FontItem{ fntn_iter->first,*fntn_iter->second };
	}
}

SystemFontManager::SystemFontManager()
{
	impl = new _impl_SystemFontManager();
}

SystemFontManager::~SystemFontManager()
{
	delete impl;
}

bool SystemFontManager::QuerySystemFont(const std::wstring& name, bool exact)
{
	return impl->QuerySystemFont(name, exact);
}

bool SystemFontManager::ExportSystemFont(const std::wstring& name, const std::wstring& path, bool exact)
{
	return impl->ExportSystemFont(name, path, exact);
}

std::pair<std::unique_ptr<char[]>, size_t> SystemFontManager::ExportSystemFontToMemory(const std::wstring& name, bool exact)
{
	return impl->ExportSystemFontToMemory(name, exact);
}

void SystemFontManager::ClearState()
{
	return impl->ClearState();
}

bool SystemFontManager::QuerySystemFontNoExport(const std::wstring& name)
{
	return impl->QuerySystemFontNoExport(name);
}

_impl_SystemFontManager::_impl_SystemFontManager() :last_hfont(NULL), last_query_success(false)
{
	hdc = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
	if (hdc == NULL)throw std::runtime_error("Unable to create HDC.");
}

_impl_SystemFontManager::~_impl_SystemFontManager()
{
	if (hdc)DeleteDC(hdc);
}

bool _impl_SystemFontManager::QuerySystemFont(const std::wstring& name, bool exact)
{
	std::wstring undecor_name = GetUndecoratedFontName(name);
	if (last_query == undecor_name) {
		// use previous result
		return last_query_success;
	}
	else {
		HGDIOBJ gdi_ret = NULL;

		DWORD table_id = 0x66637474;
		DWORD file_size = 0, file_pos = 0;

		last_query_success = false;
		last_query = undecor_name;
		if (last_hfont)DeleteObject(last_hfont);
		last_hfont = CreateFontW(0, 0, GM_COMPATIBLE, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, last_query.substr(0, 31).c_str());
		if (last_hfont == NULL)return false;

		gdi_ret = SelectObject(hdc, last_hfont);
		if (gdi_ret == HGDI_ERROR || gdi_ret == NULL)return false;

		file_size = GetFontData(hdc, table_id, 0, nullptr, 0);

		if (file_size == -1) {
			table_id = 0;
			file_size = GetFontData(hdc, table_id, 0, nullptr, 0);
		}
		if (file_size == -1)return false;

		font_buffer.resize(file_size);

		while (file_pos != file_size) {
			DWORD len = GetFontData(hdc, table_id, file_pos, font_buffer.data() + file_pos, font_buffer.size() - file_pos);
			if (len == -1)return false;
			file_pos += len;
		}

		auto font_name_list = GetFontItemFromMemory(font_buffer.data(), font_buffer.size());
		for (const auto& item : font_name_list) {
			if (item.name.substr(0, exact ? size_t(-1) : undecor_name.size()) == undecor_name) {
				last_query_success = true;
				return true;
			}
		}
		return false;
	}
}

bool _impl_SystemFontManager::ExportSystemFont(const std::wstring& name, const std::wstring& path, bool exact)
{
	std::wstring undecor_name = GetUndecoratedFontName(name);
	if (last_query == undecor_name) {
		if (last_query_success == true) {
			FILE* fp = _wfopen(path.c_str(), L"wb");
			if (fp == nullptr)return false;
			size_t written = fwrite(font_buffer.data(), sizeof(char), font_buffer.size(), fp);
			fclose(fp);
			if (written != font_buffer.size()) {
				return false;
			}
			else {
				return true;
			}
		}
		else {
			return false;
		}
	}
	else {
		if (QuerySystemFont(name, exact)) {
			return ExportSystemFont(name, path, exact);
		}
		else {
			return false;
		}
	}
}

std::pair<std::unique_ptr<char[]>, size_t> _impl_SystemFontManager::ExportSystemFontToMemory(const std::wstring& name, bool exact)
{
	std::unique_ptr<char[]> ret;
	std::wstring undecor_name = GetUndecoratedFontName(name);
	if (last_query == undecor_name) {
		if (last_query_success == true) {
			ret.reset(new char[font_buffer.size()]);
			std::memcpy(ret.get(), font_buffer.data(), font_buffer.size());
			return std::make_pair(std::move(ret), font_buffer.size());
		}
		else {
			return std::make_pair(std::move(ret), 0);
		}
	}
	else {
		if (QuerySystemFont(name,exact)) {
			return ExportSystemFontToMemory(name, exact);
		}
		else {
			return std::make_pair(std::move(ret), 0);
		}
	}
}

void _impl_SystemFontManager::ClearState()
{
	if (last_hfont) {
		DeleteObject(last_hfont);
		last_hfont = NULL;
	}
	last_query_success = false;
	font_buffer.swap(std::vector<char>());
	last_query.clear();
}

int CALLBACK CheckFontProc(CONST LOGFONTW*, CONST TEXTMETRICW*, DWORD, LPARAM ptr) {
	bool* ret = (bool*)ptr;
	*ret = true;
	return 0;
}

bool _impl_SystemFontManager::QuerySystemFontNoExport(const std::wstring& name)
{
	bool found = false;
	LOGFONTW lgf;
	ZeroMemory(&lgf, sizeof(LOGFONTW));
	lgf.lfCharSet = DEFAULT_CHARSET;
	name.copy(lgf.lfFaceName, 31);
	EnumFontFamiliesExW(hdc, &lgf, CheckFontProc, (LPARAM)&found, 0);
	return found;
}

static std::vector<FontItem> GetFontItemFromFile(const std::wstring& filename) {
	std::vector<FontItem> ret;
	GetFileMemoryBuffer(filename, [&](void* mem,size_t len,const std::wstring& fn) {
		ret = GetFontItemFromMemory(mem, len, fn);
		});
	return ret;
}

static std::vector<FontItem> GetFontItemFromMemory(void* mem, size_t size, const std::wstring& path)
{
	std::vector<FontItem> ret;
	FT_Library lib = FreeTypeLibGuard.lib;
	FT_Face face;

	LANGID lang_id = GetUserDefaultUILanguage();

	if (FT_New_Memory_Face(lib, (FT_Byte*)mem, size, -1, &face) != 0) {
		return std::vector<FontItem>();
	}
	int face_count = face->num_faces;
	FT_Done_Face(face);
	for (int i = 0; i < face_count; ++i) {
		if (FT_New_Memory_Face(lib, (FT_Byte*)mem, size, i, &face) != 0) {
			return std::vector<FontItem>();
		}
		bool has_vertical = face->face_flags & FT_FACE_FLAG_VERTICAL;
		std::wstring name = face->family_name ? ASCIIToStdWString(face->family_name) : std::wstring();

		FT_UInt sfnt_name_count = FT_Get_Sfnt_Name_Count(face);
		if (sfnt_name_count > 0) {

			int plat = -1, enc = -1, lang = -1;
			std::wstring loc_name;
			std::wstring loc_fullname;

			for (FT_UInt si = 0; si < sfnt_name_count; ++si) {
				FT_SfntName sfnt_name;
				FT_Get_Sfnt_Name(face, si, &sfnt_name);

				std::wstring widename = GetFontSFNTName(sfnt_name);
				if (plat != sfnt_name.platform_id || enc != sfnt_name.encoding_id || lang != sfnt_name.language_id) {
					if (!loc_name.empty()) {
						ret.emplace_back(FontItem{ loc_name,path });
						loc_name.clear();
					}
					if (!loc_fullname.empty()) {
						ret.emplace_back(FontItem{ loc_fullname,path });
						loc_fullname.clear();
					}
				}

				plat = sfnt_name.platform_id;
				enc = sfnt_name.encoding_id;
				lang = sfnt_name.language_id;

				if (sfnt_name.name_id == TT_NAME_ID_FONT_FAMILY) {
					loc_name.swap(widename);
				}
				else if (sfnt_name.name_id == TT_NAME_ID_FULL_NAME) {
					loc_fullname.swap(widename);
				}
			}
			if (!loc_name.empty()) {
				ret.emplace_back(FontItem{ loc_name,path });
				loc_name.clear();
			}
			if (!loc_fullname.empty()) {
				ret.emplace_back(FontItem{ loc_fullname,path });
				loc_fullname.clear();
			}
		}
		else {
			if (!name.empty()) {
				// some fonts have broken names
				// use this only when there's no sfnt name
				ret.emplace_back(FontItem{ name, path });
			}
		}
		FT_Done_Face(face);
	}
	std::sort(ret.begin(), ret.end(), [](const FontItem& a, const FontItem& b)->bool {
		return a.name < b.name;
		});
	std::vector<FontItem>::iterator new_end = std::unique(ret.begin(), ret.end(), [](const FontItem& a, const FontItem& b)->bool {
		return a.name == b.name;
		});
	ret.erase(new_end, ret.end());
	return ret;
}

static std::wstring MultiByteToStdWString(UINT codepage, const char* ptr, size_t len) {
	if (*ptr == 0) {
		// some strange fonts store bytes in uint16_t bigendian
		if (len % 2)len -= 1;
		// try utf16 first
		try {
			return UTF16BEToStdWString(ptr, len);
		}
		catch (std::range_error) {
			// conversion failure
			// might be strange storage
		}
		size_t new_len = len / 2;
		std::unique_ptr<char[]> tmp(new char[new_len]);
		for (size_t i = 0; i < new_len; ++i) {
			tmp.get()[i] = ptr[i * 2 + 1];
		}
		return MultiByteToStdWString(codepage, tmp.get(), new_len);
	}
	else if(*ptr==(char)0xFF){
		// no known charset(gb*,sjis,big5,wansung,johab) use 0xff in first byte
		try {
			return UTF16BEToStdWString(ptr, len);
		}
		catch (std::range_error) {
			// conversion failure
			// i have no idea about that...
		}
	}
	int alen = MultiByteToWideChar(codepage, 0, ptr, len, nullptr, 0);
	if (alen == 0)return std::wstring();
	std::unique_ptr<wchar_t[]> out(new wchar_t[alen]);
	alen = MultiByteToWideChar(codepage, 0, ptr, len, out.get(), alen);
	if (alen == 0)return std::wstring();
	auto dbg = out.get();
	return std::wstring(out.get(), alen);
}



static std::wstring GetFontSFNTName(const FT_SfntName& sfnt) {
	enum class Encoding { NONE, UTF8, UTF16, UTF32, SJIS, GBK, BIG5, WANS, JOHA, ROMAN, SYMBOL };
	Encoding encoding = Encoding::NONE;
	switch (sfnt.platform_id) {
	case TT_PLATFORM_APPLE_UNICODE:
	{
		return UTF16BEToStdWString((char*)sfnt.string, sfnt.string_len);
	}
	case TT_PLATFORM_MACINTOSH:
	{
		switch (sfnt.encoding_id) {
		case TT_MAC_ID_ROMAN:
			return MultiByteToStdWString(CP_ACP, (char*)sfnt.string, sfnt.string_len);
		case TT_MAC_ID_JAPANESE:
			return MultiByteToStdWString(932, (char*)sfnt.string, sfnt.string_len);
		case TT_MAC_ID_TRADITIONAL_CHINESE:
			return MultiByteToStdWString(950, (char*)sfnt.string, sfnt.string_len);
		case TT_MAC_ID_SIMPLIFIED_CHINESE:
			return MultiByteToStdWString(54936, (char*)sfnt.string, sfnt.string_len);
		case TT_MAC_ID_KOREAN:
			return MultiByteToStdWString(20949, (char*)sfnt.string, sfnt.string_len);
		default:
			return std::wstring();
		}
		break;
	}
	case TT_PLATFORM_MICROSOFT:
	{
		switch (sfnt.encoding_id) {
		case TT_MS_ID_SJIS:
			return MultiByteToStdWString(932, (char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_BIG_5:
			return MultiByteToStdWString(950, (char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_PRC:
			return MultiByteToStdWString(54936, (char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_WANSUNG:
			return MultiByteToStdWString(20949, (char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_JOHAB:
			return MultiByteToStdWString(1361, (char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_UNICODE_CS:
			// Big endian
			return UTF16BEToStdWString((char*)sfnt.string, sfnt.string_len);
		case TT_MS_ID_UCS_4:
			return std::wstring();
		default:
			return std::wstring();
		}
		break;
	}
	default:
		return std::wstring();
	}

	return std::wstring();
}

std::wstring GetUndecoratedFontName(const std::wstring& name)
{
	std::wstring undecor_name;
	if (name.size() > 0 && name[0] == L'@')undecor_name = name.substr(1);
	else undecor_name = name;
	return undecor_name;
}

bool WalkDirectoryAndBuildDatabase(const std::wstring& dir, const std::wstring& db_path, VisitCallback cb,
	bool recursive,const std::vector<std::wstring>& ext) {

	bool flag = true;
	std::vector<FontItem> fonts;
	if (dir.empty())return false;

	std::wstring full_dir = GetFullPath(dir);

	//FILE* fp = _wfopen(db_path.c_str(), L"wb");
	//if (fp == nullptr)return false;

	WalkDirectory(full_dir, flag, recursive, [&](const std::wstring& fn) {
		bool match_ext = false;
		for (const auto& ext_name : ext) {
			if (fn.rfind(ext_name) == fn.size() - ext_name.size()) {
				match_ext = true;
				break;
			}
		}
		if (match_ext == false)return false;
		cb(fn);
		auto font_info = GetFontItemFromFile(fn);
		fonts.insert(fonts.end(), font_info.begin(), font_info.end());
		return true;
		});
	std::sort(fonts.begin(), fonts.end(), [](const FontItem& a, const FontItem& b)->bool {
		return a.name < b.name;
		});
	fonts.erase(std::unique(fonts.begin(), fonts.end(), [](const FontItem& a, const FontItem& b)->bool {
		return a.name == b.name;
		}), fonts.end());

	uint32_t entry_count = fonts.size();
	
	QByteArray bytes;
	QXmlStreamWriter writer(&bytes);
	writer.setCodec("UTF-8");
	writer.writeStartDocument();
	writer.writeStartElement("FontList");
	writer.writeAttribute("directory", QString::fromStdWString(dir));
	writer.writeAttribute("count", QString::number(entry_count));
	for (const auto& item : fonts) {
		writer.writeStartElement("Font");
		writer.writeAttribute("name", QString::fromStdWString(item.name));
		writer.writeAttribute("path", QString::fromStdWString(item.path));
		writer.writeEndElement();
	}
	writer.writeEndElement();
	writer.writeEndDocument();

	return WriteAllToFile(db_path, bytes.toStdString());

	//if (!fwrite(&entry_count, sizeof(uint32_t), 1, fp)) {
	//	fclose(fp);
	//	return false;
	//}
	//std::string tmp;
	//for (const auto& item : fonts) {
	//	tmp = GetStdWStringStorageByteStream(item.name);
	//	if (!fwrite(tmp.data(), tmp.size(), 1, fp)) {
	//		fclose(fp);
	//		return false;
	//	}
	//	tmp = GetStdWStringStorageByteStream(item.path);
	//	if (!fwrite(tmp.data(), tmp.size(), 1, fp)) {
	//		fclose(fp);
	//		return false;
	//	}
	//}
	//fclose(fp);
}
