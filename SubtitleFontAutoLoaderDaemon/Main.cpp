#include "pch.h"

#include "Common.h"
#include "IDaemon.h"
#include "TrayIcon.h"
#include "PersistantData.h"
#include "QueryService.h"
#include "RpcServer.h"
#include "ProcessMonitor.h"

#include <queue>
#include <variant>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <pathcch.h>
#include <strsafe.h>
#include <shellapi.h>
#include <wil/win32_helpers.h>

#include "event.h"

#pragma comment(lib, "pathcch.lib")

namespace sfh
{
	class SingleInstanceLock
	{
	private:
		wil::unique_mutex m_mutex;
	public:
		SingleInstanceLock()
		{
			std::wstring mutexName = LR"_(SubtitleFontAutoLoaderMutex-)_";
			mutexName += GetCurrentProcessUserSid();
			m_mutex.create(mutexName.c_str());
			if (WaitForSingleObject(m_mutex.get(), 0) != WAIT_OBJECT_0)
				throw std::runtime_error("Another instance is running!");
		}

		~SingleInstanceLock()
		{
			m_mutex.ReleaseMutex();
		}
	};

	class Daemon : public IDaemon
	{
	private:
		enum class MessageType
		{
			Init = 0,
			Exception,
			Exit,
			Reload
		};

		struct Message
		{
			MessageType m_type;
			std::variant<
				std::nullopt_t,
				const std::vector<std::wstring>*,
				std::exception_ptr> m_arg;
		};

		std::mutex m_queueLock;
		std::condition_variable m_queueCV;
		std::queue<Message> m_msgQueue;

		struct Service
		{
			std::unique_ptr<SystemTray> m_systemTray;
			std::unique_ptr<QueryService> m_queryService;
			std::unique_ptr<RpcServer> m_rpcServer;
			std::unique_ptr<ProcessMonitor> m_processMonitor;
		};

		std::unique_ptr<Service> m_service;

		void NotifyException(std::exception_ptr exception) override
		{
			std::unique_lock ul(m_queueLock);
			m_msgQueue.emplace(MessageType::Exception, exception);
			m_queueCV.notify_one();
		}

		void NotifyExit() override
		{
			std::unique_lock ul(m_queueLock);
			m_msgQueue.emplace(MessageType::Exit, std::nullopt);
			m_queueCV.notify_one();
		}

		void NotifyReload() override
		{
			std::unique_lock ul(m_queueLock);
			m_msgQueue.emplace(MessageType::Reload, std::nullopt);
			m_queueCV.notify_one();
		}

	public:
		int DaemonMain(const std::vector<std::wstring>& cmdline)
		{
			std::unique_lock ul(m_queueLock);
			m_msgQueue.emplace(MessageType::Init, &cmdline);
			while (!m_msgQueue.empty())
			{
				auto [msgType, msgArg] = m_msgQueue.front();
				m_msgQueue.pop();
				ul.unlock();
				switch (msgType)
				{
				case MessageType::Init:
					OnInit(*std::get<const std::vector<std::wstring>*>(msgArg));
					break;
				case MessageType::Exception:
					OnException(std::get<std::exception_ptr>(msgArg));
					break;
				case MessageType::Exit:
					return 0;
				case MessageType::Reload:
					OnInit(cmdline);
					break;
				default:
					MarkUnreachable();
				}
				ul.lock();
				if (m_msgQueue.empty())
				{
					m_queueCV.wait(ul, [&]()
					{
						return !m_msgQueue.empty();
					});
				}
			}
			MarkUnreachable();
		}

	private:
		void OnInit(const std::vector<std::wstring>& cmdline)
		{
			{
				std::unique_lock lg(m_queueLock);
				while (!m_msgQueue.empty())
					m_msgQueue.pop();
			}
			m_service = std::make_unique<Service>();
			auto selfPathPtr = wil::GetModuleFileNameW<wil::unique_hlocal_string>();
			PathCchRemoveFileSpec(selfPathPtr.get(), wcslen(selfPathPtr.get()) + 1);
			std::wstring configPath = selfPathPtr.get();
			configPath += L"\\SubtitleFontHelper.xml";
			auto cfg = ConfigFile::ReadFromFile(configPath);

			m_service->m_systemTray = std::make_unique<SystemTray>(this);
			std::vector<std::unique_ptr<FontDatabase>> dbs;
			for (auto& indexFile : cfg->m_indexFile)
			{
				dbs.emplace_back(FontDatabase::ReadFromFile(indexFile.m_path));
			}
			m_service->m_queryService = std::make_unique<QueryService>(this);
			m_service->m_rpcServer = std::make_unique<RpcServer>(
				this, m_service->m_queryService->GetRpcRequestHandler());
			m_service->m_queryService->Load(std::move(dbs));
			m_service->m_processMonitor = std::make_unique<ProcessMonitor>(
				this, std::chrono::milliseconds(cfg->wmiPollInterval));
			std::vector<std::wstring> monitorProcess;
			for (auto& process : cfg->m_monitorProcess)
			{
				monitorProcess.emplace_back(process.m_name);
			}
			m_service->m_processMonitor->SetMonitorList(std::move(monitorProcess));
			m_service->m_systemTray->NotifyFinishLoad();
		}

		void OnException(std::exception_ptr exception)
		{
			std::rethrow_exception(exception);
		}
	};

	std::vector<std::wstring> GetCommandLineVector(const wchar_t* cmdline)
	{
		std::vector<std::wstring> ret;
		int argc = 0;
		wchar_t** argv = CommandLineToArgvW(cmdline, &argc);
		for (int i = 0; i < argc; ++i)
		{
			ret.emplace_back(argv[i]);
		}
		return ret;
	}
}

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
	// initialize locale for ACP
	setlocale(LC_ALL, "");
	SetEnvironmentVariableW(L"__NO_DETOUR", L"TRUE");
	auto cmdline = sfh::GetCommandLineVector(lpCmdLine);
	try
	{
		sfh::SingleInstanceLock lock;
		return sfh::Daemon().DaemonMain(cmdline);
	}
	catch (std::exception& e)
	{
		MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
		return 1;
	}
}
