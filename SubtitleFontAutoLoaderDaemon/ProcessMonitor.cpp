#include "pch.h"

#include "Common.h"
#include "ProcessMonitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WbemIdl.h>
#pragma comment(lib, "wbemuuid.lib")

#include <queue>

#include <wil/win32_helpers.h>
#include <wil/resource.h>
#include <wil/com.h>

class sfh::ProcessMonitor::Implementation
{
private:
	std::mutex m_accessLock;
	std::vector<std::wstring> m_list;

	std::atomic<size_t> m_checkPoint = 0;
	std::atomic<bool> m_exitFlag = false;
	std::thread m_worker;

	IDaemon* m_daemon;
	std::chrono::milliseconds m_interval;

	constexpr static const wchar_t* QUERY_STRING_TEMPLATE =
		L"SELECT * FROM __InstanceCreationEvent WITHIN %.3f WHERE TargetInstance ISA 'Win32_Process'";

	class EventSink : public IWbemObjectSink
	{
	private:
		std::atomic<ULONG> m_refCount;
		Implementation* m_impl;
	public:
		EventSink(Implementation* impl)
			: m_refCount(1), m_impl(impl)
		{
		}

		~EventSink()
		{
		}

		virtual ULONG STDMETHODCALLTYPE AddRef()
		{
			return ++m_refCount;
		}

		virtual ULONG STDMETHODCALLTYPE Release()
		{
			auto i = --m_refCount;
			if (i == 0)
				delete this;
			return i;
		}

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
		{
			if (riid == IID_IUnknown)
			{
				*ppv = static_cast<IUnknown*>(this);
				AddRef();
				return WBEM_S_NO_ERROR;
			}
			if (riid == IID_IWbemObjectSink)
			{
				*ppv = static_cast<IWbemObjectSink*>(this);
				AddRef();
				return WBEM_S_NO_ERROR;
			}
			return E_NOINTERFACE;
		}

		virtual HRESULT STDMETHODCALLTYPE Indicate(
			LONG lObjectCount,
			IWbemClassObject __RPC_FAR* __RPC_FAR* apObjArray
		)
		{
			m_impl->PostNewEvent(apObjArray, lObjectCount);
			return WBEM_S_NO_ERROR;
		}

		virtual HRESULT STDMETHODCALLTYPE SetStatus(
			LONG lFlags,
			HRESULT hResult,
			BSTR strParam,
			IWbemClassObject __RPC_FAR* pObjParam
		)
		{
			return WBEM_S_NO_ERROR;
		}
	};

	std::queue<wil::com_ptr<IWbemClassObject>> m_queue;
	std::mutex m_queueMutex;
	std::condition_variable m_queueCV;

public:
	Implementation(IDaemon* daemon, std::chrono::milliseconds interval)
		: m_daemon(daemon), m_interval(interval)
	{
		m_worker = std::thread([&]()
		{
			try
			{
				WorkerProcedure();
			}
			catch (...)
			{
				++m_checkPoint;
				daemon->NotifyException(std::current_exception());
			}
		});
		while (m_checkPoint.load() == 0)
			std::this_thread::yield();
	}

	~Implementation()
	{
		m_exitFlag = true;
		m_queueCV.notify_one();
		if (m_worker.joinable())
			m_worker.join();
	}

	void SetMonitorList(std::vector<std::wstring>&& list)
	{
		std::lock_guard lg(m_accessLock);
		for (auto& item : list)
		{
			if (_wcsicmp(item.c_str(), L"rundll32.exe") == 0)
				throw std::logic_error("rundll32.exe is not allowed!");
		}
		m_list = std::move(list);
	}

