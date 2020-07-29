
#include "Common.h"
#include "ProcessMonitor.h"

#include <unordered_set>
#include <mutex>
#include <cwctype>
#include <stdexcept>
#include <condition_variable>
#include <algorithm>
#include <exception>
#include <queue>
#include <atomic>
#include <thread>

#define _WIN32_DCOM
#include <comdef.h>
#include <Wbemidl.h>

static std::wstring GetProcessPath(DWORD pid);

typedef std::function<void(uint32_t process_id)> EventSinkProcessCreatedCallback;

class EventSink : public IWbemObjectSink
{
private:
	LONG m_lRef;
	bool bDone;

	EventSinkProcessCreatedCallback cb;
public:
	EventSink() { m_lRef = 0; bDone = false; }
	~EventSink() { bDone = true; }

	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();
	virtual HRESULT
		STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);

	virtual HRESULT STDMETHODCALLTYPE Indicate(
		LONG lObjectCount,
		IWbemClassObject __RPC_FAR* __RPC_FAR* apObjArray
	);

	virtual HRESULT STDMETHODCALLTYPE SetStatus(
		/* [in] */ LONG lFlags,
		/* [in] */ HRESULT hResult,
		/* [in] */ BSTR strParam,
		/* [in] */ IWbemClassObject __RPC_FAR* pObjParam
	);

	void SetCallback(EventSinkProcessCreatedCallback fn);
};

class _impl_ProcessMonitor {
private:
	std::unordered_set<std::wstring, CaseInsensitiveHasher, CaseInsensitiveEqual> process_list;
	std::mutex list_mutex;

	std::queue<uint32_t> event_queue;
	std::mutex msg_queue_mutex;
	std::condition_variable cond_var;

	std::thread monitor_thread;
	std::mutex run_mutex; // lock while r/w other members
	std::atomic<bool> should_cancel = false;

	std::atomic<bool> cleaned = true;
	std::exception_ptr last_exception = nullptr;

	IWbemLocator* pLocator = nullptr;
	IWbemServices* pService = nullptr;
	IUnsecuredApartment* pApart = nullptr;
	EventSink* pEventSink = nullptr;
	IUnknown* pStubUnknown = nullptr;
	IWbemObjectSink* pStubSink = nullptr;

	bool full_init = false;
	bool com_init = false;
	std::atomic<bool> running = false;

	float poll_inteval = 1.0f;

	ProcessCreatedCallback cb;

	void EventSinkCallback(uint32_t process_id);

	void Init();
	void CleanUp();

	void Start();
	void Stop();

public:
	_impl_ProcessMonitor();
	~_impl_ProcessMonitor();

	void RunMonitor();      // initialize everything and run
	void CancelMonitor();   // uninitialize everything

	void AddProcessName(const std::wstring& process);
	void RemoveProcessName(const std::wstring& process);

	void SetCallback(ProcessCreatedCallback cb);

	std::exception_ptr GetLastException();

	void BusyWaitUntilStatusClean();

	bool IsRunning()const;

	void SetPollInterval(float i);

	static int AdjustPrivilege();
};


ULONG EventSink::AddRef()
{
	return InterlockedIncrement(&m_lRef);
}

ULONG EventSink::Release()
{
	LONG lRef = InterlockedDecrement(&m_lRef);
	if (lRef == 0)
		delete this;
	return lRef;
}

HRESULT EventSink::QueryInterface(REFIID riid, void** ppv)
{
	if (riid == IID_IUnknown || riid == IID_IWbemObjectSink)
	{
		*ppv = (IWbemObjectSink*)this;
		AddRef();
		return WBEM_S_NO_ERROR;
	}
	else return E_NOINTERFACE;
}

