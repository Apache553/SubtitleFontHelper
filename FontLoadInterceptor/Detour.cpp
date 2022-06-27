#include "pch.h"

#include "Detour.h"
#include "RpcClient.h"

HFONT WINAPI sfh::Detour::CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                                      DWORD bItalic,
                                      DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision,
                                      DWORD iClipPrecision, DWORD iQuality,
                                      DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
	QueryAndLoad(pszFaceName);
	return Original::CreateFontA(
		cHeight,
		cWidth,
		cEscapement,
		cOrientation,
		cWeight,
		bItalic,
		bUnderline,
		bStrikeOut,
		iCharSet,
		iOutPrecision,
		iClipPrecision,
		iQuality,
		iPitchAndFamily,
		pszFaceName);
}

HFONT WINAPI sfh::Detour::CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                                      DWORD bItalic,
                                      DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision,
                                      DWORD iClipPrecision, DWORD iQuality,
                                      DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
	QueryAndLoad(pszFaceName);
	return Original::CreateFontW(
		cHeight,
		cWidth,
		cEscapement,
		cOrientation,
		cWeight,
		bItalic,
		bUnderline,
		bStrikeOut,
		iCharSet,
		iOutPrecision,
		iClipPrecision,
		iQuality,
		iPitchAndFamily,
		pszFaceName);
}

HFONT WINAPI sfh::Detour::CreateFontIndirectA(const LOGFONTA* lplf)
{
	QueryAndLoad(lplf->lfFaceName);
	return Original::CreateFontIndirectA(lplf);
}

HFONT WINAPI sfh::Detour::CreateFontIndirectW(const LOGFONTW* lplf)
{
	QueryAndLoad(lplf->lfFaceName);
	return Original::CreateFontIndirectW(lplf);
}


HFONT WINAPI sfh::Detour::CreateFontIndirectExA(const ENUMLOGFONTEXDVA* lpelf)
{
	QueryAndLoad(reinterpret_cast<const char*>(lpelf->elfEnumLogfontEx.elfFullName));
	QueryAndLoad(lpelf->elfEnumLogfontEx.elfLogFont.lfFaceName);
	return Original::CreateFontIndirectExA(lpelf);
}

HFONT WINAPI sfh::Detour::CreateFontIndirectExW(const ENUMLOGFONTEXDVW* lpelf)
{
	QueryAndLoad(lpelf->elfEnumLogfontEx.elfFullName);
	QueryAndLoad(lpelf->elfEnumLogfontEx.elfLogFont.lfFaceName);
	return Original::CreateFontIndirectExW(lpelf);
}

int WINAPI sfh::Detour::EnumFontFamiliesA(HDC hdc, LPCSTR lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam)
{
	QueryAndLoad(lpLogfont);
	return Original::EnumFontFamiliesA(hdc, lpLogfont, lpProc, lParam);
}

int WINAPI sfh::Detour::EnumFontFamiliesW(HDC hdc, LPCWSTR lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam)
{
	QueryAndLoad(lpLogfont);
	return Original::EnumFontFamiliesW(hdc, lpLogfont, lpProc, lParam);
}

int WINAPI sfh::Detour::EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam,
                                            DWORD dwFlags)
{
	QueryAndLoad(lpLogfont->lfFaceName);
	return Original::EnumFontFamiliesExA(hdc, lpLogfont, lpProc, lParam, dwFlags);
}

int WINAPI sfh::Detour::EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam,
                                            DWORD dwFlags)
{
	QueryAndLoad(lpLogfont->lfFaceName);
	return Original::EnumFontFamiliesExW(hdc, lpLogfont, lpProc, lParam, dwFlags);
}
