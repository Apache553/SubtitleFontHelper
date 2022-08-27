#include "pch.h"
#include "Common.h"
#include "RpcServer.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wil/resource.h>

#include <queue>
#include <vector>
#include <cstdint>
#include <list>

class sfh::RpcServer::Implementation
{
private:
	IDaemon* m_daemon;
	IRpcRequestHandler* m_requestHandler;
	IRpcFeedbackHandler* m_feedbackHandler;

	std::thread m_listener;
	std::atomic<size_t> m_checkPoint;
	wil::unique_event m_exitEvent;

	std::vector<std::thread> m_workers;

	wil::unique_handle m_iocp;

	struct ConnectionBlock;

	typedef bool (Implementation::*IoCallback)(ConnectionBlock& connection, DWORD transferredBytes, DWORD error);

	struct IOBlock
	{
		uint8_t* m_buffer;
		DWORD m_completedBytes;
		DWORD m_totalBytes;
		OVERLAPPED m_overlapped = {};

		IoCallback m_completionCallback;
	};

	struct RawMessageBlock
	{
		uint32_t m_length;
		std::vector<uint8_t> m_buffer;
	};


	struct ConnectionBlock
	{
		wil::unique_hfile m_pipe;
		IOBlock m_io;
		RawMessageBlock m_msg;
		std::list<ConnectionBlock>::iterator m_iterator;
	};

	std::mutex m_connectionMutex;
	std::list<ConnectionBlock> m_connections;


	bool BeginReadLengthPrefix(ConnectionBlock& connection)
	{
		connection.m_io.m_buffer = reinterpret_cast<uint8_t*>(&connection.m_msg.m_length);
		connection.m_io.m_totalBytes = sizeof(uint32_t);
		connection.m_io.m_completedBytes = 0;
		connection.m_io.m_completionCallback = &Implementation::EndReadLengthPrefix;

		return DoRead(connection);
	}

	bool EndReadLengthPrefix(ConnectionBlock& connection, DWORD transferredBytes, DWORD error)
	{
		if (error != ERROR_SUCCESS)
			return false;
		connection.m_io.m_completedBytes += transferredBytes;
		if (connection.m_io.m_completedBytes != connection.m_io.m_totalBytes)
			return false;

		return BeginReadMessage(connection);
	}

	bool BeginReadMessage(ConnectionBlock& connection)
	{
		// sanity check
		if (connection.m_msg.m_length > 4 * 1024 * 1024)
			return false;

		connection.m_msg.m_buffer.resize(connection.m_msg.m_length);
		connection.m_io.m_buffer = connection.m_msg.m_buffer.data();
		connection.m_io.m_totalBytes = connection.m_msg.m_length;
		connection.m_io.m_completedBytes = 0;
		connection.m_io.m_completionCallback = &Implementation::EndReadMessage;

		return DoRead(connection);
	}

	bool EndReadMessage(ConnectionBlock& connection, DWORD transferredBytes, DWORD error)
	{
		if (error != ERROR_SUCCESS)
			return false;
		connection.m_io.m_completedBytes += transferredBytes;
		if (connection.m_io.m_completedBytes != connection.m_io.m_totalBytes)
			return false;

		return ProcessMessage(connection);
	}

	bool ProcessMessage(ConnectionBlock& connection)
	{
		FontQueryRequest request;
		if (!request.ParseFromArray(connection.m_msg.m_buffer.data(), connection.m_msg.m_length))
			return false;

		if (request.version() != 1)
			return false;

		if (request.has_feedbackdata())
		{
			// handle feedback
			return ProcessFeedback(connection, request);
		}
		else if (request.has_querystring())
		{
			// handle query
			return ProcessRequest(connection, request);
		}
		else
		{
			return false;
		}
	}

	bool BeginWriteLengthPrefix(ConnectionBlock& connection)
	{
		connection.m_io.m_buffer = reinterpret_cast<uint8_t*>(&connection.m_msg.m_length);
		connection.m_io.m_totalBytes = sizeof(uint32_t);
		connection.m_io.m_completedBytes = 0;
		connection.m_io.m_completionCallback = &Implementation::EndWriteLengthPrefix;

		return DoWrite(connection);
	}

