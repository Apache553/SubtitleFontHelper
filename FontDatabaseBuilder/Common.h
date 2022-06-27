#pragma once

#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cwctype>
#include <atomic>

extern std::atomic<bool> g_cancelToken;

#define WIN32_LEAN_AND_MEAN
#include <cassert>
#include <Windows.h>
#undef GetCurrentDirectory
#undef GetFullPathName
#undef min
#undef max
#include <wil/resource.h>
#include <wil/win32_helpers.h>

inline void ThrowIfCancelled()
{
	if (g_cancelToken)
		throw std::runtime_error("Operation cancelled");
}
