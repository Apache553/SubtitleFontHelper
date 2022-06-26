#include "pch.h"
#include "Common.h"
#include "RpcServer.h"

#define WIN32_LEAN_AND_MEAN
#include <queue>
#include <Windows.h>
#include <wil/resource.h>

class sfh::RpcServer::Implementation
{
private:
	IDaemon* m_daemon;
	IRpcRequestHandler* m_handler;

	std::thread m_listener;
	std::atomic<size_t> m_checkPoint;
	wil::unique_event m_exitEvent;


	void ProcessNewRequest(wil::unique_handle& pipe) noexcept
	{
		OVERLAPPED overlapped{};
		ResetOverlapped(&overlapped);
		try
		{
			AcceptRequest(pipe, &overlapped);
		}
		catch (...)
		{
			// ignore exceptions
		}
		FlushFileBuffers(pipe.get());
		DisconnectNamedPipe(pipe.get());
	}

	void DoWork(std::condition_variable& cv, std::mutex& lock, std::queue<wil::unique_handle>& pending, bool& stop)
	{
		std::unique_lock lg(lock);
		while (!stop)
		{
			cv.wait(lg, [&]() { return !pending.empty() || stop; });
			while (!pending.empty())
			{
				auto conn = std::move(pending.front());
				pending.pop();
				lg.unlock();

				ProcessNewRequest(conn);

				lg.lock();
			}
		}
	}


public:
	static constexpr size_t WORKER_COUNT = 4;

	Implementation(IDaemon* daemon, IRpcRequestHandler* handler)
		: m_daemon(daemon), m_handler(handler), m_checkPoint(0)
	{
		m_exitEvent.create(wil::EventOptions::ManualReset);
		m_listener = std::thread([&]()
		{
			try
			{
				std::condition_variable workerCV;
				std::mutex workerMutex;
				std::queue<wil::unique_handle> workerQueue;
				bool stop = false;
				std::vector<std::thread> workers;


				workers.reserve(WORKER_COUNT);
				for (size_t i = 0; i < WORKER_COUNT; ++i)
				{
					workers.emplace_back([&]()
					{
						DoWork(workerCV, workerMutex, workerQueue, stop);
					});
				}

				wil::unique_event connectedEvent;
				connectedEvent.create(wil::EventOptions::ManualReset);
				HANDLE waitList[2] = {
					connectedEvent.get(),
					m_exitEvent.get()
				};

				OVERLAPPED overlapped;
				overlapped.hEvent = connectedEvent.get();

				while (!m_exitEvent.is_signaled())
				{
					auto pipe = CreateNewNamedPipe();
					++m_checkPoint;
					ResetOverlapped(&overlapped);
					ConnectNamedPipe(pipe.get(), &overlapped);
					THROW_LAST_ERROR_IF(GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_PIPE_CONNECTED);
					if (GetLastError() == ERROR_PIPE_CONNECTED)
						connectedEvent.SetEvent();
					auto waitResult = WaitForMultipleObjects(
						std::extent_v<decltype(waitList)>,
						waitList,
						FALSE,
						INFINITE);
					if (waitResult == WAIT_OBJECT_0)
					{
						// connected
						std::lock_guard lg(workerMutex);
						workerQueue.push(std::move(pipe));
						workerCV.notify_one();
					}
					else if (waitResult == WAIT_OBJECT_0 + 1)
					{
						// exit
						std::unique_lock lg(workerMutex);
						stop = true;
						workerCV.notify_all();
						lg.unlock();
						for (auto& worker : workers)
						{
							worker.join();
						}
						break;
					}
					else if (waitResult == WAIT_FAILED)
					{
						THROW_LAST_ERROR();
					}
					else
					{
						MarkUnreachable();
					}
				}
			}
			catch (...)
			{
				++m_checkPoint;
				m_daemon->NotifyException(std::current_exception());
			}
		});
		while (m_checkPoint.load() == 0)
			std::this_thread::yield();
	}

	~Implementation()
	{
		m_exitEvent.SetEvent();
		if (m_listener.joinable())
			m_listener.join();
	}

private:
	static wil::unique_handle CreateNewNamedPipe()
	{
		// generate pipe name
		std::wstring pipeName = LR"_(\\.\pipe\SubtitleFontAutoLoaderRpc-)_";
		pipeName += GetCurrentProcessUserSid();

		wil::unique_handle pipe;
		*pipe.put() = CreateNamedPipeW(
			pipeName.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
			PIPE_UNLIMITED_INSTANCES,
			4096,
			4096,
			0,
			nullptr
		);
		THROW_LAST_ERROR_IF_NULL(pipe.get());
		return pipe;
	}

	void AcceptRequest(wil::unique_handle& pipe, OVERLAPPED* overlapped)
	{
		uint32_t requestLength;
		ReadPipe(pipe.get(), &requestLength, sizeof(uint32_t), overlapped);
		std::vector<char> requestBuffer(requestLength);
		ReadPipe(pipe.get(), requestBuffer.data(), requestLength, overlapped);

		FontQueryRequest request;
		if (!request.ParseFromArray(requestBuffer.data(), requestLength))
			return;

		if (request.version() != 1)
			return;

		auto response = m_handler->HandleRequest(request);

		std::ostringstream oss;
		response.SerializeToOstream(&oss);
		std::string buffer = std::move(oss).str();
		auto responseLength = static_cast<uint32_t>(buffer.size());
		WritePipe(pipe.get(), &responseLength, sizeof(uint32_t), overlapped);
		WritePipe(pipe.get(), buffer.data(), static_cast<DWORD>(responseLength), overlapped);
	}

	static void ReadPipe(HANDLE pipe, void* dst, DWORD size, OVERLAPPED* overlapped)
	{
		DWORD readCount;
		ResetOverlapped(overlapped);
		THROW_LAST_ERROR_IF(
			ReadFile(pipe, dst, size, nullptr, overlapped) == FALSE && GetLastError() != ERROR_IO_PENDING);
		THROW_LAST_ERROR_IF(GetOverlappedResult(pipe, overlapped, &readCount, TRUE) == FALSE);
		if (readCount != size)throw std::runtime_error("not enough data");
	}

	static void WritePipe(HANDLE pipe, const void* src, DWORD size, OVERLAPPED* overlapped)
	{
		DWORD writeCount;
		ResetOverlapped(overlapped);
		THROW_LAST_ERROR_IF(
			WriteFile(pipe, src, size, nullptr, overlapped) == FALSE && GetLastError() != ERROR_IO_PENDING);
		THROW_LAST_ERROR_IF(GetOverlappedResult(pipe, overlapped, &writeCount, TRUE) == FALSE);
		if (writeCount != size)throw std::runtime_error("can't write much data");
	}

	static void ResetOverlapped(OVERLAPPED* overlapped)
	{
		HANDLE hEvent = overlapped->hEvent;
		memset(overlapped, 0, sizeof(OVERLAPPED));
		if (hEvent != nullptr)
		{
			overlapped->hEvent = hEvent;
			ResetEvent(hEvent);
		}
	}
};

sfh::RpcServer::RpcServer(IDaemon* daemon, IRpcRequestHandler* handler)
	: m_impl(std::make_unique<Implementation>(daemon, handler))
{
}

sfh::RpcServer::~RpcServer() = default;