HRESULT EventSink::Indicate(LONG lObjectCount, IWbemClassObject** apObjArray)
{
	// we needn't to call AddRef/Release on each pointer since we used those boys inside function
	HRESULT hres = S_OK;
	for (int i = 0; i < lObjectCount; i++)
	{
		IWbemClassObject* obj = apObjArray[i];
		VARIANT instance;
		instance.vt = VT_EMPTY;
		hres = obj->Get(L"TargetInstance", 0, &instance, nullptr, nullptr);
		if (hres == WBEM_S_NO_ERROR && instance.vt != VT_EMPTY) {
			IUnknown* pUnkObj = instance.punkVal;
			IWbemClassObject* pWbemClsObj = nullptr;
			hres = pUnkObj->QueryInterface(IID_IWbemClassObject, (void**)&pWbemClsObj);
			if (hres == S_OK) {
				auto oses = g_logger.GetNewSession();
				oses.Debug(L"New WMI __InstanceCreationEvent");
				VARIANT exec_path;
				VARIANT process_id;
				exec_path.vt = VT_EMPTY;
				process_id.vt = VT_EMPTY;
				hres = pWbemClsObj->Get(L"ProcessId", 0, &process_id, nullptr, nullptr);
				if (hres == WBEM_S_NO_ERROR && process_id.vt != VT_EMPTY) {
					oses.SetLogLevel(LogLevel::Debug).PrintHeader() << L"  ProcessId is " << process_id.uintVal << L"\n";
					cb(process_id.uintVal);
					VariantClear(&process_id);
				}
				else {
					oses.Error(L"  Unable to ProcessId of __InstanceCreationEvent.");
				}

				pWbemClsObj->Release();
			}
			// this does IUnknown::Release
			VariantClear(&instance);
		}
	}

	return WBEM_S_NO_ERROR;
}

HRESULT EventSink::SetStatus(
	/* [in] */ LONG lFlags,
	/* [in] */ HRESULT hResult,
	/* [in] */ BSTR strParam,
	/* [in] */ IWbemClassObject __RPC_FAR* pObjParam
)
{
	// I dont care :P
	if (lFlags == WBEM_STATUS_COMPLETE)
	{
		//TODO
	}
	else if (lFlags == WBEM_STATUS_PROGRESS)
	{
		//TODO
	}

	return WBEM_S_NO_ERROR;
}

void EventSink::SetCallback(EventSinkProcessCreatedCallback fn)
{
	cb = fn;
}

void _impl_ProcessMonitor::EventSinkCallback(uint32_t process_id)
{
	{
		std::lock_guard<std::mutex> lg(msg_queue_mutex);
		event_queue.push(process_id);
	}
	cond_var.notify_one();
}

_impl_ProcessMonitor::_impl_ProcessMonitor()
{
	// delayed initialize for multithreading
}

_impl_ProcessMonitor::~_impl_ProcessMonitor()
{
	CancelMonitor();
	if (monitor_thread.joinable())monitor_thread.join();
}

void _impl_ProcessMonitor::Init()
{
	std::lock_guard<std::mutex> lg(run_mutex);
	if (full_init)return;
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);;
	if (hr != S_OK && hr != S_FALSE) {
		throw std::runtime_error("Unable to initialize COM");
	}
	else {
		com_init = true;
	}
	// Init Locator
	hr = CoCreateInstance(
		CLSID_WbemLocator,
		0,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator, (LPVOID*)&pLocator);
	if (hr != S_OK)throw std::runtime_error("CoCreateInstance CLSID_WbemLocator failed");

	// Init Service
	hr = pLocator->ConnectServer(
		_bstr_t(L"ROOT\\CIMV2"),
		NULL,
		NULL,
		0,
		NULL,
		0,
		0,
		&pService
	);
	if (hr != S_OK)throw std::runtime_error("pLocator->ConnectServer failed");

	// Set Proxy Blanket
	hr = CoSetProxyBlanket(
		pService,                    // Indicates the proxy to set
		RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx 
		RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx 
		NULL,                        // Server principal name 
		RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
		RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
		NULL,                        // client identity
		EOAC_NONE                    // proxy capabilities 
	);
	if (hr != S_OK)throw std::runtime_error("CoSetProxyBlanket failed");

	// Obtain Security Apartment
	hr = CoCreateInstance(CLSID_UnsecuredApartment, NULL,
		CLSCTX_LOCAL_SERVER, IID_IUnsecuredApartment,
		(void**)&pApart);
	if (hr != S_OK)throw std::runtime_error("CoCreateInstance CLSID_UnsecuredApartment failed");

	// setup event sink
	pEventSink = new EventSink;
	pEventSink->AddRef();
	pEventSink->SetCallback([this](uint64_t process_id) {
		this->EventSinkCallback(process_id);
		});

	hr = pApart->CreateObjectStub(pEventSink, &pStubUnknown);
	if (hr != S_OK)throw std::runtime_error("pApart->CreateObjectStub failed");

	hr = pStubUnknown->QueryInterface(IID_IWbemObjectSink, (void**)&pStubSink);
	if (hr != S_OK)throw std::runtime_error("pStubUnknown->QueryInterface IID_IWbemObjectSink failed");

	// well done
	full_init = true;
}

