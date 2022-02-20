#pragma once

#include "Win32Helper.h"

struct SetOutputAttr
{
	WORD attribute;

	void SetAttribute() const
	{
		HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)SetConsoleTextAttribute(handle, attribute);
	}
};

inline std::ostream& operator<<(std::ostream& os, const SetOutputAttr& attr)
{
	attr.SetAttribute();
	return os;
}

inline std::wostream& operator<<(std::wostream& os, const SetOutputAttr& attr)
{
	attr.SetAttribute();
	return os;
}

const SetOutputAttr SetOutputDefault{
	[]()-> WORD
	{
		CONSOLE_SCREEN_BUFFER_INFO info;
		HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle == INVALID_HANDLE_VALUE || handle == nullptr)return 0;
		if (!GetConsoleScreenBufferInfo(handle, &info))return 0;
		return info.wAttributes;
	}()
};
const SetOutputAttr SetOutputRed{FOREGROUND_RED};
const SetOutputAttr SetOutputGreen{FOREGROUND_GREEN};
const SetOutputAttr SetOutputBlue{FOREGROUND_BLUE};
const SetOutputAttr SetOutputCyan{FOREGROUND_BLUE | FOREGROUND_GREEN};
const SetOutputAttr SetOutputMagenta{FOREGROUND_BLUE | FOREGROUND_RED};
const SetOutputAttr SetOutputYellow{FOREGROUND_GREEN | FOREGROUND_RED};
const SetOutputAttr SetOutputWhite{FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN};

struct EraseLineStruct
{
	static void EraseLine()
	{
		HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			CONSOLE_SCREEN_BUFFER_INFO info;
			if (GetConsoleScreenBufferInfo(handle, &info))
			{
				info.dwCursorPosition.X = 0;
				DWORD written;
				SetConsoleCursorPosition(handle, info.dwCursorPosition);
				FillConsoleOutputCharacterW(handle, L' ', info.dwMaximumWindowSize.X, info.dwCursorPosition, &written);
			}
		}
	}
};

const EraseLineStruct EraseLine;

inline std::ostream& operator<<(std::ostream& os, const EraseLineStruct& attr)
{
	EraseLineStruct::EraseLine();
	return os;
}

inline std::wostream& operator<<(std::wostream& os, const EraseLineStruct& attr)
{
	EraseLineStruct::EraseLine();
	return os;
}

std::wstring ConsoleReadLine()
{
	std::wstring ret;
	wchar_t c;
	DWORD readBytes;
	while (true)
	{
		THROW_LAST_ERROR_IF(ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &readBytes, nullptr) == FALSE);
		if (readBytes == 0)
		{
			throw std::logic_error("Ctrl-C signaled");
			break;
		}
		if (c == L'\n')
			break;
		if (std::iswcntrl(c))
			continue;
		ret.push_back(c);
	}
	return ret;
}


inline void PrintProgressBar(size_t done, size_t total, size_t barWidth)
{
	std::wcout << "Progress: " << std::setfill(L' ') << std::setw(7) << done << '/' << std::setw(7) << total << ' ';
	size_t filled = barWidth * done / total;
	std::wcout << '[';
	if (filled > 0)std::wcout << std::setfill(L'#') << std::setw(filled) << '#';
	if (barWidth - filled > 0)
		std::wcout << std::setfill(L' ') << std::setw(static_cast<std::streamsize>(barWidth - filled)) << ' ';
	std::wcout << ']';
	std::wcout << ' ' << std::setprecision(4) << static_cast<double>(done) / static_cast<double>(total) * 100 << '%';
}

inline bool AskConsoleQuestionBoolean(const std::wstring& prompt)
{
	std::wstring ans;
	while (!g_cancelToken)
	{
		std::wcout << SetOutputDefault << prompt << " [Y/N] ";
		ans = ConsoleReadLine();
		if (_wcsicmp(ans.c_str(), L"Y") == 0)
			return true;
		if (_wcsicmp(ans.c_str(), L"N") == 0)
			return false;
	}
	__assume(false);
}
