#include "Logger.h"

MyLoggerSession::MyLoggerSession(MyLogger& _logger, LogLevel _level) :
	logger(&_logger), level(_level), locker(_logger.session_lock)
{
}

MyLoggerSession::MyLoggerSession(MyLoggerSession&& o) :
	logger(o.logger), level(o.level)
{
	locker.swap(o.locker);
}

MyLoggerSession& MyLoggerSession::operator=(MyLoggerSession&& o)
{
	logger = o.logger;
	level = o.level;
	locker.swap(o.locker);
	return *this;
}

MyLoggerSession::~MyLoggerSession()
{
}

MyLoggerSession& MyLoggerSession::operator<<(const std::wstring& str)
{
	for (auto& printfn : logger->fn_vec) {
		if (printfn) {
			printfn(str, level);
		}
	}
	return *this;
}

MyLoggerSession& MyLoggerSession::operator<<(wchar_t ch)
{
	return *this << std::wstring(1, ch);
}

MyLoggerSession& MyLoggerSession::SetLogLevel(LogLevel _level)
{
	level = _level;
	return *this;
}

MyLoggerSession& MyLoggerSession::PrintHeader()
{
	switch (level) {
	case LogLevel::Debug:
		return *this << L"[DEBUG] ";
	case LogLevel::Info:
		return *this << L"[INFO] ";
	case LogLevel::Warning:
		return *this << L"[WARN] ";
	case LogLevel::Error:
		return *this << L"[ERROR] ";
	default:
		return *this;
	}
}

MyLoggerSession MyLogger::GetNewSession(LogLevel _level)
{
	return MyLoggerSession(*this, _level);
}

size_t MyLogger::AddOutputFunc(MyLoggerOutputFunc fn)
{
	std::lock_guard<std::mutex> lg(session_lock);
	for (size_t i = 0; i < fn_vecsiz; ++i) {
		if (fn_vec[i] == false) {
			fn_vec[i] = fn;
			return i;
		}
	}
	return -1;
}

void MyLogger::RemoveOutputFunc(size_t id)
{
	if (id == -1)return;
	std::lock_guard<std::mutex> lg(session_lock);
	fn_vec[id] = MyLoggerOutputFunc();
}

#ifdef _DEBUG
#include <Windows.h>
#endif

MyLogger::MyLogger()
{
#ifdef _DEBUG
	AddOutputFunc([](const std::wstring& str, LogLevel) {
		OutputDebugStringW(str.c_str());
		});
#endif
}
