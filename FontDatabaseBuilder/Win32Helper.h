#pragma once

inline std::unique_ptr<wchar_t[]> GetCurrentDirectory()
{
	DWORD length = GetCurrentDirectoryW(0, nullptr);
	auto ret = std::make_unique<wchar_t[]>(length);
	GetCurrentDirectoryW(length, ret.get());
	return ret;
}

inline std::unique_ptr<wchar_t[]> GetFullPathName(const wchar_t* path)
{
	DWORD length;
	THROW_LAST_ERROR_IF((length = GetFullPathNameW(path, 0, nullptr, nullptr)) == 0);
	auto ret = std::make_unique<wchar_t[]>(length);
	GetFullPathNameW(path, length, ret.get(), nullptr);
	return ret;
}

inline std::wstring Utf8ToWideString(const std::string& str)
{
	std::wstring ret;
	const int length = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		str.c_str(),
		static_cast<int>(str.size()),
		nullptr,
		0);
	assert(length != 0 && "utf8 conversion to wide char mustn't fail!");
	ret.resize(length);
	MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		str.c_str(),
		static_cast<int>(str.size()),
		ret.data(),
		length);
	return ret;
}

inline bool IsDirectory(const wchar_t* path)
{
	DWORD attr = GetFileAttributesW(path);
	return attr != INVALID_FILE_ATTRIBUTES && attr & FILE_ATTRIBUTE_DIRECTORY;
}

class FileMapping
{
private:
	wil::unique_hfile m_hfile;
	wil::unique_handle m_hmap;
	wil::unique_mapview_ptr<void> m_map;
public:
	FileMapping(const wchar_t* path)
	{
		m_hfile.reset(
			CreateFileW(
				path,
				GENERIC_READ,
				FILE_SHARE_READ,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
				nullptr));
		THROW_LAST_ERROR_IF(!m_hfile.is_valid());

		m_hmap.reset(CreateFileMappingW(
			m_hfile.get(),
			nullptr,
			PAGE_READONLY,
			0,
			0,
			nullptr));
		THROW_LAST_ERROR_IF(!m_hmap.is_valid());

		m_map.reset(MapViewOfFile(
			m_hmap.get(),
			FILE_MAP_READ,
			0,
			0,
			0));
		THROW_LAST_ERROR_IF(m_map.get() == nullptr);
	}

	void* GetMappedPointer() const
	{
		return m_map.get();
	}

	size_t GetMappedLength() const
	{
		MEMORY_BASIC_INFORMATION info;
		THROW_LAST_ERROR_IF(VirtualQuery(m_map.get(), &info, sizeof(info)) == 0);
		return info.RegionSize;
	}

	size_t GetFileLength() const
	{
		FILE_STANDARD_INFO info;
		THROW_LAST_ERROR_IF(GetFileInformationByHandleEx(
			m_hfile.get(),
			FileStandardInfo,
			&info,
			sizeof(info)
		) == FALSE);
		return static_cast<size_t>(info.EndOfFile.QuadPart);
	}
};

template <typename T>
void ScanDirectory(const wchar_t* path, std::vector<std::wstring>& result, std::vector<uint64_t>& sizes, T&& filter)
{
	std::vector<std::wstring> ret;
	const size_t pathLength = wcslen(path);
	const auto subDirectoryBuffer = std::make_unique<wchar_t[]>(
		pathLength + std::extent_v<decltype(WIN32_FIND_DATAW::cFileName)> + 1);
	memcpy(subDirectoryBuffer.get(), path, pathLength * sizeof(wchar_t));
	wchar_t* subDirectoryPointer = subDirectoryBuffer.get() + pathLength - 1;
	if (*subDirectoryPointer != L'\\')
	{
		++subDirectoryPointer;
		*subDirectoryPointer = L'\\';
	}
	++subDirectoryPointer;
	*subDirectoryPointer = 0;

	wil::unique_hfind hFind;
	WIN32_FIND_DATAW data;
	wcscpy_s(subDirectoryPointer, std::extent_v<decltype(WIN32_FIND_DATAW::cFileName)>, L"*");
	hFind.reset(FindFirstFileW(subDirectoryBuffer.get(), &data));
	THROW_LAST_ERROR_IF(!hFind.is_valid());

	do
	{
		ThrowIfCancelled();
		if (wcscmp(data.cFileName, L".") == 0)continue;
		if (wcscmp(data.cFileName, L"..") == 0)continue;
		wcscpy_s(subDirectoryPointer, std::extent_v<decltype(WIN32_FIND_DATAW::cFileName)>, data.cFileName);
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			ScanDirectory(subDirectoryBuffer.get(), result, sizes, std::forward<T>(filter));
		}
		else if (filter(subDirectoryBuffer.get()))
		{
			result.emplace_back(subDirectoryBuffer.get());
			LARGE_INTEGER iSize;
			iSize.HighPart = data.nFileSizeHigh;
			iSize.LowPart = data.nFileSizeLow;
			sizes.push_back(iSize.QuadPart);
		}
	}
	while (FindNextFileW(hFind.get(), &data) != 0);
	THROW_LAST_ERROR_IF(GetLastError() != ERROR_NO_MORE_FILES);
}
