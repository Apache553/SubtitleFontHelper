#pragma once

#include "pch.h"
#include "IDaemon.h"

namespace sfh
{
	class ProcessMonitor
	{
	private:
		class Implementation;
		std::unique_ptr<Implementation> m_impl;
	public:
		ProcessMonitor(IDaemon* daemon, std::chrono::milliseconds interval);
		~ProcessMonitor();

		ProcessMonitor(const ProcessMonitor&) = delete;
		ProcessMonitor(ProcessMonitor&&) = delete;

		ProcessMonitor& operator=(const ProcessMonitor&) = delete;
		ProcessMonitor& operator=(ProcessMonitor&&) = delete;

		void SetMonitorList(std::vector<std::wstring>&& list);
	};
}
