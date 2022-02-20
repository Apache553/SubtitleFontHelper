#pragma once

#include <exception>

namespace sfh
{
	class IDaemon
	{
	public:
		virtual ~IDaemon() = default;
		virtual void NotifyException(std::exception_ptr exception) = 0;
		virtual void NotifyExit() = 0;
	};
}