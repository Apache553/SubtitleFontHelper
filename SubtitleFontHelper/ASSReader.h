
/*
	TODO: implement a real ass parser
	FIXME: we should not just search certain string in the file(though is works)

*/

#pragma once

#include <string>
#include <fstream>
#include <cstdint>
#include <codecvt>
#include <vector>

class ASSParser {
public:
	/**
		@brief Open a file
		@param filename - utf8 encoded filename
		@return bool - true for success, false for failure
	*/
	bool OpenFile(const std::string& filename);
	/**
		@brief Open a file
		@param filename - wide string filename
		@return bool - true for success, false for failure
	*/
	bool OpenFile(const std::wstring& filename);
	/**
		@brief Get font names referenced by subtitle
		@return a list of font names
	*/
	const std::vector<std::wstring>& GetReferencedFonts();
private:
	std::vector<std::wstring> fonts_list;
};