	void PostNewEvent(IWbemClassObject** events, size_t count)
	{
		std::lock_guard lg(m_queueMutex);
		for (size_t i = 0; i < count; ++i)
			m_queue.emplace(events[i]);
		m_queueCV.notify_one();
	}

private:
	void WorkerProcedure()
	{
		auto com = wil::CoInitializeEx();

		THROW_IF_FAILED(CoInitializeSecurity(
			nullptr,
			-1,
			nullptr,
			nullptr,
			RPC_C_AUTHN_LEVEL_DEFAULT,
			RPC_C_IMP_LEVEL_IMPERSONATE,
			nullptr,
			EOAC_NONE,
			nullptr
		));

		auto wbemLocator = wil::CoCreateInstance<IWbemLocator>(CLSID_WbemLocator);
		wil::com_ptr<IWbemServices> wbemService;
		THROW_IF_FAILED(wbemLocator->ConnectServer(
			wil::make_bstr(L"ROOT\\CIMV2").get(),
			nullptr,
			nullptr,
			nullptr,
			0,
			nullptr,
			nullptr,
			wbemService.put()
		));

		THROW_IF_FAILED(CoSetProxyBlanket(
			wbemService.query<IUnknown>().get(),
			RPC_C_AUTHN_DEFAULT,
			RPC_C_AUTHZ_DEFAULT,
			nullptr,
			RPC_C_AUTHN_LEVEL_DEFAULT,
			RPC_C_IMP_LEVEL_IMPERSONATE,
			nullptr,
			EOAC_NONE
		));


		wil::com_ptr<EventSink> sink(new EventSink(this));
		wil::com_ptr<IWbemObjectSink> sinkStub;

		wil::com_ptr<IUnsecuredApartment> apartment;
		THROW_IF_FAILED(CoCreateInstance(
			CLSID_UnsecuredApartment,
			NULL,
			CLSCTX_LOCAL_SERVER,
			IID_IUnsecuredApartment,
			apartment.put_void()));

		auto wbemApartment = apartment.try_query<IWbemUnsecuredApartment>();
		if (wbemApartment)
		{
			wil::com_ptr<IUnknown> unkStub;
			THROW_IF_FAILED(wbemApartment->CreateSinkStub(
				sink.get(),
				WBEM_FLAG_UNSECAPP_DEFAULT_CHECK_ACCESS,
				nullptr,
				reinterpret_cast<IWbemObjectSink**>(unkStub.put())));
			sinkStub = unkStub.query<IWbemObjectSink>();
		}
		else
		{
			wil::com_ptr<IUnknown> unkStub;
			THROW_IF_FAILED(apartment->CreateObjectStub(sink.get(), unkStub.put_unknown()));
			sinkStub = unkStub.query<IWbemObjectSink>();
		}


		wchar_t queryString[128];
		swprintf(queryString, std::extent_v<decltype(queryString)>, QUERY_STRING_TEMPLATE,
		         static_cast<double>(m_interval.count()) / 1000.0);

		THROW_IF_FAILED(wbemService->ExecNotificationQueryAsync(
			wil::make_bstr(L"WQL").get(),
			wil::make_bstr(queryString).get(),
			WBEM_FLAG_SEND_STATUS,
			nullptr,
			sinkStub.get()
		));

		++m_checkPoint;

		while (!m_exitFlag.load())
		{
			std::unique_lock lock(m_queueMutex);
			m_queueCV.wait(lock, [&]() { return !m_queue.empty() || m_exitFlag; });
			if (m_exitFlag)
				break;

			while (!m_queue.empty())
			{
				wil::com_ptr<IWbemClassObject> object = std::move(m_queue.front());
				m_queue.pop();
				lock.unlock();
				try
				{
					HandleProcessCreation(object.get(), wbemService.get());
				}
				catch (...)
				{
				}
				lock.lock();
			}
		}
		wbemService->CancelAsyncCall(sinkStub.get());
	}

