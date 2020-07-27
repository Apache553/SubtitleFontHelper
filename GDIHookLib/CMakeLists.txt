
cmake_minimum_required (VERSION 3.8)

project ("GDIHookLib")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)

set(IS_X86 OFF CACHE BOOL "Set it to ON if you build for i386 architecture")



set(hook_source "DllEntry.cpp" "Hook.cpp" "Common.cpp")

set(detour_include ${CMAKE_CURRENT_SOURCE_DIR}/Detours/include)

if(IS_X86)
message("Build i386 Hook dll")
set(library_name GDIHook32)
set(hook_source ${hook_source} "export32.def")
set(detour_lib ${CMAKE_CURRENT_SOURCE_DIR}/Detours/lib.X86/detours.lib)
else(IS_X86)
message("Build x86_64 Hook dll")
set(library_name GDIHook64)
set(hook_source ${hook_source} "export64.def")
set(detour_lib ${CMAKE_CURRENT_SOURCE_DIR}/Detours/lib.X64/detours.lib)
endif(IS_X86)

add_library(${library_name} SHARED ${hook_source})
target_include_directories(${library_name} PRIVATE ${detour_include})
target_link_libraries(${library_name} PRIVATE ${detour_lib})