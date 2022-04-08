#pragma once

#include "pch.h"
#include "IDaemon.h"
#include "QueryService.h"

#include "FontQuery.pb.h"

namespace sfh
{
	class IRpcRequestHandler
	{
	public:
		virtual FontQueryResponse HandleRequest(const FontQueryRequest& request) = 0;
	};

	class RpcServer
	{
	private:
		class Implementation;
		std::unique_ptr<Implementation> m_impl;
	public:
		RpcServer(IDaemon* daemon, IRpcRequestHandler* handler);
		~RpcServer();

		RpcServer(const RpcServer&) = delete;
		RpcServer(RpcServer&&) = delete;

		RpcServer& operator=(const RpcServer&) = delete;
		RpcServer& operator=(RpcServer&&) = delete;
	};
}
