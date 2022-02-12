
#include <Windows.h>

extern HFONT(WINAPI* True_CreateFontIndirectA)(_In_ CONST LOGFONTA* lplf);
extern HFONT(WINAPI* True_CreateFontIndirectW)(_In_ CONST LOGFONTW* lplf);
extern HFONT(WINAPI* True_CreateFontA)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation, _In_ int cWeight, _In_ DWORD bItalic,
	_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet, _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
	_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCSTR pszFaceName);
extern HFONT(WINAPI* True_CreateFontW)(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation, _In_ int cWeight, _In_ DWORD bItalic,
	_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet, _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
	_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCWSTR pszFaceName);
extern HFONT(WINAPI* True_CreateFontIndirectExA)(_In_ CONST ENUMLOGFONTEXDVA*);
extern HFONT(WINAPI* True_CreateFontIndirectExW)(_In_ CONST ENUMLOGFONTEXDVW*);
extern int(WINAPI* True_EnumFontFamiliesW)(_In_ HDC hdc, _In_opt_ LPCWSTR lpLogfont, _In_ FONTENUMPROCW lpProc, _In_ LPARAM lParam);

extern "C" {

	HFONT WINAPI HookedCreateFontIndirectW(_In_ CONST LOGFONTW* lplf);
	HFONT WINAPI HookedCreateFontIndirectA(_In_ CONST LOGFONTA* lplf);

	HFONT WINAPI HookedCreateFontA(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation, _In_ int cWeight, _In_ DWORD bItalic,
		_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet, _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
		_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCSTR pszFaceName);
	HFONT WINAPI HookedCreateFontW(_In_ int cHeight, _In_ int cWidth, _In_ int cEscapement, _In_ int cOrientation, _In_ int cWeight, _In_ DWORD bItalic,
		_In_ DWORD bUnderline, _In_ DWORD bStrikeOut, _In_ DWORD iCharSet, _In_ DWORD iOutPrecision, _In_ DWORD iClipPrecision,
		_In_ DWORD iQuality, _In_ DWORD iPitchAndFamily, _In_opt_ LPCWSTR pszFaceName);

	HFONT WINAPI HookedCreateFontIndirectExA(_In_ CONST ENUMLOGFONTEXDVA*);
	HFONT WINAPI HookedCreateFontIndirectExW(_In_ CONST ENUMLOGFONTEXDVW*);

    int   WINAPI HookedEnumFontFamiliesW(_In_ HDC hdc, _In_opt_ LPCWSTR lpLogfont, _In_ FONTENUMPROCW lpProc, _In_ LPARAM lParam);

}

void InjectNotification(bool status);