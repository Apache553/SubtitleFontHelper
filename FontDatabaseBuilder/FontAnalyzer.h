#pragma once

#include "Common.h"
#include "PersistantData.h"

class FontAnalyzer
{
private:
	class Implementation;
	std::unique_ptr<Implementation> m_impl;
public:
	FontAnalyzer();
	~FontAnalyzer();

	FontAnalyzer(const FontAnalyzer&) = delete;
	FontAnalyzer(FontAnalyzer&&) = delete;

	FontAnalyzer& operator=(const FontAnalyzer&) = delete;
	FontAnalyzer& operator=(FontAnalyzer&&) = delete;

	std::vector<sfh::FontDatabase::FontFaceElement> AnalyzeFontFile(const wchar_t* path);
};
