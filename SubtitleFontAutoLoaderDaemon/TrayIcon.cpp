#include "pch.h"

#include "TrayIcon.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <wil/win32_helpers.h>

class sfh::SystemTray::Implementation
{
private:
	NOTIFYICONDATAW m_iconData = {};
	HWND m_hWnd = nullptr;
	std::thread m_trayThread;

	IDaemon* m_daemon;
public:
	Implementation(IDaemon* daemon)
		: m_daemon(daemon)
	{
		std::atomic<int> barrier = 0;
		m_trayThread = std::thread([&]()
		{
			SetupMessageWindow();
			++barrier;
			MessageLoop();
		});
		while (barrier.load() == 0)
			std::this_thread::yield();
	}

	~Implementation()
	{
		if (m_trayThread.joinable())
		{
			PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
			m_trayThread.join();
		}
	}

private:
	constexpr static UINT WM_TRAY_ICON_MESSAGE = WM_USER;

	void SetupMessageWindow()
	{
		WNDCLASSW wndClass;
		RtlZeroMemory(&wndClass, sizeof(wndClass));
		wndClass.lpfnWndProc = WindowProc;
		wndClass.hInstance = wil::GetModuleInstanceHandle();
		wndClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		wndClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wndClass.lpszMenuName = MAKEINTRESOURCEW(IDR_TRAYMENU);
		wndClass.lpszClassName = L"AutoLoaderDaemonTray";

		RegisterClassW(&wndClass);

		CreateWindowExW(
			0,
			wndClass.lpszClassName,
			L"AutoLoaderDaemonTray",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			nullptr,
			nullptr,
			wndClass.hInstance,
			this
		);

		assert(m_hWnd);
	}

	void SetupTrayIcon()
	{
		RtlZeroMemory(&m_iconData, sizeof(m_iconData));
		m_iconData.cbSize = sizeof(m_iconData);
		m_iconData.hWnd = m_hWnd;
		m_iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		m_iconData.uCallbackMessage = WM_TRAY_ICON_MESSAGE;
		m_iconData.hIcon = LoadIconW(wil::GetModuleInstanceHandle(), MAKEINTRESOURCEW(IDI_TRAYICON));
		wcscpy_s(m_iconData.szTip, L"SubtitleFontAutoLoaderDaemon");
		Shell_NotifyIconW(NIM_ADD, &m_iconData);
	}

	void DestroyTrayIcon()
	{
		Shell_NotifyIconW(NIM_DELETE, &m_iconData);
	}

	static void MessageLoop()
	{
		MSG msg = {};
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	LRESULT CALLBACK MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		const UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
		switch (uMsg)
		{
		case WM_CREATE:
			SetupTrayIcon();
			break;
		case WM_CLOSE:
			DestroyTrayIcon();
			DestroyWindow(hWnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_TRAY_ICON_MESSAGE:
			if (lParam == WM_RBUTTONUP)
			{
				ShowContextMenu(hWnd, uMsg, wParam, lParam);
			}
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case ID_TRAYICONMENU_EXIT:
				m_daemon->NotifyExit();
				break;
			}
			return 0;
		default:
			if (uMsg == WM_TASKBARCREATED)
			{
				SetupTrayIcon();
			}
		}
		return DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

	static void ShowContextMenu(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		POINT cursorPos;

		GetCursorPos(&cursorPos);
		SetForegroundWindow(hWnd);
		HMENU hMenu = LoadMenuW(wil::GetModuleInstanceHandle(), MAKEINTRESOURCEW(IDR_TRAYMENU));
		HMENU hMenu1 = GetSubMenu(hMenu, 0);
		TrackPopupMenuEx(hMenu1, TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursorPos.x, cursorPos.y, hWnd, nullptr);
		DestroyMenu(hMenu);
	}

	static Implementation* GetThisByWindow(HWND hWnd)
	{
		return reinterpret_cast<Implementation*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
	}

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		assert(hWnd);
		if (uMsg == WM_CREATE)
		{
			auto pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
			auto that = reinterpret_cast<Implementation*>(pCreate->lpCreateParams);
			that->m_hWnd = hWnd;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));
		}
		if (auto that = GetThisByWindow(hWnd))
		{
			return that->MessageHandler(hWnd, uMsg, wParam, lParam);
		}

		return DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}
};

sfh::SystemTray::SystemTray(IDaemon* daemon)
	: m_impl(std::make_unique<Implementation>(daemon))
{
}

sfh::SystemTray::~SystemTray() = default;
