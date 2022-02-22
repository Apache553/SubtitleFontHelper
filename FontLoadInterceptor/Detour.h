#pragma once

#include "pch.h"

namespace sfh
{
	namespace Detour
	{
		HFONT WINAPI CreateFontA(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation,
		                         _In_ int cWeight, _In_ DWORD bItalic,
		                         _In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
		                         _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
		                         _In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCSTR pszFaceName);
		HFONT WINAPI CreateFontW(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation,
		                         _In_ int cWeight, _In_ DWORD bItalic,
		                         _In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
		                         _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
		                         _In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCWSTR pszFaceName);

		HFONT WINAPI CreateFontIndirectA(_In_ CONST LOGFONTA* lplf);
		HFONT WINAPI CreateFontIndirectW(_In_ CONST LOGFONTW* lplf);

		HFONT WINAPI CreateFontIndirectExA(_In_ CONST ENUMLOGFONTEXDVA*);
		HFONT WINAPI CreateFontIndirectExW(_In_ CONST ENUMLOGFONTEXDVW*);

		int WINAPI EnumFontFamiliesA(_In_ HDC hdc, _In_opt_ LPCSTR lpLogfont, _In_ FONTENUMPROCA lpProc,
		                             _In_ LPARAM lParam);
		int WINAPI EnumFontFamiliesW(_In_ HDC hdc, _In_opt_ LPCWSTR lpLogfont, _In_ FONTENUMPROCW lpProc,
		                             _In_ LPARAM lParam);

		int WINAPI EnumFontFamiliesExA(_In_ HDC hdc, _In_ LPLOGFONTA lpLogfont, _In_ FONTENUMPROCA lpProc,
		                               _In_ LPARAM lParam, _In_ DWORD dwFlags);
		int WINAPI EnumFontFamiliesExW(_In_ HDC hdc, _In_ LPLOGFONTW lpLogfont, _In_ FONTENUMPROCW lpProc,
		                               _In_ LPARAM lParam, _In_ DWORD dwFlags);

		namespace Original
		{
			extern HFONT (WINAPI* CreateFontA)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement,
			                                   _In_ int cOrientation,
			                                   _In_ int cWeight, _In_ DWORD bItalic,
			                                   _In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
			                                   _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
			                                   _In_ DWORD iQuality, _In_ DWORD iPitchAndFamily,
			                                   _In_opt_ LPCSTR pszFaceName);
			extern HFONT (WINAPI* CreateFontW)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement,
			                                   _In_ int cOrientation,
			                                   _In_ int cWeight, _In_ DWORD bItalic,
			                                   _In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet,
			                                   _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
			                                   _In_ DWORD iQuality, _In_ DWORD iPitchAndFamily,
			                                   _In_opt_ LPCWSTR pszFaceName);

			extern HFONT (WINAPI* CreateFontIndirectA)(_In_ CONST LOGFONTA* lplf);
			extern HFONT (WINAPI* CreateFontIndirectW)(_In_ CONST LOGFONTW* lplf);

			extern HFONT (WINAPI* CreateFontIndirectExA)(_In_ CONST ENUMLOGFONTEXDVA*);
			extern HFONT (WINAPI* CreateFontIndirectExW)(_In_ CONST ENUMLOGFONTEXDVW*);

			extern int (WINAPI* EnumFontFamiliesA)(_In_ HDC hdc, _In_opt_ LPCSTR lpLogfont, _In_ FONTENUMPROCA lpProc,
			                                       _In_ LPARAM lParam);
			extern int (WINAPI* EnumFontFamiliesW)(_In_ HDC hdc, _In_opt_ LPCWSTR lpLogfont, _In_ FONTENUMPROCW lpProc,
			                                       _In_ LPARAM lParam);

			extern int (WINAPI* EnumFontFamiliesExA)(_In_ HDC hdc, _In_ LPLOGFONTA lpLogfont, _In_ FONTENUMPROCA lpProc,
			                                         _In_ LPARAM lParam, _In_ DWORD dwFlags);
			extern int (WINAPI* EnumFontFamiliesExW)(_In_ HDC hdc, _In_ LPLOGFONTW lpLogfont, _In_ FONTENUMPROCW lpProc,
			                                         _In_ LPARAM lParam, _In_ DWORD dwFlags);
		}
	}
}