	void HandleProcessCreation(IWbemClassObject* object, IWbemServices* service)
	{
		wil::unique_variant targetInstanceVariant;
		THROW_IF_FAILED(object->Get(L"TargetInstance", 0, targetInstanceVariant.addressof(), nullptr, nullptr));
		auto targetInstance = wil::com_query<IWbemClassObject>(targetInstanceVariant.punkVal);
		wil::unique_variant processIdVariant;
		wil::unique_variant executablePathVariant;
		THROW_IF_FAILED(targetInstance->Get(L"ProcessId", 0, processIdVariant.addressof(), nullptr, nullptr));
		THROW_IF_FAILED(targetInstance->Get(L"ExecutablePath", 0, executablePathVariant.addressof(), nullptr, nullptr));
		bool filteredOut = true;
		const wchar_t* executablePath = executablePathVariant.bstrVal;
		if (executablePath == nullptr)
			return;
		size_t exePathLength = wcslen(executablePath);
		{
			std::lock_guard lg(m_accessLock);
			for (auto& name : m_list)
			{
				const wchar_t* comparisonStart = executablePath + exePathLength - name.size();
				if ((comparisonStart == executablePath
						|| (comparisonStart > executablePath
							&& (*(comparisonStart - 1) == L'\\' || *(comparisonStart - 1) == L'/')))
					&& _wcsicmp(comparisonStart, name.c_str()) == 0)
				{
					filteredOut = false;
					break;
				}
			}
		}

		// check the process's owner
		if (!filteredOut)
		{
			wil::unique_variant objectPath;
			THROW_IF_FAILED(targetInstance->Get(L"__PATH", 0, objectPath.addressof(), nullptr, nullptr));
			wil::com_ptr<IWbemClassObject> outputParams;
			THROW_IF_FAILED(service->ExecMethod(
				objectPath.bstrVal,
				wil::make_bstr(L"GetOwnerSid").get(),
				0,
				nullptr,
				nullptr,
				outputParams.put(),
				nullptr));
			wil::unique_variant sidString;
			THROW_IF_FAILED(outputParams->Get(L"Sid", 0, sidString.addressof(), nullptr, nullptr));
			auto selfSid = GetCurrentProcessUserSid();
			if (wcscmp(sidString.bstrVal, selfSid.c_str()) != 0)
				filteredOut = true;
		}

		if (!filteredOut)
			InjectInspector(processIdVariant.uintVal, executablePath);
	}

	bool Is32BitProcess(uint32_t processId)
	{
		SYSTEM_INFO systemInfo;
		GetNativeSystemInfo(&systemInfo);
		if (systemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
		{
			return true;
		}

		wil::unique_handle hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
		THROW_LAST_ERROR_IF(!hProcess.is_valid());
		USHORT imageType, hostType;
		THROW_LAST_ERROR_IF(IsWow64Process2(hProcess.get(), &imageType, &hostType) == FALSE);
		if (imageType == IMAGE_FILE_MACHINE_I386)
			return true;
		return false;
	}

	void InjectInspector(uint32_t processId, const wchar_t* executablePath)
	{
		auto selfPathPtr = wil::GetModuleFileNameW();
		std::wstring dllPath = selfPathPtr.get();
		size_t lastSlash = dllPath.rfind(L'\\');
		if (lastSlash != std::wstring::npos) dllPath.erase(lastSlash);
		if (Is32BitProcess(processId))
		{
			dllPath += L"\\FontLoadInterceptor32.dll";
		}
		else
		{
			dllPath += L"\\FontLoadInterceptor64.dll";
		}
		std::wostringstream oss;
		oss << L"rundll32.exe \"" << dllPath << "\",InjectProcess " << processId;

		STARTUPINFOW startupInfo;
		wil::unique_process_information processInfo;
		RtlZeroMemory(&startupInfo, sizeof(startupInfo));
		RtlZeroMemory(&processInfo, sizeof(processInfo));

		startupInfo.cb = sizeof(startupInfo);
		auto cmdline = oss.str();
		cmdline.push_back(L'\0');

		THROW_LAST_ERROR_IF(CreateProcessW(
			nullptr,
			cmdline.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_UNICODE_ENVIRONMENT,
			nullptr,
			nullptr,
			&startupInfo,
			processInfo.addressof()
		) == FALSE);
	}
};

sfh::ProcessMonitor::ProcessMonitor(IDaemon* daemon, std::chrono::milliseconds interval)
	: m_impl(std::make_unique<Implementation>(daemon, interval))
{
}

sfh::ProcessMonitor::~ProcessMonitor() = default;

void sfh::ProcessMonitor::SetMonitorList(std::vector<std::wstring>&& list)
{
	m_impl->SetMonitorList(std::move(list));
}
