#include "Common.h"
#include "ConsoleHelper.h"
#include "Win32Helper.h"
#include "FontAnalyzer.h"
#include "FileDeduplicate.h"

#include <fcntl.h>
#include <io.h>

DWORD g_ProcessorCount = []()
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
}();
DWORD g_WorkerCount = g_ProcessorCount / 2;

std::atomic<bool> g_cancelToken{false};

BOOL WINAPI ControlHandler(DWORD dwCtrlType)
{
	g_cancelToken = true;
	return TRUE;
}

void FindOptions(int argc, wchar_t** argv, std::vector<std::wstring>& input, std::wstring& output, bool& deduplicate)
{
	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] == L'-')
		{
			if (_wcsicmp(argv[i], L"-output") == 0)
			{
				if (i + 1 < argc)
				{
					output = argv[i + 1];
					++i;
				}
				else
				{
					throw std::runtime_error("missing argument for option -output");
				}
			}
			else if (_wcsicmp(argv[i], L"-dedup") == 0)
			{
				deduplicate = true;
			}
			else if (_wcsicmp(argv[i], L"-worker") == 0)
			{
				if (i + 1 < argc)
				{
					g_WorkerCount = std::stoul(argv[i + 1]);
					if (g_WorkerCount >= 100)
						throw std::runtime_error("worker count must be within 1-99");
					++i;
				}
				else
				{
					throw std::runtime_error("missing argument for option -worker");
				}
			}
			else
			{
				char buffer[64];
				snprintf(buffer, 64, "unknown option at position %d", i);
				throw std::runtime_error(buffer);
			}
		}
		else
		{
			input.emplace_back(GetFullPathName(argv[i]).get());
		}
	}
	if (input.empty())
	{
		throw std::runtime_error("missing input directory");
	}
}

void PrintHelp()
{
	std::wcout << SetOutputDefault
		<< "Usage: FontDatabaseBuilder.exe [-output OutputFile] [-dedup] [-worker WorkerCount] Directory... \n"
		<< "\t-output OutputFile: path to the output\n"
		<< "\t-dedup: enable deduplication of files\n"
		<< "\t-worker WorkerCount: set work thread count, default is half of your processor count\n"
		<< "\tDirectory: directories need to build index" << std::endl;
}