	bool EndWriteLengthPrefix(ConnectionBlock& connection, DWORD transferredBytes, DWORD error)
	{
		if (error != ERROR_SUCCESS)
			return false;
		connection.m_io.m_completedBytes += transferredBytes;
		if (connection.m_io.m_completedBytes != connection.m_io.m_totalBytes)
			return false;

		return BeginWriteMessage(connection);
	}

	bool BeginWriteMessage(ConnectionBlock& connection)
	{
		connection.m_io.m_buffer = connection.m_msg.m_buffer.data();
		connection.m_io.m_totalBytes = connection.m_msg.m_length;
		connection.m_io.m_completedBytes = 0;
		connection.m_io.m_completionCallback = &Implementation::EndWriteMessage;

		return DoWrite(connection);
	}

	bool EndWriteMessage(ConnectionBlock& connection, DWORD transferredBytes, DWORD error)
	{
		if (error != ERROR_SUCCESS)
			return false;
		connection.m_io.m_completedBytes += transferredBytes;
		if (connection.m_io.m_completedBytes != connection.m_io.m_totalBytes)
			return false;

		return BeginReadLengthPrefix(connection);
	}

	bool BeginConnection(ConnectionBlock& connection)
	{
		return BeginReadLengthPrefix(connection);
	}

	template <typename IoFn>
	bool DoIo(ConnectionBlock& connection, IoFn& IoFunction) noexcept
	{
		// return true indicates connection still can do something
		memset(&connection.m_io.m_overlapped, 0, sizeof(OVERLAPPED));
		BOOL result = IoFunction(connection.m_pipe.get(), connection.m_io.m_buffer, connection.m_io.m_totalBytes,
		                         nullptr, &connection.m_io.m_overlapped);

		if (result != FALSE || GetLastError() == ERROR_IO_PENDING)
		{
			return true;
		}
		return (this->*connection.m_io.m_completionCallback)(connection, 0, GetLastError());
	}

	bool DoRead(ConnectionBlock& connection)
	{
		return DoIo(connection, ReadFile);
	}

	bool DoWrite(ConnectionBlock& connection)
	{
		return DoIo(connection, WriteFile);
	}

	void ListenProcedure()
	{
		wil::unique_event connectedEvent;
		connectedEvent.create(wil::EventOptions::ManualReset);

		HANDLE waitList[] = {m_exitEvent.get(), connectedEvent.get()};

		while (true)
		{
			OVERLAPPED connectOverlapped = {};
			connectedEvent.ResetEvent();
			connectOverlapped.hEvent = connectedEvent.get();
			auto listenPipe = CreateNewNamedPipe();

			++m_checkPoint;

			bool connected = ConnectNamedPipe(listenPipe.get(), &connectOverlapped)
				                 ? true
				                 : GetLastError() == ERROR_PIPE_CONNECTED;
			if (!connected)
			{
				if (GetLastError() == ERROR_IO_PENDING)
				{
					DWORD result = WaitForMultipleObjects(std::extent_v<decltype(waitList)>, waitList, FALSE, INFINITE);
					if (result == WAIT_OBJECT_0 + 1)
					{
						// connected
					}
					else if (result == WAIT_OBJECT_0)
					{
						// exiting
						break;
					}
					else
					{
						// generic failure
						continue;
					}
				}
				else
				{
					// generic failure
					continue;
				}
			}
			else
			{
				if (m_exitEvent.is_signaled())
					break;
			}

			// bind iocp
			std::lock_guard lg(m_connectionMutex);
			auto& connection = m_connections.emplace_front();
			connection.m_iterator = m_connections.begin();
			connection.m_pipe = std::move(listenPipe);
			if (CreateIoCompletionPort(connection.m_pipe.get(), m_iocp.get(),
			                           reinterpret_cast<ULONG_PTR>(&m_connections.front()), 0) == nullptr)
			{
				// bind failure
				continue;
			}

			if (!BeginConnection(m_connections.front()))
			{
				m_connections.pop_front();
			}
		}
	}

