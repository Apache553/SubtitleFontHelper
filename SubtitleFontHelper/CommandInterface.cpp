#include "CommandInterface.h"

#include <algorithm>
#include <stdexcept>

#include <Windows.h>

#include "ASSReader.h"
#include "ConfigFile.h"
#include "Common.h"
#include "FontDatabase.h"
#include "Daemon.h"

#define QUAZIP_STATIC
#include <quazip.h>
#include <quazipfile.h>

struct _ConWarpper {
	bool console_enabled;
	_ConWarpper(bool enable) :console_enabled(enable) {
		if (console_enabled) {
#if 0
			if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE)
#endif
				AllocConsole();
		}
	}
	~_ConWarpper() {
		if (console_enabled) {
			FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
			WriteLine(L"Press any key to continue...");
			INPUT_RECORD rec;
			DWORD n_read;
			while (true) {
				if (ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &rec, 1, &n_read) == FALSE)break;
				if (n_read == 0)continue;
				if (rec.EventType == KEY_EVENT)break;
			}
			FreeConsole();
		}
	}

	_ConWarpper& Write(const std::wstring& str) {
		if (console_enabled)WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), str.data(), str.size(), NULL, NULL);
		return *this;
	}

	_ConWarpper& Write(wchar_t ch) {
		if (console_enabled)WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), &ch, 1, NULL, NULL);
		return *this;
	}

	_ConWarpper& WriteLine(const std::wstring& str) {
		Write(str);
		Write(L'\n');
		return *this;
	}
};

CommandLineArgCollector::CommandLineArgCollector(const std::vector<CommandArgDef>& def) :argdef(def)
{
}

CommandLineArgCollector::CommandLineArgCollector(std::initializer_list<CommandArgDef> init) : argdef(init)
{
}

std::multimap<std::string, std::string>& CommandLineArgCollector::CollectArg(int argc, char** argv, int begin_idx)
{
	for (int i = begin_idx; i < argc; ++i) {
		std::string cur_token = argv[i];
		std::vector<CommandArgDef>::iterator cur_def =
			std::find_if(argdef.begin(), argdef.end(), [&](const CommandArgDef& v)->bool {
			return v.id == cur_token;
				});
		if (cur_def == argdef.end()) {
			throw std::out_of_range("unexcepted option: " + cur_token);
		}
		if (!cur_def->can_dup && args.find(cur_token) != args.end()) {
			throw std::overflow_error("duplicated option: " + cur_def->id);
		}
		if (cur_def->has_value && i + 1 >= argc) {
			throw std::invalid_argument("option " + cur_def->id + " requires a value");
		}
		if (cur_def->has_value) {
			args.insert(std::make_pair(cur_def->id, std::string(argv[i + 1])));
			++i;
		}
		else {
			args.insert(std::make_pair(cur_def->id, std::string()));
		}
	}
	for (const auto& def : argdef) {
		if (!def.is_mandatory)continue;
		if (args.find(def.id) == args.end()) {
			throw std::invalid_argument("option " + def.id + " is mandatory");
		}
	}
	return args;
}

const std::multimap<std::string, std::string>& CommandLineArgCollector::GetArgs() const
{
	return args;
}

bool CommandLineArgCollector::IsExist(const std::string& key) const
{
	return args.find(key) != args.end();
}

std::pair<bool, std::string> CommandLineArgCollector::GetFirst(const std::string& key) const
{
	auto iter = args.find(key);
	if (iter == args.end()) {
		return std::make_pair(false, std::string());
	}
	else {
		return std::make_pair(true, iter->second);
	}
}

