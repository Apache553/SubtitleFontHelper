

#include <functional>
#include <string>
#include <mutex>
#include <utility>
#include <type_traits>

enum class LogLevel {
	Debug,
	Info,
	Warning,
	Error
};

typedef std::function<void(const std::wstring& str, LogLevel level)> MyLoggerOutputFunc;

class MyLogger;

class MyLoggerSession {
private:
	std::unique_lock<std::mutex> locker;
	friend class MyLogger;
	MyLogger* logger;
	LogLevel level;
public:
	MyLoggerSession(MyLogger& _logger, LogLevel _level = LogLevel::Info);
	MyLoggerSession(MyLoggerSession&&);
	MyLoggerSession(const MyLoggerSession&) = delete;
	MyLoggerSession& operator=(MyLoggerSession&&);
	MyLoggerSession& operator=(const MyLoggerSession&) = delete;
	~MyLoggerSession();

	template<typename T>
	auto operator <<(const T& val) -> decltype(std::to_wstring(val), void(), (*this)) {
		*this << std::to_wstring(val);
		return *this;
	}

	MyLoggerSession& operator <<(const std::wstring& str);

	MyLoggerSession& operator <<(wchar_t ch);

	MyLoggerSession& SetLogLevel(LogLevel _level);

	MyLoggerSession& PrintHeader();

private:
	template<typename T>
	MyLoggerSession& PrintLineHelper(const T& v) {
		return *this << v << L"\n";
	}

	template<typename T, typename ...Args>
	MyLoggerSession& PrintLineHelper(const T& v, Args... rest) {
		return (*this << v).PrintLineHelper(std::forward<Args>(rest)...);
	}

public:
	template<typename ...Args>
	MyLoggerSession& PrintLine(LogLevel _level, Args... arg) {
		LogLevel o_level = level;
		level = _level;
		this->PrintHeader();
		PrintLineHelper(std::forward<Args>(arg)...);
		level = o_level;
		return *this;
	}

	template<typename ...Args>
	MyLoggerSession& Debug(Args... arg) {
		return PrintLine(LogLevel::Debug, std::forward<Args>(arg)...);
	}
	template<typename ...Args>
	MyLoggerSession& Info(Args... arg) {
		return PrintLine(LogLevel::Info, std::forward<Args>(arg)...);
	}
	template<typename ...Args>
	MyLoggerSession& Warning(Args... arg) {
		return PrintLine(LogLevel::Warning, std::forward<Args>(arg)...);
	}
	template<typename ...Args>
	MyLoggerSession& Error(Args... arg) {
		return PrintLine(LogLevel::Error, std::forward<Args>(arg)...);
	}

};

class MyLogger {
private:
	constexpr static size_t fn_vecsiz = 4;
	MyLoggerOutputFunc fn_vec[fn_vecsiz];
	std::mutex session_lock;
	friend class MyLoggerSession;
public:

	MyLoggerSession GetNewSession(LogLevel _level = LogLevel::Info);

	size_t AddOutputFunc(MyLoggerOutputFunc fn);
	void RemoveOutputFunc(size_t id);

	MyLogger();
	MyLogger(MyLogger&&) = delete;
	MyLogger(const MyLogger&) = delete;
	MyLogger& operator=(MyLogger&&) = delete;
	MyLogger& operator=(const MyLogger&) = delete;
};

