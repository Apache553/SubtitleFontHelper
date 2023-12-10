#include "pch.h"
#include "EventLog.h"

#include <cassert>
#include <sstream>
#include <iomanip>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "event.h"

constexpr size_t INITIAL_BUFFER_SIZE = 256;


sfh::EventLog::EventLog()
{
	EventRegisterSubtitleFontHelper();
}

sfh::EventLog::~EventLog()
{
	EventUnregisterSubtitleFontHelper();
}

sfh::EventLog& sfh::EventLog::GetInstance()
{
	static EventLog instance;
	return instance;
}

void sfh::EventLog::LogDllAttach(uint32_t processId)
{
	EventWriteDllAttach(processId);
}

void sfh::EventLog::LogDllQuerySuccess(uint32_t processId, uint32_t threadId, const wchar_t* requestName,
	const std::vector<const wchar_t*> responsePaths)
{
	if (!MCGEN_EVENT_ENABLED(DllQuerySuccess))
		return;

	ULONG dataCount = 4 + static_cast<ULONG>(responsePaths.size());
	auto data = std::make_unique<EVENT_DATA_DESCRIPTOR[]>(dataCount);
	uint32_t pathLength = static_cast<uint32_t>(responsePaths.size());

	EventDataDescCreate(&data[0], &processId, sizeof(uint32_t));
	EventDataDescCreate(&data[1], &threadId, sizeof(uint32_t));
	EventDataDescCreate(&data[2], requestName ? requestName : L"NULL",
		static_cast<ULONG>(requestName
			? (wcslen(requestName) + 1) * sizeof(wchar_t)
			: sizeof(L"NULL")));
	EventDataDescCreate(&data[3], &pathLength, sizeof(uint32_t));
	for (size_t i = 0; i < responsePaths.size(); ++i)
	{
		auto path = responsePaths[i];
		EventDataDescCreate(&data[4 + i], path ? path : L"NULL",
			static_cast<ULONG>(path ? (wcslen(path) + 1) * sizeof(wchar_t) : sizeof(L"NULL")));
	}
	EventWrite(SubtitleFontHelper_Context.RegistrationHandle, &DllQuerySuccess, dataCount, data.get());
}

void sfh::EventLog::LogDllQueryFailure(uint32_t processId, uint32_t threadId, const wchar_t* requestName,
	const wchar_t* reason)
{
	EventWriteDllQueryFailure(processId, threadId, requestName, reason);
}

void sfh::EventLog::LogDaemonTryAttach(uint32_t processId, const wchar_t* processName,
	const wchar_t* processArchitecture)
{
	EventWriteDaemonTryAttach(processId, processName, processArchitecture);
}

void sfh::EventLog::LogDaemonBumpVersion(uint32_t oldVersion, uint32_t newVersion)
{
	EventWriteDaemonBumpVersion(oldVersion, newVersion);
}

void sfh::EventLog::LogDllInjectProcessSuccess(uint32_t processId)
{
	EventWriteDllInjectProcessSuccess(processId);
}

void sfh::EventLog::LogDllInjectProcessFailure(uint32_t processId, const wchar_t* reason)
{
	EventWriteDllInjectProcessFailure(processId, reason);
}

void sfh::EventLog::LogDllQueryNoResult(uint32_t processId, uint32_t threadId, const wchar_t* requestName)
{
	EventWriteDllQueryNoResult(processId, threadId, requestName);
}

void sfh::EventLog::LogDllLoadFont(uint32_t processId, uint32_t threadId, const wchar_t* path)
{
	EventWriteDllLoadFont(processId, threadId, path);
}

static std::wstring AnsiToWideString(const std::string& str)
{
	std::wstring ret;
	const int length = MultiByteToWideChar(
		CP_ACP,
		0,
		str.c_str(),
		static_cast<int>(str.size()),
		nullptr,
		0);
	if (length == 0)
	{
		// conversion failed, return hex string
		std::wostringstream oss;
		oss <<L"Invalid String: " <<std::hex;
		for (auto ch :str)
		{
			oss << std::setw(2) << std::setfill(L'0') << static_cast<unsigned int>(ch);
		}
		return oss.str();
	}
	ret.resize(length);
	MultiByteToWideChar(
		CP_ACP,
		0,
		str.c_str(),
		static_cast<int>(str.size()),
		ret.data(),
		length);
	return ret;
}

void sfh::EventLog::LogDebugMessageSingle(const wchar_t* str)
{
	EventWriteDebugLog(str);
}

void sfh::EventLog::LogDebugMessage(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	size_t bufferSize = INITIAL_BUFFER_SIZE;
	std::unique_ptr<char[]> buffer;
	while (true) {
		buffer = std::make_unique<char[]>(bufferSize);
		int outputLength = vsnprintf(buffer.get(), bufferSize, fmt, args);
		if (outputLength >= bufferSize)
		{
			// truncated
			bufferSize += bufferSize / 2;
		}
		else
		{
			break;
		}
	}
	va_end(args);
	LogDebugMessageSingle(AnsiToWideString(buffer.get()).c_str());
}

void sfh::EventLog::LogDebugMessage(const wchar_t* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	size_t bufferSize = INITIAL_BUFFER_SIZE;
	std::unique_ptr<wchar_t[]> buffer;
	while (true) {
		buffer = std::make_unique<wchar_t[]>(bufferSize);
		int outputLength = vswprintf(buffer.get(), bufferSize, fmt, args);
		if (outputLength >= bufferSize)
		{
			// truncated
			bufferSize += bufferSize / 2;
		}
		else
		{
			break;
		}
	}
	va_end(args);
	LogDebugMessageSingle(buffer.get());
}
