
#pragma once

#include <string>
#include <vector>

struct MyConfig {
public:
	std::vector<std::wstring> index_files;

	bool ToFile(const std::wstring& filename)const;

	static MyConfig FromFile(const std::wstring& filename);
};