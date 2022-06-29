#include "pch.h"

#include "Common.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>
#include <wil/resource.h>

#undef min

std::string sfh::WideToUtf8String(const std::wstring& wStr)
{
	std::string ret;
	const int length = WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		wStr.c_str(),
		static_cast<int>(wStr.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	assert(length != 0 && "wide char conversion to utf8 mustn't fail!");
	ret.resize(length);
	WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		wStr.c_str(),
		static_cast<int>(wStr.size()),
		ret.data(),
		length,
		nullptr,
		nullptr);
	return ret;
}

std::wstring sfh::Utf8ToWideString(const std::string& str)
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

void sfh::ErrorMessageBox(const std::wstring& text, const std::wstring& caption, long hResult)
{
	wil::unique_hlocal_string message;
	THROW_LAST_ERROR_IF(FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		nullptr,
		hResult,
		LANG_USER_DEFAULT,
		reinterpret_cast<LPWSTR>(message.put()),
		0,
		nullptr
	) == 0);
	MessageBoxW(nullptr, (text + L"\n\n" + message.get()).c_str(), caption.c_str(), MB_OK);
}

std::string sfh::GetFileContent(const std::wstring& path)
{
	std::string ret;
	wil::unique_file fp;
	if (_wfopen_s(fp.put(), path.c_str(), L"rb"))
		throw std::runtime_error("unable to open file");
	fseek(fp.get(), 0, SEEK_END);
	ret.resize(ftell(fp.get()));
	rewind(fp.get());
	fread(ret.data(), sizeof(char), ret.size(), fp.get());
	return ret;
}

void sfh::SetFileContent(const std::wstring& path, const std::string_view& data)
{
	static constexpr size_t BLOCK_SIZE = 4 * 1024;
	wil::unique_file fp;
	if (_wfopen_s(fp.put(), path.c_str(), L"wb"))
		throw std::runtime_error("unable to open file");
	size_t written = 0;
	while (written != data.size())
	{
		written += fwrite(
			data.data() + written,
			sizeof(std::string_view::value_type),
			std::min(data.size() - written, BLOCK_SIZE),
			fp.get());
	}
}

std::wstring sfh::GetCurrentProcessUserSid()
{
	auto hToken = GetCurrentProcessToken();
	PTOKEN_USER user;
	std::unique_ptr<char[]> buffer;
	DWORD returnLength;
	wil::unique_hlocal_string ret;;
	if (GetTokenInformation(
		hToken,
		TokenUser,
		nullptr,
		0,
		&returnLength) == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		buffer = std::make_unique<char[]>(returnLength);
		user = reinterpret_cast<PTOKEN_USER>(buffer.get());
	}
	else
	{
		MarkUnreachable();
	}
	THROW_LAST_ERROR_IF(GetTokenInformation(
		hToken,
		TokenUser,
		user,
		returnLength,
		&returnLength) == FALSE);
	THROW_LAST_ERROR_IF(ConvertSidToStringSidW(user->User.Sid, ret.put()) == FALSE);
	return ret.get();
}
