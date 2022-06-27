#pragma once

#include "IDaemon.h"
#include "PersistantData.h"

namespace sfh
{
	class IRpcRequestHandler;

	class QueryService
	{
	private:
		class Implementation;
		std::unique_ptr<Implementation> m_impl;
	public:
		QueryService(IDaemon* daemon);
		~QueryService();

		QueryService(const QueryService&) = delete;
		QueryService(QueryService&&) = delete;

		QueryService& operator=(const QueryService&) = delete;
		QueryService& operator=(QueryService&&) = delete;

		void Load(std::vector<std::unique_ptr<FontDatabase>>&& dbs);

		IRpcRequestHandler* GetRpcRequestHandler();
	};
}
