#include "pch.h"
#include "Common.h"
#include "RpcServer.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wil/resource.h>

class sfh::RpcServer::Implementation
{
private:
	IDaemon* m_daemon;
	IRpcRequestHandler* m_handler;

	std::thread m_worker;
	std::atomic<size_t> m_checkPoint;
	wil::unique_event m_exitEvent;


public:
	Implementation(IDaemon* daemon, IRpcRequestHandler* handler)
		: m_daemon(daemon), m_handler(handler), m_checkPoint(0)
	{
		m_exitEvent.create(wil::EventOptions::ManualReset);
		m_worker = std::thread([&]()
		{
			try
			{
				OVERLAPPED overlapped;
				wil::unique_event connectedEvent;
				connectedEvent.create(wil::EventOptions::ManualReset);
				HANDLE waitList[2] = {
					connectedEvent.get(),
					m_exitEvent.get()
				};
				overlapped.hEvent = connectedEvent.get();
				auto pipe = CreateNewNamedPipe();
				++m_checkPoint;
				while (!m_exitEvent.is_signaled())
				{
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
					else if (waitResult == WAIT_OBJECT_0 + 1)
					{
						// exit
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
		if (m_worker.joinable())
			m_worker.join();
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
		std::wstring request(requestLength, 0);
		ReadPipe(pipe.get(), request.data(), sizeof(wchar_t) * requestLength, overlapped);

		auto result = m_handler->HandleRequest(request);

		size_t responseSize = sizeof(uint32_t);
		for (auto& response : result)
		{
			responseSize += sizeof(uint32_t);
			responseSize += sizeof(wchar_t) * response.get().size();
		}
		std::unique_ptr<char[]> buffer = std::make_unique<char[]>(responseSize);
		char* bufferPointer = buffer.get();
		uint32_t responseCount = static_cast<uint32_t>(result.size());
		memcpy(bufferPointer, &responseCount, sizeof(uint32_t));
		bufferPointer += sizeof(uint32_t);
		for (auto& response : result)
		{
			uint32_t responseLength = static_cast<uint32_t>(response.get().size());
			memcpy(bufferPointer, &responseLength, sizeof(uint32_t));
			bufferPointer += sizeof(uint32_t);
			memcpy(bufferPointer, response.get().data(), sizeof(wchar_t) * response.get().size());
			bufferPointer += sizeof(wchar_t) * response.get().size();
		}
		WritePipe(pipe.get(), buffer.get(), static_cast<DWORD>(responseSize), overlapped);
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
		overlapped->hEvent = hEvent;
		ResetEvent(hEvent);
	}
};

sfh::RpcServer::RpcServer(IDaemon* daemon, IRpcRequestHandler* handler)
	: m_impl(std::make_unique<Implementation>(daemon, handler))
{
}

sfh::RpcServer::~RpcServer() = default;
