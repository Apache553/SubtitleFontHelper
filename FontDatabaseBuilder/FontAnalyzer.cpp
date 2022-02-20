#include "FontAnalyzer.h"
#include "Win32Helper.h"

#include <ft2build.h>


#include FT_FREETYPE_H
#include FT_TRUETYPE_IDS_H
#include FT_SFNT_NAMES_H

class FontAnalyzer::Implementation
{
private:
	std::vector<unsigned char> m_buffer;

	std::wstring ConvertMBCSName(const FT_SfntName& name)
	{
		if (name.string_len == 0)return {};
		UINT codePage;
		switch (name.encoding_id)
		{
		case TT_MS_ID_BIG_5:
			codePage = 950;
			break;
		case TT_MS_ID_GB2312:
			codePage = 936;
			break;
		case TT_MS_ID_WANSUNG:
			codePage = 949;
			break;
		default:
			throw std::logic_error("unexpected name encoding");
		}

		m_buffer.clear();
		for (FT_UInt i = 0; i < name.string_len - 1; i += 2)
		{
			if (name.string[i])
			{
				m_buffer.push_back(name.string[i]);
			}
			m_buffer.push_back(name.string[i + 1]);
		}

		int length = MultiByteToWideChar(
			codePage,
			MB_ERR_INVALID_CHARS,
			reinterpret_cast<char*>(m_buffer.data()),
			static_cast<int>(m_buffer.size()),
			nullptr,
			0);
		THROW_LAST_ERROR_IF(length == 0);

		std::wstring ret(length, 0);

		length = MultiByteToWideChar(
			codePage,
			MB_ERR_INVALID_CHARS,
			reinterpret_cast<char*>(m_buffer.data()),
			static_cast<int>(m_buffer.size()),
			ret.data(),
			static_cast<int>(ret.size()));
		THROW_LAST_ERROR_IF(length == 0);
		ret.resize(length);

		return ret;
	}

	static std::wstring ConvertUtf16BEName(const FT_SfntName& name)
	{
		size_t length = name.string_len / 2;
		std::wstring ret(length, 0);
		memcpy(ret.data(), name.string, length * sizeof(wchar_t));
			// all x86 systems use little endian
		std::transform(ret.begin(), ret.end(), ret.begin(), _byteswap_ushort);
		return ret;
	}

	std::wstring ConvertSfntName(const FT_SfntName& name)
	{
		switch (name.encoding_id)
		{
		case TT_MS_ID_BIG_5:
		case TT_MS_ID_GB2312:
		case TT_MS_ID_WANSUNG:
			return ConvertMBCSName(name);
		default:
			return ConvertUtf16BEName(name);
		}
	}

public:
	FT_Library m_lib;

	Implementation()
	{
		m_buffer.reserve(1024);
		FT_Init_FreeType(&m_lib);
	}

	~Implementation()
	{
		FT_Done_FreeType(m_lib);
	}

	std::vector<sfh::FontDatabase::FontFaceElement> AnalyzeFontFile(const wchar_t* path)
	{
		std::vector<sfh::FontDatabase::FontFaceElement> ret;
		FileMapping mapping(path);
		FT_Face face;

		if (FT_New_Memory_Face(
			m_lib,
			static_cast<FT_Byte*>(mapping.GetMappedPointer()),
			static_cast<FT_Long>(mapping.GetMappedLength()),
			-1,
			&face) != 0)
			throw std::runtime_error("failed to open font!");

		int faceCount = face->num_faces;
		ret.reserve(faceCount);
		FT_Done_Face(face);

		for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex)
		{
			sfh::FontDatabase::FontFaceElement faceElement;
			faceElement.m_path = path;
			faceElement.m_index = faceIndex;

			if (FT_New_Memory_Face(
				m_lib,
				static_cast<FT_Byte*>(mapping.GetMappedPointer()),
				static_cast<FT_Long>(mapping.GetMappedLength()),
				faceIndex,
				&face) != 0)
				throw std::runtime_error("failed to open fontface!");
			auto doneFace = wil::scope_exit([&]() { FT_Done_Face(face); });

			FT_UInt nameCount = FT_Get_Sfnt_Name_Count(face);
			for (FT_UInt nameIndex = 0; nameIndex < nameCount; ++nameIndex)
			{
				// we are only interested following names:
				//  - Win32FontFamilyName
				//  - FullName
				//  - PostScriptName
				FT_SfntName name;
				if (FT_Get_Sfnt_Name(face, nameIndex, &name) != 0)
					continue;

				// filter out non-microsoft names
				if (name.platform_id != TT_PLATFORM_MICROSOFT)
					continue;

				sfh::FontDatabase::FontFaceElement::NameElement::NameType nameType;

				switch (name.name_id)
				{
				case TT_NAME_ID_FONT_FAMILY:
					nameType = sfh::FontDatabase::FontFaceElement::NameElement::Win32FamilyName;
					break;
				case TT_NAME_ID_FULL_NAME:
					nameType = sfh::FontDatabase::FontFaceElement::NameElement::FullName;
					break;
				case TT_NAME_ID_PS_NAME:
					nameType = sfh::FontDatabase::FontFaceElement::NameElement::PostScriptName;
					break;
				default:
					continue;
				}

				try
				{
					faceElement.m_names.emplace_back(nameType, ConvertSfntName(name));
				}
				catch (...)
				{
					// ignore exception, discarding this name
				}
			}

			std::sort(faceElement.m_names.begin(), faceElement.m_names.end());
			faceElement.m_names.erase(
				std::unique(faceElement.m_names.begin(), faceElement.m_names.end()),
				faceElement.m_names.end());

			ret.emplace_back(std::move(faceElement));
		}
		return ret;
	}
};

FontAnalyzer::FontAnalyzer()
	: m_impl(std::make_unique<Implementation>())
{
}

FontAnalyzer::~FontAnalyzer() = default;

std::vector<sfh::FontDatabase::FontFaceElement> FontAnalyzer::AnalyzeFontFile(const wchar_t* path)
{
	return m_impl->AnalyzeFontFile(path);
}
