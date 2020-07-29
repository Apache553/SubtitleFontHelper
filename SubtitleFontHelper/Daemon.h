
#pragma once

#include "FontDatabase.h"

#include <atomic>
#include <mutex>
#include <functional>

typedef std::function<void(const std::wstring&, const std::wstring&, bool good)> QueryCallback;

class QueryDaemon {
private:
	FontDatabase& ref_db;
	std::atomic<bool> should_exit;
	std::mutex& db_mutex;
	QueryCallback cb;
public:
	QueryDaemon(FontDatabase& db, std::mutex& mut);
	void SetCallback(QueryCallback cb);
	void RunDaemon();
};