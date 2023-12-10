#pragma once

#ifdef DLL_COMPILE_TIME
#define CLASS_DECLSPEC __declspec( dllexport )
#else
#define CLASS_DECLSPEC __declspec( dllimport )
#endif