template<typename T>
inline void ReleaseAndSetNULL(T& ptr) {
	if (ptr) {
		ptr->Release();
		ptr = nullptr;
	}
}

void _impl_ProcessMonitor::CleanUp()
{
	std::lock_guard<std::mutex> lg(run_mutex);
	ReleaseAndSetNULL(pLocator);
	ReleaseAndSetNULL(pService);
	ReleaseAndSetNULL(pApart);
	ReleaseAndSetNULL(pStubUnknown);
	ReleaseAndSetNULL(pEventSink);
	ReleaseAndSetNULL(pStubSink);
	if (com_init) {
		CoUninitialize();
		com_init = false;
	}
	running = false;
	full_init = false;
	cleaned = true;
}

void _impl_ProcessMonitor::Start()
{
	std::lock_guard<std::mutex> lg(run_mutex);
	if (!full_init)return;
	if (running)return;
	const char* wql_fmt = "SELECT * "
		"FROM __InstanceCreationEvent WITHIN %.3f "
		"WHERE TargetInstance ISA 'Win32_Process'";
	size_t wql_size = size_t(std::snprintf(nullptr, 0, wql_fmt, poll_inteval)) + 1;
	std::unique_ptr<char[]> wql_buffer(new char[wql_size]);
	std::snprintf(wql_buffer.get(), wql_size, wql_fmt, poll_inteval);
	HRESULT hr = pService->ExecNotificationQueryAsync(
		_bstr_t("WQL"),
		_bstr_t(wql_buffer.get()),
		WBEM_FLAG_SEND_STATUS,
		NULL,
		pStubSink);
	if (hr != S_OK)throw std::runtime_error("pService->ExecNotificationQueryAsync failed");
	running = true;
}

void _impl_ProcessMonitor::Stop()
{
	std::lock_guard<std::mutex> lg(run_mutex);
	if (!full_init)return;
	if (!running)return;
	pService->CancelAsyncCall(pStubSink);
	running = false;
}

int _impl_ProcessMonitor::AdjustPrivilege()
{
	int error_code = ERROR_SUCCESS;
	HANDLE h_token = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &h_token)) {
		LUID dbg_luid;
		if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &dbg_luid)) {
			TOKEN_PRIVILEGES token_priv;
			token_priv.PrivilegeCount = 1;
			token_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			token_priv.Privileges[0].Luid = dbg_luid;
			if (AdjustTokenPrivileges(h_token, FALSE, &token_priv, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
				error_code = GetLastError();
			}
		}
		else {
			error_code = GetLastError();
		}
		CloseHandle(h_token);
	}
	else {
		error_code = GetLastError();
	}
	return error_code;
}

void _impl_ProcessMonitor::RunMonitor()
{
	if (cleaned.load() == false)throw std::runtime_error("ProcessMonitor status not clean");
	should_cancel.store(false);
	monitor_thread = std::thread([&]() {
		try {
			// do init
			Init();
			// start listening
			Start();
			// process event
			while (!should_cancel.load()) {
				std::unique_lock<std::mutex> ul(msg_queue_mutex);
				// wait until we should cancel or an event comes
				cond_var.wait(ul, [&]() {return should_cancel.load() || !event_queue.empty(); });
				if (!event_queue.empty()) {
					// process event, one per loop
					auto pid = event_queue.front();
					event_queue.pop();
					ul.unlock();
					std::wstring exe_path = GetProcessPath(pid);
					if (exe_path.empty()) {
						g_logger.GetNewSession().Debug(L"Unable to get ExecutablePath for ", pid);
						continue;
					}
					else {
						g_logger.GetNewSession().Debug(L"PID, ExecutablePath = ", pid, L", ", exe_path);
					}
					bool process_in_list = false;
					size_t fpos = exe_path.rfind(L'\\');
					if (fpos != std::wstring::npos && exe_path.back() != L'\\') {
						std::lock_guard<std::mutex> lg(list_mutex);
						process_in_list = process_list.find(exe_path.substr(fpos + 1)) != process_list.end();
					}
					if (process_in_list) {
						cb(exe_path, pid);
					}
				}
			}
		}
		catch (...) {
			std::lock_guard<std::mutex> lg(run_mutex);
			last_exception = std::current_exception();
		}
		Stop();
		CleanUp();
		});
}