int wmain(int argc, wchar_t* argv[], wchar_t* envp[])
{
	// set control signal handler
	SetConsoleCtrlHandler(ControlHandler, TRUE);
	// prepare utf8 console
	UINT originalCP = GetConsoleCP();
	UINT originalOutputCP = GetConsoleOutputCP();
	auto revertConsoleCP = wil::scope_exit([=]()
	{
		SetConsoleCP(originalCP);
		SetConsoleOutputCP(originalOutputCP);
		std::wcout << SetOutputDefault;
	});
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	setlocale(LC_ALL, ".UTF8");
	// cin/wcin is broken for wide characters
	// use helper function instead
	// use wcout to print wide string
	// use cout to print utf8 string
	std::wcout.imbue(std::locale(".UTF8"));
	std::wcerr.imbue(std::locale(".UTF8"));

	try
	{
		// validate arguments
		std::vector<std::wstring> input;
		std::wstring output;
		bool deduplicate;
		try
		{
			FindOptions(argc, argv, input, output, deduplicate);
		}
		catch (std::exception& e)
		{
			std::cout << SetOutputRed << e.what() << std::endl;
			PrintHelp();
			return 1;
		}

		std::wcout << SetOutputDefault << "Current Directory: \n    " << SetOutputYellow << GetCurrentDirectory().get()
			<< std::endl;

		std::wcout << SetOutputDefault << "Input directory: \n";
		for (auto& i : input)
		{
			if (!IsDirectory(i.c_str()))
			{
				std::wcout << "    " << SetOutputRed << i << " is not a directory!" << std::endl;
				return 1;
			}
			std::wcout << "    " << SetOutputYellow << i << std::endl;
		}

		if (output.empty())
		{
			std::wcout << SetOutputYellow << "Output file path not specified. Generate path from first input." <<
				std::endl;
			output = input[0];
			if (output.back() != '\\')output.push_back('\\');
			output += L"FontIndex.xml";
			std::wcout << SetOutputDefault << L"Output path: \n    " << SetOutputYellow << output << std::endl;
			while (AskConsoleQuestionBoolean(L"Do you want to change output path?"))
			{
				std::wcout << "Enter output path: ";

				std::wstring userPath = ConsoleReadLine();
				output = userPath;
				std::wcout << SetOutputDefault << L"Output path: \n    " << SetOutputYellow << output << std::endl;
			}
		}
		else
		{
			std::wcout << SetOutputDefault << L"Output path: \n    " << SetOutputYellow << output << std::endl;
		}
		if (output.empty())
		{
			std::wcout << SetOutputRed << L"Output path is empty!" << std::endl << SetOutputDefault;
			return 1;
		}
		std::wcout << SetOutputDefault;

		std::wcout << "WORKER_COUNT = " << g_WorkerCount << std::endl;

		std::vector<std::wstring> fileSet;
		std::vector<uint64_t> fileSize;
		for (auto& i : input)
		{
			ScanDirectory(i.c_str(), fileSet, fileSize, [](const wchar_t* path)
			{
				constexpr const wchar_t* acceptExt[] = {L".ttf", L".otf", L".ttc", L".otc"};
				constexpr size_t acceptExtLen[] = {4, 4, 4, 4};
				size_t length = wcslen(path);
				for (size_t i = 0; i < std::extent_v<decltype(acceptExt)>; ++i)
				{
					if (_wcsicmp(path + length - acceptExtLen[i], acceptExt[i]) == 0)
						return true;
				}
				return false;
			});
		}
		std::wcout << "Discovered " << fileSet.size() << " files." << std::endl;

		if (fileSet.empty())
		{
			std::wcout << "Nothing to do." << std::endl;
			return 0;
		}

		if (deduplicate)
		{
			std::wcout << "Deduplicate..." << std::endl;
			std::atomic<size_t> progress = 0;
			const size_t total = fileSet.size();
			std::thread thr([&]()
			{
				fileSet = Deduplicate(fileSet, fileSize, progress);
			});
			while (!g_cancelToken)
			{
				EraseLineStruct::EraseLine();
				PrintProgressBar(progress, total, 28);
				if (progress == total)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (thr.joinable())
				thr.join();
			std::wcout << std::endl;
			ThrowIfCancelled();
			std::wcout << "Discovered " << fileSet.size() << " files." << std::endl;
		}

		std::wcout << "Build database..." << std::endl;

		std::mutex logLock;
		std::mutex consumeLock;
		std::mutex resultLock;

		auto nextFile = fileSet.begin();
		auto noFile = fileSet.end();

		sfh::FontDatabase db;
		db.m_fonts.reserve(fileSet.size()); // reduce reallocation

		std::vector<std::thread> workers;
		for (size_t i = 0; i < g_WorkerCount; ++i)
		{
			workers.emplace_back([&]()
			{
				FontAnalyzer analyzer;
				while (!g_cancelToken)
				{
					std::vector<std::wstring>::iterator path;
					try
					{
						{
							std::lock_guard lg(consumeLock);
							if (nextFile == noFile)break;
							path = nextFile;
							++nextFile;
						}
						auto result = analyzer.AnalyzeFontFile(path->c_str());
						{
							std::lock_guard lg(resultLock);
							db.m_fonts.insert(db.m_fonts.end(),
							                  std::make_move_iterator(result.begin()),
							                  std::make_move_iterator(result.end()));
						}
					}
					catch (std::exception& e)
					{
						std::lock_guard lg(logLock);
						EraseLineStruct::EraseLine();
						std::wcout << SetOutputRed << L"Error analyzing file: " << *path << L'\n';
						std::cout << "Error description: " << e.what() << std::endl << SetOutputDefault;
					}
				}
			});
		}

		while (!g_cancelToken)
		{
			{
				std::lock_guard lg(logLock);
				size_t done;
				{
					std::lock_guard lg2(consumeLock);
					done = nextFile - fileSet.begin();
				}

				EraseLineStruct::EraseLine();
				PrintProgressBar(done, fileSet.size(), 28);
				if (done == fileSet.size())
					break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		for (auto& thr : workers)
		{
			if (thr.joinable())
				thr.join();
		}
		ThrowIfCancelled();
		std::wcout << std::endl;

		std::wcout << "Writing output..." << std::endl;

		sfh::FontDatabase::WriteToFile(output, db);

		std::wcout << "Done." << std::endl;
	}
	catch (std::exception& e)
	{
		std::cout << SetOutputRed << e.what() << std::endl << SetOutputDefault;
		return 1;
	}

	return 0;
}