int DoCmdASS(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	// check file
	ASSParser ass;
	MyConfig conf;
	FontDatabase db;
	SystemFontManager sys_db;

	bool no_console = arg_collector.IsExist("-nocon");
	_ConWarpper warpper(!no_console);

	const auto& filename = args.find("-file")->second;
	if (!ass.OpenFile(filename)) {
		throw std::runtime_error("unable to open file: " + filename);
	}

	// load config
	if (!arg_collector.IsExist("-nodep")) {
		std::wstring config_path;
		auto arg_conf = arg_collector.GetFirst("-config");
		if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
		else config_path = GetDefaultConfigFilename();
		conf = MyConfig::FromFile(config_path);
	}

	// process index files

	for (const auto& fn : conf.index_files) {
		db.LoadDatabase(fn);
	}
	auto arg_index_files = args.equal_range("-index");
	for (auto iter = arg_index_files.first; iter != arg_index_files.second; ++iter) {
		db.LoadDatabase(UTF8ToStdWString(iter->second));
	}

	if (arg_collector.IsExist("-list") && arg_collector.IsExist("-missing")) {
		throw std::invalid_argument("option -list & -missing can't be present at the same time");
	}

	const std::vector<std::wstring>& ref_fonts = ass.GetReferencedFonts();

	std::vector<std::wstring> accessible_fonts;
	std::vector<std::wstring> inaccessible_fonts;

	// classify
	bool load_flag = arg_collector.IsExist("-load");

	for (const auto& fnt : ref_fonts) {
		try {
			FontItem item = db.QueryFont(fnt);
			accessible_fonts.push_back(fnt);
			if (load_flag)AddFontResourceW(item.path.c_str());
			continue;
		}
		catch (std::out_of_range e) {
		}
		if (sys_db.QuerySystemFont(fnt)) {
			accessible_fonts.push_back(fnt);
		}
		else {
			inaccessible_fonts.push_back(fnt);
		}
	}

	std::function<void(const std::wstring&)> display;
	if (arg_collector.IsExist("-output")) {
		auto output_file = UTF8ToStdWString(arg_collector.GetFirst("-output").second);
		display = [=](const std::wstring& str) {
			WriteAllToFile(output_file, StdWStringToUTF8(str));
		};
	}
	else {
		display = [&](const std::wstring& str) {
			if (no_console) {
				MessageBoxW(NULL, str.c_str(), L"INFO", MB_OK);
			}
			else {
				warpper.WriteLine(str);
			}
		};
	}

	if (arg_collector.IsExist("-list")) {
		std::wstring display_str;
		for (const auto& fnt : ref_fonts) {
			display_str += fnt;
			display_str += L'\n';
		}
		display(display_str);
	}
	else if (arg_collector.IsExist("-missing")) {
		std::wstring display_str;
		for (const auto& fnt : inaccessible_fonts) {
			display_str += fnt;
			display_str += L'\n';
		}
		display(display_str);
	}

	if (arg_collector.IsExist("-export")) {
		auto output_file = UTF8ToStdWString(arg_collector.GetFirst("-export").second);
		QuaZip zip(QString::fromStdWString(output_file));
		if (!zip.open(QuaZip::Mode::mdCreate))
		{
			throw std::runtime_error("Unable to open export file");
		}
		std::wstring missing_list;
		for (const auto& fnt_name : accessible_fonts)
		{
			try {
				FontItem item = db.QueryFont(fnt_name);
				bool good = GetFileMemoryBuffer(item.path, [&](void* mem, size_t size, const std::wstring& path) {
					QuaZipFile file(&zip);
					file.open(QIODevice::WriteOnly, QuaZipNewInfo(QString::fromStdWString(item.name + DetectFontExtensionName((char*)mem, size))));
					dbgout << L"Compressing: " << item.name << L'\n';
					warpper.WriteLine(L"Compressing: " + item.name);
					file.write((char*)mem, size);
					file.close();
					});
				if (!good) {
					dbgout << L"Missing: " << item.name << L'\n';
					warpper.WriteLine(L"Missing: " + item.name);
					missing_list += item.name;
					missing_list += L'\n';
				}
			}
			catch (std::out_of_range e) {
				auto mem = sys_db.ExportSystemFontToMemory(fnt_name);
				if (mem.first.get() == nullptr) {
					dbgout << L"Missing: " << fnt_name << L'\n';
					warpper.WriteLine(L"Missing: " + fnt_name);
					missing_list += GetUndecoratedFontName(fnt_name);
					missing_list += L'\n';
					continue;
				}
				QuaZipFile file(&zip);
				file.open(QIODevice::WriteOnly, QuaZipNewInfo(QString::fromStdWString(fnt_name + DetectFontExtensionName((char*)mem.first.get(), mem.second))));
				dbgout << L"Compressing: " << fnt_name << L'\n';
				warpper.WriteLine(L"Compressing: " + fnt_name);
				file.write((char*)mem.first.get(), mem.second);
				file.close();
			}
		}
		for (const auto& fnt_name : inaccessible_fonts) {
			dbgout << L"Missing: " << fnt_name << L'\n';
			warpper.WriteLine(L"Missing: " + fnt_name);
			missing_list += GetUndecoratedFontName(fnt_name);
			missing_list += L'\n';
		}
		QuaZipFile missing_file(&zip);
		missing_file.open(QIODevice::WriteOnly, QuaZipNewInfo(QString::fromStdWString(L"missing.txt")));
		std::string missing_str = StdWStringToUTF8(missing_list);
		missing_file.write(missing_str.data(), missing_str.size());
		missing_file.close();
		zip.close();
	}

	return 0;
}