	void IocpRoutine()
	{
		DWORD transferredBytes;
		ULONG_PTR completionKey;
		OVERLAPPED* overlapped;
		DWORD lastError = ERROR_SUCCESS;
		while (true)
		{
			BOOL status = GetQueuedCompletionStatus(m_iocp.get(), &transferredBytes, &completionKey, &overlapped,
			                                        INFINITE);
			if (status == FALSE)
			{
				if (overlapped == nullptr)
				{
					// get completion packet failure
					THROW_LAST_ERROR_MSG("Failed to get queued completion packet!");
				}
				else
				{
					// io failure
					lastError = GetLastError();
				}
			}
			else
			{
				lastError = ERROR_SUCCESS;
			}

			if (overlapped == nullptr)
			{
				// control message
				if (completionKey == 0)
				{
					// stop thread
					break;
				}
				continue;
			}

			auto& connection = *reinterpret_cast<ConnectionBlock*>(completionKey);
			if (!(this->*connection.m_io.m_completionCallback)(connection, transferredBytes, lastError))
			{
				// destroy connection
				std::lock_guard lg(m_connectionMutex);
				m_connections.erase(connection.m_iterator);
			}
		}
	}

	template <typename T>
	static void EncodeMessage(ConnectionBlock& connection, const T& message)
	{
		std::ostringstream oss;
		message.SerializeToOstream(&oss);
		std::string buffer = std::move(oss).str();

		connection.m_msg.m_length = static_cast<uint32_t>(buffer.size());
		connection.m_msg.m_buffer.resize(buffer.size());
		memcpy(connection.m_msg.m_buffer.data(), buffer.data(), buffer.size());
	}

	bool ProcessRequest(ConnectionBlock& connection, const FontQueryRequest& request)
	{
		auto response = m_requestHandler->HandleRequest(request);
		EncodeMessage(connection, response);
		return BeginWriteLengthPrefix(connection);
	}

	bool ProcessFeedback(ConnectionBlock& connection, const FontQueryRequest& request)
	{
		m_feedbackHandler->HandleFeedback(request);
		return BeginReadLengthPrefix(connection);
	}

public:
	static constexpr size_t WORKER_COUNT = 4;

	Implementation(IDaemon* daemon, IRpcRequestHandler* requestHandler, IRpcFeedbackHandler* feedbackHandler)
		: m_daemon(daemon), m_requestHandler(requestHandler), m_feedbackHandler(feedbackHandler), m_checkPoint(0)
	{
		m_exitEvent.create(wil::EventOptions::ManualReset);
		m_iocp.reset(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0));
		if (!m_iocp.is_valid())
		{
			THROW_LAST_ERROR_MSG("Failed to create rpc I/O completion port");
		}

		// do start listening here

		SYSTEM_INFO info;
		GetSystemInfo(&info);
		int workerCount = info.dwNumberOfProcessors;
		if (workerCount > 8)
			workerCount = 8;

		m_workers.reserve(workerCount);
		for (size_t i = 0; i < workerCount; ++i)
		{
			m_workers.emplace_back([this]()
			{
				try
				{
					IocpRoutine();
				}
				catch (...)
				{
					m_daemon->NotifyException(std::current_exception());
				}
			});
		}

		m_listener = std::thread([this]()
		{
			try
			{
				ListenProcedure();
			}
			catch (...)
			{
				m_daemon->NotifyException(std::current_exception());
			}
		});

		while (m_checkPoint.load() == 0)
			std::this_thread::yield();
	}

	~Implementation()
	{
		m_exitEvent.SetEvent();
		{
			std::lock_guard lg(m_connectionMutex);
			for (auto& connection : m_connections)
			{
				CancelIoEx(connection.m_pipe.get(), nullptr);
			}
		}
		for (size_t i = 0; i < m_workers.size(); ++i)
		{
			PostQueuedCompletionStatus(m_iocp.get(), 0, 0, nullptr);
		}
		for (auto& worker : m_workers)
		{
			if (worker.joinable())
				worker.join();
		}
		if (m_listener.joinable())
			m_listener.join();
	}

private:
	static wil::unique_hfile CreateNewNamedPipe()
	{
		// generate pipe name
		std::wstring pipeName = LR"_(\\.\pipe\SubtitleFontAutoLoaderRpc-)_";
		pipeName += GetCurrentProcessUserSid();

		wil::unique_hfile pipe;
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
		if (!pipe.is_valid())
			THROW_LAST_ERROR_MSG("Failed to listen named pipe!");
		return pipe;
	}
};

sfh::RpcServer::RpcServer(IDaemon* daemon, IRpcRequestHandler* requestHandler, IRpcFeedbackHandler* feedbackHandler)
	: m_impl(std::make_unique<Implementation>(daemon, requestHandler, feedbackHandler))
{
}

sfh::RpcServer::~RpcServer() = default;
