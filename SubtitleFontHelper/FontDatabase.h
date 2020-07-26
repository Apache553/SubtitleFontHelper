
#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <deque>
#include <list>
#include <memory>
#include <type_traits>
#include <functional>

struct FontItem {
	std::wstring name;
	std::wstring path;
};

class _impl_SystemFontManager;

class SystemFontManager {
private:
	_impl_SystemFontManager* impl;
public:
	SystemFontManager();
	~SystemFontManager();

	SystemFontManager(const SystemFontManager&) = delete;
	SystemFontManager(SystemFontManager&&) = delete;
	SystemFontManager& operator=(const SystemFontManager&) = delete;
	SystemFontManager& operator=(SystemFontManager&&) = delete;

	/**
		@brief Query system font by name
		@param name - Font name
		@return bool - true: font in system, false: font not in system
	*/
	bool QuerySystemFont(const std::wstring& name);
	/**
		@brief Export system font by name
		@param name - Font name
		@param path - file to store font
		@return true for success
	*/
	bool ExportSystemFont(const std::wstring& name, const std::wstring& path);
	std::pair<std::unique_ptr<char[]>, size_t> ExportSystemFontToMemory(const std::wstring& name);
	/**
		@brief Free tempory memory
	*/
	void ClearState();
};

template<typename T>
struct PointerHasher {
	typedef typename std::remove_pointer<T>::type _ValT;
	typedef typename std::decay<_ValT>::type _HashFnT;
	std::hash<_HashFnT> hash_func;
	size_t operator()(const T& p) const{
		return hash_func(*p);
	}
};

template<typename T>
struct PointerEqual {
	bool operator()(const T& a, const T& b)const {
		return *a == *b;
	}
};

class FontDatabase {
private:
	typedef std::set<std::wstring>::const_iterator FontPathIter;
	std::set<std::wstring> fontpath_set;
	std::unordered_map<std::wstring, FontPathIter> fontname_map;

public:

	FontDatabase();
	~FontDatabase();

	FontDatabase(FontDatabase&&) = delete;
	FontDatabase(const FontDatabase&) = delete;
	FontDatabase& operator=(FontDatabase&&) = delete;
	FontDatabase& operator=(const FontDatabase&) = delete;

	/**
		@brief Open a database and add entries into memory
		@param path - Path to database
		@return true for success, false for failure
	*/
	bool LoadDatabase(const std::wstring& path);
	/**
		@brief Add a new font to database
		@param item - Font item to be added
		@return void
	*/
	void AddItem(const FontItem& item);
	void AddItem(FontItem&& item);
	/**
		@brief Query a certain font in database
		@param name - Display name or unique name
		@return The font item. Will throw std::out_of_range if not exist
	*/
	FontItem QueryFont(const std::wstring& name);
};

std::wstring GetUndecoratedFontName(const std::wstring& name);

typedef std::function<void(const std::wstring&)> VisitCallback;
bool WalkDirectoryAndBuildDatabase(const std::wstring& dir, const std::wstring& db_path, VisitCallback cb,
	bool recursive = true, const std::vector<std::wstring>& ext = { {L".ttf"}, {L".ttc"}, {L".otf"} });