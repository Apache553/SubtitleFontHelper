#include "pch.h"
#include "EventLog.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "event.h"


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