void _impl_ProcessMonitor::CancelMonitor()
{
	{
		std::lock_guard<std::mutex> lg(run_mutex);
		if (!running)return;
	}
	should_cancel.store(true);
	cond_var.notify_one();
	monitor_thread.join();
}

void _impl_ProcessMonitor::AddProcessName(const std::wstring& process)
{
	if (CaseInsensitiveEqual()(process, L"rundll32.exe")) {
		g_logger.GetNewSession().Error(L"It is impossible to monitor rundll32.exe");
	}
	{
		std::lock_guard<std::mutex> lg(list_mutex);
		process_list.insert(process);
	}
}

void _impl_ProcessMonitor::RemoveProcessName(const std::wstring& process)
{
	{
		std::lock_guard<std::mutex> lg(list_mutex);
		auto iter = process_list.find(process);
		if (iter != process_list.end()) {
			process_list.erase(iter);
		}
	}
}

void _impl_ProcessMonitor::SetCallback(ProcessCreatedCallback cb)
{
	std::lock_guard<std::mutex> lg(run_mutex);
	this->cb = cb;
}

std::exception_ptr _impl_ProcessMonitor::GetLastException()
{
	std::lock_guard<std::mutex> lg(run_mutex);
	return last_exception;
}

void _impl_ProcessMonitor::BusyWaitUntilStatusClean()
{
	while (cleaned.load() == false) {
		std::this_thread::yield();
	}
}

bool _impl_ProcessMonitor::IsRunning() const
{
	return running;
}

void _impl_ProcessMonitor::SetPollInterval(float i)
{
	std::lock_guard<std::mutex> lg(run_mutex);
	poll_inteval = i;
}

ProcessMonitor::ProcessMonitor()
{
	impl = new _impl_ProcessMonitor();
}

ProcessMonitor::~ProcessMonitor()
{
	delete impl;
}

void ProcessMonitor::AddProcessName(const std::wstring& process)
{
	impl->AddProcessName(process);
}

void ProcessMonitor::RemoveProcessName(const std::wstring& process)
{
	impl->RemoveProcessName(process);
}

void ProcessMonitor::SetCallback(ProcessCreatedCallback fn)
{
	impl->SetCallback(fn);
}

void ProcessMonitor::RunMonitor()
{
	impl->RunMonitor();
}

void ProcessMonitor::CancelMonitor()
{
	impl->CancelMonitor();
}

bool ProcessMonitor::IsRunning()const
{
	return impl->IsRunning();
}

std::exception_ptr ProcessMonitor::GetLastException()
{
	return impl->GetLastException();
}

void ProcessMonitor::SetPollInterval(float interval)
{
	impl->SetPollInterval(interval);
}

int ProcessMonitor::AdjustPrivilege()
{
	return _impl_ProcessMonitor::AdjustPrivilege();
}

static std::wstring GetProcessPath(DWORD pid)
{
	std::wstring ret;
	HANDLE h_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (h_process == NULL)return std::wstring();
	size_t len = 1024;
	std::unique_ptr<wchar_t[]> mod_fn(new wchar_t[len]);
	while (true) {
		DWORD tmp = len;
		SetLastError(ERROR_SUCCESS);
		QueryFullProcessImageNameW(h_process, 0, mod_fn.get(), &tmp);
		//len = GetModuleFileNameExW(h_process, NULL, mod_fn.get(), len);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			len *= 1.5;
			if (len > 32767)break;
			delete[] mod_fn.release();
			mod_fn.reset(new wchar_t[len]);
		}
		else if (GetLastError() == ERROR_SUCCESS) {
			ret = mod_fn.get();
			break;
		}
		else {
			break;
		}
	}
	CloseHandle(h_process);
	return ret;
}