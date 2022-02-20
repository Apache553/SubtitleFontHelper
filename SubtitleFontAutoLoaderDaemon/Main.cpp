#include "pch.h"

#include "Common.h"
#include "IDaemon.h"
#include "TrayIcon.h"
#include "PersistantData.h"
#include "QueryService.h"
#include "RpcServer.h"

#include <queue>
#include <variant>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <strsafe.h>
#include <shellapi.h>


namespace sfh
{
	class Daemon : public IDaemon
	{
	public:
		enum class MessageType
		{
			Init = 0,
			Exception,
			Exit
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

		std::unique_ptr<SystemTray> m_systemTray;
		std::unique_ptr<QueryService> m_queryService;
		std::unique_ptr<RpcServer> m_rpcServer;

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

		void OnInit(const std::vector<std::wstring>& cmdline)
		{
			auto cfg = ConfigFile::ReadFromFile(LR"_(C:\Users\Apach\AppData\Local\SubtitleFontHelper.xml)_");
			std::vector<std::unique_ptr<FontDatabase>> dbs;
			dbs.emplace_back(
				std::make_unique<FontDatabase>(FontDatabase::ReadFromFile(LR"_(E:\超级字体整合包 XZ\FontIndex.xml)_")));
			m_systemTray = std::make_unique<SystemTray>(this);
			m_queryService = std::make_unique<QueryService>(this);
			m_rpcServer = std::make_unique<RpcServer>(this, m_queryService->GetRpcRequestHandler());
			m_queryService->Load(std::move(dbs));
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
	::setlocale(LC_ALL, "");
	auto cmdline = sfh::GetCommandLineVector(lpCmdLine);
	try
	{
		return sfh::Daemon().DaemonMain(cmdline);
	}
	catch (std::exception& e)
	{
		MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
		return 1;
	}
}
