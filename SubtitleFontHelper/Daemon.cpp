
#include "Daemon.h"

#include <cstdint>
#include <string>
#include <stdexcept>

#include <Windows.h>

#include "Common.h"

#pragma pack(push, 4)
struct FontPathRequest {
	volatile uint32_t done;
	volatile uint32_t length;
	volatile wchar_t data[1];
};
#pragma pack(pop)

#define DONE_BIT (0x1)
#define SUCCESS_BIT (0x2)
#define NOTIFY_BIT (0x4)

struct HandleWarpper {
	HANDLE handle;
	HandleWarpper():handle(NULL){}
	~HandleWarpper() { if (handle)CloseHandle(handle); }
	operator HANDLE() {
		return handle;
	}
	HANDLE operator=(HANDLE h) {
		handle = h;
		return handle;
	}
};

struct FileMapWarpper {
	void* ptr;

	void Set(HANDLE mapping) {
		ptr = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (ptr == nullptr)throw std::runtime_error("Unable to map memory");
	}

	FileMapWarpper() {
		ptr = nullptr;
	}
	FileMapWarpper(HANDLE mapping) {
		Set(mapping);
	}
	~FileMapWarpper() {
		UnmapViewOfFile(ptr);
	}
};

QueryDaemon::QueryDaemon(FontDatabase& db, std::mutex& mut) :ref_db(db), db_mutex(mut)
{
	cb = [](const std::wstring&, const std::wstring&) {};
}

void QueryDaemon::SetCallback(QueryCallback cb)
{
	this->cb = cb;
}

void QueryDaemon::RunDaemon()
{
	HandleWarpper h_mutex, h_event, h_mapping;
	FileMapWarpper memmap;
	
	FontPathRequest* req = nullptr;

	SystemFontManager sys_fnt;

	DWORD wait_result;

	constexpr size_t buffer_size = 4096; // 4 KiB
	
	h_mutex = CreateMutexW(NULL, FALSE, L"FontPathQueryMutex");
	if (h_mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)throw std::runtime_error("Unable to create mutex");
	h_event = CreateEventW(NULL, FALSE, FALSE, L"FontPathQueryEvent");
	if (h_event == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)throw std::runtime_error("Unable to create event");
	h_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, buffer_size, L"FontPathQueryBuffer");
	if (h_mapping == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)throw std::runtime_error("Unable to create shared memory");

	memmap.Set(h_mapping);
	req = reinterpret_cast<FontPathRequest*>(memmap.ptr);

	while (!should_exit.load()) {
		wait_result = WaitForSingleObject(h_event, 1000);
		if (wait_result == WAIT_TIMEOUT)continue;
		if (wait_result != WAIT_OBJECT_0)throw std::runtime_error("Unexpected wait result");
		if (req->done & DONE_BIT) {
			SetEvent(h_event);
			continue;
		}
		// do job

		std::wstring name;
		for (size_t i = 0; i < req->length; ++i) {
			name.push_back(req->data[i]);
		}

		if (req->done & NOTIFY_BIT) {
			req->done = 0;
			cb(L"Event: <Remote Process Hook>", L"ExePath: " + name);
			SetEvent(h_event);
		}
		else {
			req->done = 0;

			if (sys_fnt.QuerySystemFontNoExport(name)) {
				req->done |= SUCCESS_BIT;
				dbgout << name << L" already in system\n";
				cb(L"QueryFont: " + name, L"Result: <Installed In System>");
			}
			else {
				try {
					FontItem item;
					{
						std::lock_guard<std::mutex> guard(db_mutex);
						item = ref_db.QueryFont(name);
					}
					req->length = item.path.size();
					for (size_t i = 0; i < req->length; ++i) {
						req->data[i] = item.path[i];
					}
					req->done |= SUCCESS_BIT;
					dbgout << item.name << L": " << item.path << L'\n';
					cb(L"QueryFont: " + name, L"Result: "+item.path);
				}
				catch (std::out_of_range e) {
					req->length = 0;
					req->data[0] = 0;
					dbgout << name << L" not found in index\n";
					cb(L"QueryFont: " + name, L"Result: Not found in index");
				}
			}

			req->done |= DONE_BIT;
			SetEvent(h_event);
		}
	}
}
