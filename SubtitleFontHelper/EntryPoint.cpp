
#pragma warning(disable: 26812 6001)

#include <QApplication>
#include <QMessageBox>

#include <QXmlStreamReader>

#include "Common.h"
#include "MainWindow.h"
#include "CommandInterface.h"

#include <Windows.h>
#include <io.h>
#include <Fcntl.h>

#include <codecvt>
#include <string>
#include <vector>
#include <cstdio>

static bool no_messagebox = false;

int main(int argc, char** argv);

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	// init COM
	if (CoInitializeEx(NULL, COINITBASE_MULTITHREADED) != S_OK) {
		MessageBoxW(NULL, L"CoInitialize failed.", L"Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	HRESULT hr = CoInitializeSecurity(
		NULL,
		-1,                          // COM negotiates service
		NULL,                        // Authentication services
		NULL,                        // Reserved
		RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication 
		RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
		NULL,                        // Authentication info
		EOAC_NONE,                   // Additional capabilities 
		NULL                         // Reserved
	);
	if (hr != S_OK) {
		MessageBoxW(NULL, L"CoInitializeSecurity failed", L"Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	// prepare arguments
	int argc = 0;
	wchar_t** argv_w = CommandLineToArgvW(lpCmdLine, &argc);

	std::vector<std::string> arg_vec;
	std::vector<char*> argptr_vec;

	for (int i = 0; i < argc; ++i) {
		arg_vec.emplace_back(StdWStringToUTF8(argv_w[i]));
	}
	for (size_t i = 0; i < arg_vec.size(); ++i) {
		argptr_vec.push_back(const_cast<char*>(arg_vec[i].c_str()));
	}

	int ret = main(argc, argptr_vec.data());
	CoUninitialize();
	return ret;
}

const wchar_t usage[] = L" \
Usage: executable [-quiet] -nogui <target> [options...]\n \
Available Targets&Options:\n \
    0. #common#\n \
        -config <filename>\n \
            this option can be used with any target, specify a config file to use(utf8 no BOM)\n \
        -nodep\n \
            this option can be used with any target, ignore all config files\n \
        -verbose\n \
            this option can be used with any target, cause more output\n \
    1. ass\n \
        -file <filename>\n \
            specify the file will be processed\n \
        -list\n \
            list fonts referenced by file\n \
        -missing\n \
            list missing fonts referenced by file\n \
        -output <filename>\n \
            write the list to a file(utf8 no BOM) instead of showing a dialog/being printed in console\n \
        -export <filename>\n \
            save all referenced fonts into a zip file, names of missing fonts will be written into 'missing.txt'(utf8 no bom)\n \
        -index <filename>\n \
            specify extra index file used for font lookup(this option can be used multiple times)\n \
        -load\n \
            load all accessible fonts referenced by file via calling AddFontResource.\n \
            loaded fonts will be available until next boot.\n \
            inaccessible fonts will be ignored.\n \
    2. index\n \
        -dir <path>\n \
            specify the directory will be processed\n \
        -output <filename>\n \
            scan all fonts in directory, read metadata then write to a file for further usage\n \
        -ext <extension>\n \
            set extension names for search (overrides defaults[ttf,ttc,otf], can use multiple times)\n \
        -nocon\n \
            do not show console window\n \
        -writeconf\n \
            add the generated index into config for auto loading\n \
    3. daemon\n \
        running as a daemon to provide font path query service\n \
        -index <filename>\n \
            specify extra index file used for font lookup(this option can be used multiple times)\n \
        -nocon\n \
            do not show console window\n \
        -procmon\n \
            monitor process creation event to do inject hook\n \
        -process <executable name>\n \
            specify process executable name to be monitored(this option can be used multiple times)\n \
        -pollinterval <interval>\n \
            a float point value indicates the interval between wmi event polls,must greater than 0\n \
    4. config\n \
        -edit\n \
            open current config file with system text editor, if '-nodep' is present, nothing will happen\n \
        -del <entry_type>\n \
            delete an entry, no effect if nonexist\n \
        -add <entry_type>\n \
            add a new entry, no effect if exist\n \
            <entry_type> can be:\n \
                index - index file\n \
                process - process to be monitored\n \
        -value <value>\n \
            the value of the entry\n \
    5. help(or no target)\n \
        show this text\n \
";

int CmdMain(int argc, char** argv, int begin_index) {
	if (begin_index >= argc) {
		MessageBoxW(NULL, usage, L"Usage", MB_OK);
		return 0;
	}
	for (int i = begin_index; i < argc; ++i) {
		if (strcmp(argv[i], "ass") == 0) {
			CommandLineArgCollector collector({
				{"-file",true,false,true},
				{"-list",false,false},
				{"-missing",false,false},
				{"-output",true,false},
				{"-export",true,false},
				{"-load",false,false},
				{"-nocon",false,false},
				{"-index",true,true},
				{"-config",true,false},
				{"-nodep",false,false},
				{"-verbose",false,false}
				});
			try {
				collector.CollectArg(argc, argv, i + 1);
				return DoCmdASS(collector);
			}
			catch (std::exception e) {
				if (!no_messagebox)MessageBoxW(NULL, UTF8ToStdWString(e.what()).c_str(), L"Error", MB_OK | MB_ICONERROR);
				return -1;
			}
		}
		else if (strcmp(argv[i], "index") == 0) {
			CommandLineArgCollector collector({
				{"-dir",true,false,true},
				{"-output",true,false,true},
				{"-ext",true,true},
				{"-nocon",false,false},
				{"-config",true,false},
				{"-writeconf",false,false},
				{"-nodep",false,false},
				{"-verbose",false,false}
				});
			try {
				collector.CollectArg(argc, argv, i + 1);
				return DoCmdIndex(collector);
			}
			catch (std::exception e) {
				if (!no_messagebox)MessageBoxW(NULL, UTF8ToStdWString(e.what()).c_str(), L"Error", MB_OK | MB_ICONERROR);
				return -1;
			}
		}
		else if (strcmp(argv[i], "daemon") == 0) {
			CommandLineArgCollector collector({
				{"-index",true,true},
				{"-procmon",false,false},
				{"-process",true,true},
				{"-pollinterval",true,false},
				{"-nocon",false,false},
				{"-config",true,false},
				{"-nodep",false,false},
				{"-verbose",false,false}
				});
			try {
				collector.CollectArg(argc, argv, i + 1);
				return DoCmdDaemon(collector);
			}
			catch (std::exception e) {
				if (!no_messagebox)MessageBoxW(NULL, UTF8ToStdWString(e.what()).c_str(), L"Error", MB_OK | MB_ICONERROR);
				return -1;
			}
		}
		else if (strcmp(argv[i], "config") == 0) {
			CommandLineArgCollector collector({
				{"-edit",false,false},
				{"-add",true,false},
				{"-del",true,false},
				{"-value",true,false},
				{"-config",true,false},
				{"-nodep",false,false},
				{"-verbose",false,false}
				});
			try {
				collector.CollectArg(argc, argv, i + 1);
				return DoCmdConfig(collector);
			}
			catch (std::exception e) {
				if (!no_messagebox)MessageBoxW(NULL, UTF8ToStdWString(e.what()).c_str(), L"Error", MB_OK | MB_ICONERROR);
				return -1;
			}
		}
		else if (strcmp(argv[i], "help") == 0) {
			MessageBoxW(NULL, usage, L"Usage", MB_OK);
			return 0;
		}
		else {
			MessageBoxW(NULL, usage, L"Usage", MB_OK);
			return 0;
		}
	}
	return -1;
}

int GUIMain(int argc, char** argv) {
	//QApplication app(argc, argv);
	//MainWindow main_window;
	//main_window.show();
	//return app.exec();
	MessageBoxW(NULL, L"GUI interface has not finished yet. Please use this tool in command line with '-nogui' option the first option.",
		L"Sorry", MB_OK | MB_ICONERROR);
	return 0;
}

int main(int argc, char** argv) {
	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "-nogui") == 0) {
			return CmdMain(argc, argv, i + 1);
		}
		else if (strcmp(argv[i], "-quiet") == 0) {
			no_messagebox = true;
		}
	}
	return GUIMain(argc, argv);
}