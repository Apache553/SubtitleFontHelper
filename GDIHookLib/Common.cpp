#include "Common.h"

#include <memory>

std::wstring GetHModulePath(HMODULE hModule)
{
	std::wstring ret;
	// get our exe path
	size_t len = 1024;
	std::unique_ptr<wchar_t[]> mod_fn(new wchar_t[len]);
	while (true) {
		len = GetModuleFileNameW(hModule, mod_fn.get(), len);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			len *= 1.5;
			if (len > 32767)break;
			delete[] mod_fn.release();
			mod_fn.reset(new wchar_t[len]);
		}
		else if (GetLastError() == ERROR_SUCCESS) {
			ret = mod_fn.get();
			break;
		}
		else {
			break;
		}
	}
	return ret;
}
