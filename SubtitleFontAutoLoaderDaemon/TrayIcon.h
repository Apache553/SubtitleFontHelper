#pragma once

#include "pch.h"

#include "IDaemon.h"

namespace sfh
{
	class SystemTray
	{
	private:
		class Implementation;
		std::unique_ptr<Implementation> m_impl;
	public:
		SystemTray(IDaemon* daemon);
		~SystemTray();

		SystemTray(const SystemTray&) = delete;
		SystemTray(SystemTray&&) = delete;

		SystemTray& operator=(const SystemTray&) = delete;
		SystemTray& operator=(SystemTray&&) = delete;

		void NotifyFinishLoad();
	};
}
