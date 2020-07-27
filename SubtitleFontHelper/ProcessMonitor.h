
#pragma once

#include <string>
#include <functional>
#include <cstdint>

typedef std::function<void(const std::wstring& exec_path, uint32_t process_id)> ProcessCreatedCallback;

class _impl_ProcessMonitor;

class ProcessMonitor {
private:
	_impl_ProcessMonitor* impl;
public:
	ProcessMonitor();
	~ProcessMonitor();

	void AddProcessName(const std::wstring& process);
	void RemoveProcessName(const std::wstring& process);

	void SetCallback(ProcessCreatedCallback);

	void RunMonitor();
	void CancelMonitor();
};