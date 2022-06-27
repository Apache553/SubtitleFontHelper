#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace sfh
{
	class EventLog
	{
	private:
		EventLog();
	public:
		~EventLog();

		static EventLog& GetInstance();

		EventLog(const EventLog&) = delete;
		EventLog(EventLog&&) = delete;

		EventLog& operator=(const EventLog&) = delete;
		EventLog& operator=(EventLog&&) = delete;

		void LogDllAttach(uint32_t processId);
		void LogDllQuerySuccess(uint32_t processId, uint32_t threadId, const wchar_t* requestName,
		                        const std::vector<const wchar_t*> responsePaths);
		void LogDllQueryFailure(uint32_t processId, uint32_t threadId, const wchar_t* requestName,
		                        const wchar_t* reason);

		void LogDaemonTryAttach(uint32_t processId, const wchar_t* processName, const wchar_t* processArchitecture);
		void LogDaemonBumpVersion(uint32_t oldVersion, uint32_t newVersion);

		void LogDllInjectProcessSuccess(uint32_t processId);
		void LogDllInjectProcessFailure(uint32_t processId, const wchar_t* reason);

		void LogDllQueryNoResult(uint32_t processId, uint32_t threadId, const wchar_t* requestName);

		void LogDllLoadFont(uint32_t processId, uint32_t threadId, const wchar_t* path);
	};
}
