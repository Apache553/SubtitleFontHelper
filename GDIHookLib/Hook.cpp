#include <Windows.h>

#include <string>
#include <ctime>
#include <cstdint>
#include <unordered_set>
#include <mutex>

#include "Common.h"
#include "Hook.h"

#define TIMEOUT_MSEC 500

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

// global query cache, leaks memory but doesn't matter
static std::unordered_set<std::wstring>* query_cache = nullptr;
// cache mutex
static std::mutex cache_mutex;

/**
	This function first check if the external mutex exists.
	If true, then open shared memory and a event to invoke a request
	else just returning a empty string
*/
std::wstring QueryExternalSourceFontPath(const std::wstring& name) {
	if (query_cache == nullptr) {
		std::lock_guard<std::mutex> lg(cache_mutex);
		query_cache = new std::unordered_set<std::wstring>;
	}

	// skip query if it exists
	{
		std::lock_guard<std::mutex> lg(cache_mutex);
		if (query_cache->find(name) != query_cache->end())return std::wstring();
	}

	if (name.empty())return std::wstring();

	static bool b_failure = false;
	if (b_failure)return std::wstring();

	HANDLE h_mutex = NULL;
	HANDLE h_mapping = NULL;
	HANDLE h_event = NULL;
	DWORD mutex_result = 0;
	DWORD event_result = 0;
	void* memptr = nullptr;
	FontPathRequest* req = nullptr;
	std::wstring ret{};

	h_mutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"FontPathQueryMutex");
	if (h_mutex == NULL)goto ONERROR;
	h_event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"FontPathQueryEvent");
	if (h_event == NULL)goto ONERROR;
	h_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"FontPathQueryBuffer");
	if (h_mapping == NULL)goto ONERROR;

	memptr = MapViewOfFile(h_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (memptr == nullptr)goto ONERROR;

	mutex_result = WaitForSingleObject(h_mutex, TIMEOUT_MSEC);
	if (mutex_result != WAIT_OBJECT_0)goto ONERROR;

	// Aquired access to query

	req = reinterpret_cast<FontPathRequest*>(memptr);
	req->length = name.size();
	req->done = 0;
	for (size_t i = 0; i < name.size(); ++i) {
		req->data[i] = name[i];
	}

	// Do query
	SetEvent(h_event);
	event_result = WaitForSingleObject(h_event, TIMEOUT_MSEC);
	if (event_result != WAIT_OBJECT_0)goto ONERROR;

	if (req->done & SUCCESS_BIT) {
		std::lock_guard<std::mutex> lg(cache_mutex);
		query_cache->insert(name);
	}
	ret.clear();
	for (size_t i = 0; i < req->length; ++i) {
		ret.push_back(req->data[i]);
	}

	goto CLEANUP;
ONERROR:
	// b_failure = true;
	// MessageBoxW(NULL, L"Unable to make remote font path query.", L"QueryExternalSourceFontPath", MB_OK | MB_ICONERROR);
CLEANUP:
	if (h_mutex) {
		ReleaseMutex(h_mutex);
		CloseHandle(h_mutex);
	}
	if (memptr)UnmapViewOfFile(memptr);
	if (h_mapping)CloseHandle(h_mapping);
	if (h_event)CloseHandle(h_event);
	return ret;
}

void InjectNotification() {
	std::wstring my_exe = GetHModulePath(NULL);
	HANDLE h_mutex = NULL;
	HANDLE h_mapping = NULL;
	HANDLE h_event = NULL;
	DWORD mutex_result = 0;
	DWORD event_result = 0;
	void* memptr = nullptr;
	FontPathRequest* req = nullptr;
	std::wstring ret{};

	if (my_exe.empty())return;

	h_mutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"FontPathQueryMutex");
	if (h_mutex == NULL)goto ONERROR;
	h_event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"FontPathQueryEvent");
	if (h_event == NULL)goto ONERROR;
	h_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"FontPathQueryBuffer");
	if (h_mapping == NULL)goto ONERROR;

	memptr = MapViewOfFile(h_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (memptr == nullptr)goto ONERROR;

	mutex_result = WaitForSingleObject(h_mutex, TIMEOUT_MSEC);
	if (mutex_result != WAIT_OBJECT_0)goto ONERROR;

	// Aquired access to query

	req = reinterpret_cast<FontPathRequest*>(memptr);
	req->length = my_exe.size();
	for (size_t i = 0; i < my_exe.size(); ++i) {
		req->data[i] = my_exe[i];
	}
	req->done = NOTIFY_BIT;

	SetEvent(h_event);
	event_result = WaitForSingleObject(h_event, TIMEOUT_MSEC);
	if (event_result != WAIT_OBJECT_0)goto ONERROR;

	goto CLEANUP;
ONERROR:
CLEANUP:
	if (h_mutex) {
		ReleaseMutex(h_mutex);
		CloseHandle(h_mutex);
	}
	if (memptr)UnmapViewOfFile(memptr);
	if (h_mapping)CloseHandle(h_mapping);
	if (h_event)CloseHandle(h_event);
}

void QueryAndLoadFont(const wchar_t* name)
{
	std::wstring path = QueryExternalSourceFontPath(name);
	if (!path.empty()) {
		AddFontResourceExW(path.c_str(), FR_PRIVATE, 0);
	}
}

void QueryAndLoadFont(const char* name)
{
	wchar_t buffer[LF_FULLFACESIZE];
	if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, name, -1, buffer, LF_FULLFACESIZE) == 0)return;
	QueryAndLoadFont(buffer);
}

extern "C" {

	HFONT WINAPI HookedCreateFontIndirectW(_In_ CONST LOGFONTW* lplf) {
		QueryAndLoadFont(lplf->lfFaceName);
		HFONT ret = True_CreateFontIndirectW(lplf);
		return ret;
	}

	HFONT WINAPI HookedCreateFontIndirectA(_In_ CONST LOGFONTA* lplf) {
		QueryAndLoadFont(lplf->lfFaceName);
		HFONT ret = True_CreateFontIndirectA(lplf);
		return ret;
	}
	HFONT WINAPI HookedCreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, 
		int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, 
		DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
	{
		QueryAndLoadFont(pszFaceName);
		HFONT ret = True_CreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);;
		return ret;
	}
	HFONT WINAPI HookedCreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation,
		int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
		DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
	{
		QueryAndLoadFont(pszFaceName);
		HFONT ret = True_CreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);;
		return ret;
	}

	HFONT WINAPI HookedCreateFontIndirectExA(_In_ CONST ENUMLOGFONTEXDVA* r) {
		QueryAndLoadFont((char*)r->elfEnumLogfontEx.elfFullName);
		QueryAndLoadFont(r->elfEnumLogfontEx.elfLogFont.lfFaceName);
		HFONT ret = True_CreateFontIndirectExA(r);
		return ret;
	}

	HFONT WINAPI HookedCreateFontIndirectExW(_In_ CONST ENUMLOGFONTEXDVW* r) {
		QueryAndLoadFont(r->elfEnumLogfontEx.elfFullName);
		QueryAndLoadFont(r->elfEnumLogfontEx.elfLogFont.lfFaceName);
		HFONT ret = True_CreateFontIndirectExW(r);
		return ret;
	}
}