int DoCmdDaemon(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;
	FontDatabase db;

	bool no_console = arg_collector.IsExist("-nocon");
	_ConWarpper warpper(!no_console);

	// load config
	if (!arg_collector.IsExist("-nodep")) {
		std::wstring config_path;
		auto arg_conf = arg_collector.GetFirst("-config");
		if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
		else config_path = GetDefaultConfigFilename();
		conf = MyConfig::FromFile(config_path);
	}

	// process index files

	for (const auto& fn : conf.index_files) {
		db.LoadDatabase(fn);
	}
	auto arg_index_files = args.equal_range("-index");
	for (auto iter = arg_index_files.first; iter != arg_index_files.second; ++iter) {
		db.LoadDatabase(UTF8ToStdWString(iter->second));
	}

	std::mutex mut;
	QueryDaemon daemon(db, mut);
	if (!no_console) {
		daemon.SetCallback([&](const std::wstring& qn, const std::wstring& path) {
			static size_t seq = 0;
			warpper.Write(L"[").Write(std::to_wstring(seq++)).Write(L"]\n");
			warpper.Write(L"Query: ").Write(qn).Write(L'\n');
			warpper.Write(L"Result: ");
			if (path.empty())warpper.Write(L"not found in index");
			else warpper.Write(path);
			warpper.Write(L'\n');
			});
	}
	warpper.WriteLine(L"Daemon Start.");
	daemon.RunDaemon();
	return 0;
}

int DoCmdIndex(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;
	bool no_console = arg_collector.IsExist("-nocon");
	_ConWarpper warpper(!no_console);
	// load config
	if (!arg_collector.IsExist("-nodep")) {
		std::wstring config_path;
		auto arg_conf = arg_collector.GetFirst("-config");
		if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
		else config_path = GetDefaultConfigFilename();
		conf = MyConfig::FromFile(config_path);
	}
	const std::wstring dir = UTF8ToStdWString(arg_collector.GetFirst("-dir").second);
	const std::wstring output = UTF8ToStdWString(arg_collector.GetFirst("-output").second);
	bool default_override = arg_collector.IsExist("-ext");
	std::vector<std::wstring> exts = { {L".ttf"} ,{L".ttc"} ,{L".otf"} };
	if (default_override) {
		exts.clear();
		auto range = args.equal_range("-ext");
		for (auto iter = range.first; iter != range.second; ++iter) {
			exts.push_back(UTF8ToStdWString('.' + iter->second));
		}
	}

	bool good = WalkDirectoryAndBuildDatabase(dir, output, [&](const std::wstring& fn) {
		warpper.WriteLine(L"Read: " + fn);
		dbgout << L"Read: " << fn << L'\n';
		}, true, exts);
	if (!good) {
		throw std::runtime_error("build index failed");
	}
	return 0;
}

int DoCmdConfig(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;
	// load config
	if (!arg_collector.IsExist("-nodep")) {
		std::wstring config_path;
		auto arg_conf = arg_collector.GetFirst("-config");
		if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
		else config_path = GetDefaultConfigFilename();
		conf = MyConfig::FromFile(config_path);
		if (arg_collector.IsExist("-edit")) {
			HINSTANCE exec_ret = ShellExecuteW(NULL, L"edit", config_path.c_str(), NULL, NULL, SW_SHOW);
		}
	}
	return 0;
}
