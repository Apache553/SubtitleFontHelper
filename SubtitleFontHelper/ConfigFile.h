
#pragma once

#include <string>
#include <set>

struct MyConfig {
public:
	std::set<std::wstring> index_files;
	std::set<std::wstring> monitored_process;

	bool ToFile(const std::wstring& filename)const;

	static MyConfig FromFile(const std::wstring& filename);
};