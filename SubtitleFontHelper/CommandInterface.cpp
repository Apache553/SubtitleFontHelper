#include "CommandInterface.h"

#include <algorithm>
#include <stdexcept>

#include <Windows.h>

#include "ASSReader.h"
#include "ConfigFile.h"
#include "Common.h"
#include "FontDatabase.h"
#include "Daemon.h"
#include "ProcessMonitor.h"

#define QUAZIP_STATIC
#include <quazip.h>
#include <quazipfile.h>

struct _ConWarpper {
	bool console_enabled;
	WORD ori_attr;

	HANDLE output = NULL;
	HANDLE input = NULL;

	_ConWarpper(bool enable) :console_enabled(enable), ori_attr(0) {
		if (console_enabled) {
#if 0
			if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE)
#endif
				AllocConsole();
			SetConsoleCtrlHandler(HandlerRoutine, TRUE);
			output = GetStdHandle(STD_OUTPUT_HANDLE);
			input = GetStdHandle(STD_INPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFO info;
			if (GetConsoleScreenBufferInfo(output, &info) != 0) {
				ori_attr = info.wAttributes;
			}
		}
	}
	~_ConWarpper() {
		if (console_enabled) {
			FreeConsole();
		}
	}

	void WaitContinue() {
		FlushConsoleInputBuffer(input);
		WriteLine(L"Press any key to continue...");
		INPUT_RECORD rec;
		DWORD n_read;
		while (true) {
			if (ReadConsoleInputW(input, &rec, 1, &n_read) == FALSE)break;
			if (n_read == 0)continue;
			if (rec.EventType == KEY_EVENT)break;
		}
	}

	static BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
		exit(0);
		return TRUE;
	}

	_ConWarpper& Write(const std::wstring& str) {
		if (console_enabled)WriteConsoleW(output, str.data(), str.size(), NULL, NULL);
		return *this;
	}

	_ConWarpper& Write(wchar_t ch) {
		if (console_enabled)WriteConsoleW(output, &ch, 1, NULL, NULL);
		return *this;
	}

	_ConWarpper& WriteLine(const std::wstring& str) {
		Write(str);
		Write(L'\n');
		return *this;
	}

	_ConWarpper& SetTextColor(WORD attr) {
		SetConsoleTextAttribute(output, ori_attr & (~0xF) | (attr & 0xF));
		return *this;
	}

	_ConWarpper& ResetTextColor() {
		SetConsoleTextAttribute(output, ori_attr);
		return *this;
	}

};

struct _LoggerConHelper {
	_ConWarpper& warpper;
	MyLogger& logger;
	bool verbose;
	size_t id;

	_LoggerConHelper(_ConWarpper& w, MyLogger& l, bool s) :warpper(w), logger(l), verbose(s) {
		id = logger.AddOutputFunc([&](const std::wstring& str, LogLevel level) {
			switch (level)
			{
			case LogLevel::Debug:
				if (verbose) {
					warpper.SetTextColor(FOREGROUND_INTENSITY).Write(str).ResetTextColor();
				}
				break;
			case LogLevel::Info:
				warpper.ResetTextColor().Write(str);
				break;
			case LogLevel::Warning:
				warpper.SetTextColor(FOREGROUND_RED | FOREGROUND_GREEN).Write(str).ResetTextColor();
				break;
			case LogLevel::Error:
				warpper.SetTextColor(FOREGROUND_RED).Write(str).ResetTextColor();
				break;
			default:
				warpper.Write(str);
				break;
			}
			});

	}

	~_LoggerConHelper() {
		logger.RemoveOutputFunc(id);
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

void InvokeRundll32(const std::wstring& dll, const std::wstring& func, const std::wstring& arg) {
	//std::wstring rundll32_path = GetSystem32Directory() + L"\\rundll32.exe";
	std::wstring real_arg;
	real_arg += L"rundll32.exe ";
	real_arg += L'\"';
	real_arg += dll;
	real_arg += L"\",";
	real_arg += func;
	real_arg += L' ';
	real_arg += arg;
	// obtain tmp buffer to make CreateProcessW happy
	std::unique_ptr<wchar_t[]> cmdl_buf(new wchar_t[real_arg.size() + 1]);
	wcscpy(cmdl_buf.get(), real_arg.c_str());
	wchar_t* env_blk = GetEnvironmentStringsW();
	if (env_blk == nullptr)return;
	std::wstring new_env;
	new_env += L"NODETOUR=1";
	new_env += L'\0';
	wchar_t prev_ch = 0;
	while (prev_ch != 0 || *env_blk != 0) {
		new_env.push_back(*env_blk);
		++env_blk;
		prev_ch = new_env.back();
	}
	new_env.push_back(0);
	STARTUPINFOW start_info;
	PROCESS_INFORMATION process_info;
	ZeroMemory(&start_info, sizeof(start_info));
	start_info.cb = sizeof(start_info);
	BOOL create_ret = CreateProcessW(nullptr, cmdl_buf.get(), NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT,
		(void*)new_env.c_str(), NULL, &start_info, &process_info);
	if (create_ret != FALSE) {
		CloseHandle(process_info.hProcess);
		CloseHandle(process_info.hThread);
	}
}

void LoadIndex(const CommandLineArgCollector& arg_collector, const MyConfig& conf, FontDatabase& db, _ConWarpper& warpper) {
	auto sess = g_logger.GetNewSession();
	for (const auto& fn : conf.index_files) {
		sess.Info(L"Loading ", fn);
		bool suc = db.LoadDatabase(fn);
		if (suc)sess.Info(L"Success!");
		else sess.Error(L"Failed.");
	}
	auto arg_index_files = arg_collector.GetArgs().equal_range("-index");
	for (auto iter = arg_index_files.first; iter != arg_index_files.second; ++iter) {
		auto fn = UTF8ToStdWString(iter->second);
		sess.Info(L"Loading ", fn);
		bool suc = db.LoadDatabase(fn);
		if (suc)sess.Info(L"Success!");
		else sess.Error(L"Failed.");
	}
	sess.Info(L"Index loading complete.");
	sess.Info(L"There are ", db.GetCount(), L" font names in index(including addtional truncated names).");
	sess.Info(L"There are ", db.GetFileCount(), L" font files in index.");
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
	bool verbose = arg_collector.IsExist("-verbose");
	_LoggerConHelper helper(warpper, g_logger, verbose);

	const auto& filename = args.find("-file")->second;
	if (!ass.OpenFile(filename)) {
		throw std::runtime_error("unable to open file: " + filename);
	}

	// load config
	std::wstring config_path;
	auto arg_conf = arg_collector.GetFirst("-config");
	if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
	else config_path = GetDefaultConfigFilename();
	if (!arg_collector.IsExist("-nodep")) {
		conf = MyConfig::FromFile(config_path);
	}

	// process index files

	LoadIndex(arg_collector, conf, db, warpper);



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
				g_logger.GetNewSession().Info(str);
			}
		};
	}

	if (arg_collector.IsExist("-list")) {
		std::wstring display_str;
		for (const auto& fnt : ref_fonts) {
			display_str += fnt;
			display_str += L'\n';
		}
		if (!display_str.empty())display(display_str);
	}
	else if (arg_collector.IsExist("-missing")) {
		std::wstring display_str;
		for (const auto& fnt : inaccessible_fonts) {
			display_str += fnt;
			display_str += L'\n';
		}
		if (!display_str.empty())display(display_str);
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
					g_logger.GetNewSession().Info(L"Compressing: ", item.name);
					file.write((char*)mem, size);
					file.close();
					});
				if (!good) {
					g_logger.GetNewSession().Warning(L"Missing: ", item.name);
					missing_list += item.name;
					missing_list += L'\n';
				}
			}
			catch (std::out_of_range e) {
				auto mem = sys_db.ExportSystemFontToMemory(fnt_name);
				if (mem.first.get() == nullptr) {
					g_logger.GetNewSession().Warning(L"Missing: ", fnt_name);
					missing_list += GetUndecoratedFontName(fnt_name);
					missing_list += L'\n';
					continue;
				}
				QuaZipFile file(&zip);
				file.open(QIODevice::WriteOnly, QuaZipNewInfo(QString::fromStdWString(fnt_name + DetectFontExtensionName((char*)mem.first.get(), mem.second))));
				g_logger.GetNewSession().Info(L"Compressing: ", fnt_name);
				file.write((char*)mem.first.get(), mem.second);
				file.close();
			}
		}
		for (const auto& fnt_name : inaccessible_fonts) {
			g_logger.GetNewSession().Warning(L"Missing: ", fnt_name);
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
	warpper.WaitContinue();
	return 0;
}

int DoCmdDaemon(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;
	FontDatabase db;
	ProcessMonitor monitor;

	bool no_console = arg_collector.IsExist("-nocon");
	_ConWarpper warpper(!no_console);
	bool verbose = arg_collector.IsExist("-verbose");
	_LoggerConHelper helper(warpper, g_logger, verbose);

	// load config
	std::wstring config_path;
	auto arg_conf = arg_collector.GetFirst("-config");
	if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
	else config_path = GetDefaultConfigFilename();
	if (!arg_collector.IsExist("-nodep")) {
		conf = MyConfig::FromFile(config_path);
	}

	// process index files

	LoadIndex(arg_collector, conf, db, warpper);

	bool do_process_mon = arg_collector.IsExist("-procmon");

	if (do_process_mon) {
		int error_code = ProcessMonitor::AdjustPrivilege();
		if (error_code != ERROR_SUCCESS) {
			if (error_code == ERROR_NOT_ALL_ASSIGNED) {
				g_logger.GetNewSession().Warning(L"Unable to get SeDebugPrivilege, "
					L"please consider running this program as administrator. Or this program may have unexpected behavior.");
			}
			else {
				throw std::runtime_error("Unable to adjust current process privilege(SeDebugPrivilege)");
			}
		}
		// add monitored process names
		for (const auto& pn : conf.monitored_process) {
			monitor.AddProcessName(pn);
		}
		auto arg_proc_name = args.equal_range("-process");
		for (auto iter = arg_proc_name.first; iter != arg_proc_name.second; ++iter) {
			monitor.AddProcessName(UTF8ToStdWString(iter->second));
		}
		// setup inject function

		std::wstring dll32, dll64;
		std::wstring my_exe_path;
		// get our exe path
		size_t len = 1024;
		std::unique_ptr<wchar_t[]> mod_fn(new wchar_t[len]);
		while (true) {
			len = GetModuleFileNameW(NULL, mod_fn.get(), len);
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				len *= 1.5;
				if (len > 32767)break;
				delete[] mod_fn.release();
				mod_fn.reset(new wchar_t[len]);
			}
			else if (GetLastError() == ERROR_SUCCESS) {
				my_exe_path = mod_fn.get();
				break;
			}
			else {
				break;
			}
		}
		if (my_exe_path.empty())throw std::runtime_error("Unable to get current exe path");
		// extract path
		size_t spos = my_exe_path.rfind(L'\\');
		if (spos == std::wstring::npos) throw std::runtime_error("invalid current exe path");
		my_exe_path.resize(spos);
		dll32 = my_exe_path + L"\\GDIHook32.dll";
		dll64 = my_exe_path + L"\\GDIHook64.dll";

		// do setup
		monitor.SetCallback([dll32, dll64, &warpper](const std::wstring& exe_path, uint32_t pid) {
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (hProcess == NULL) {
				g_logger.GetNewSession().Error(L"Unable to query process ", pid);
				return;
			}
			BOOL is_32_process = false;
			USHORT process_mach, host_mach;
			if (IsWow64Process2(hProcess, &process_mach, &host_mach) == 0) {
				CloseHandle(hProcess);
				return;
			}
			CloseHandle(hProcess);
			if (process_mach == IMAGE_FILE_MACHINE_I386)is_32_process = true;
			g_logger.GetNewSession().Debug(L"Injecting process ", pid);
			InvokeRundll32(is_32_process ? dll32 : dll64, L"DoInject", std::to_wstring(pid));
			});
	}
	std::mutex mut;
	QueryDaemon daemon(db, mut);
	if (!no_console) {
		daemon.SetCallback([&](const std::wstring& qn, const std::wstring& path, bool good) {
			static size_t seq = 0;
			auto oses = g_logger.GetNewSession();
			oses.Info(L"RemoteCall [ " + std::to_wstring(seq++) + L" ]:");
			oses.Info(L"  " + qn);
			if (!good)oses.Error(L"  " + path);
			else oses.Info(L"  " + path);
			});
	}
	g_logger.GetNewSession().Info(L"Daemon Started.");
	if (do_process_mon) {
		if (arg_collector.IsExist("-pollinterval")) {
			auto inte = arg_collector.GetFirst("-pollinterval");
			float interval = 1.0f;
			try {
				interval = std::stof(inte.second);
			}
			catch (...) {
				throw std::runtime_error("unable to convert interval value");
			}
			monitor.SetPollInterval(interval);
			g_logger.GetNewSession().Debug(L"Set PollInterval to ", interval);
		}
		monitor.RunMonitor();
		while (true) {
			if (monitor.GetLastException() != nullptr)
				std::rethrow_exception(monitor.GetLastException());
			if (monitor.IsRunning())break;
		}
		g_logger.GetNewSession().Info(L"ProcessMonitor Started.");
	}
	daemon.RunDaemon();
	monitor.CancelMonitor();
	warpper.WaitContinue();
	return 0;
}

int DoCmdIndex(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;

	bool no_console = arg_collector.IsExist("-nocon");
	_ConWarpper warpper(!no_console);
	bool verbose = arg_collector.IsExist("-verbose");
	_LoggerConHelper helper(warpper, g_logger, verbose);

	// load config
	std::wstring config_path;
	auto arg_conf = arg_collector.GetFirst("-config");
	if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
	else config_path = GetDefaultConfigFilename();
	if (!arg_collector.IsExist("-nodep")) {
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

	bool good;
	{
		auto oses = g_logger.GetNewSession();
		good = WalkDirectoryAndBuildDatabase(dir, output, [&](const std::wstring& fn) {
			oses.PrintHeader() << L"Read: " << fn << L'\n';
			}, true, exts);
	}
	if (!good) {
		throw std::runtime_error("build index failed");
	}
	if (!arg_collector.IsExist("-nodep")) {
		if (arg_collector.IsExist("-writeconf")) {
			std::wstring full_output = GetFullPath(output);
			if (full_output.empty()) {
				g_logger.GetNewSession().Warning(L"Get full output path failed. Using given path...");
				full_output = output;
			}
			conf.index_files.insert(full_output);
			conf.ToFile(config_path);
		}
	}
	warpper.WaitContinue();
	return 0;
}

int DoCmdConfig(const CommandLineArgCollector& arg_collector)
{
	const auto& args = arg_collector.GetArgs();
	MyConfig conf;
	// load config
	std::wstring config_path;
	auto arg_conf = arg_collector.GetFirst("-config");
	if (arg_conf.first)config_path = UTF8ToStdWString(arg_conf.second);
	else config_path = GetDefaultConfigFilename();
	if (!arg_collector.IsExist("-nodep")) {
		conf = MyConfig::FromFile(config_path);

		if (arg_collector.IsExist("-edit")) {
			HINSTANCE exec_ret = ShellExecuteW(NULL, L"edit", config_path.c_str(), NULL, NULL, SW_SHOW);
			return 0;
		}

		if (arg_collector.IsExist("-add") && arg_collector.IsExist("-del")) {
			throw std::runtime_error("-add and -del conflict");
		}
		else if (arg_collector.IsExist("-add") || arg_collector.IsExist("-del")) {
			if (!arg_collector.IsExist("-value"))throw std::runtime_error("you must specify a value");
			std::wstring op_type, op_target, op_value;
			op_value = UTF8ToStdWString(arg_collector.GetFirst("-value").second);
			if (op_value.empty())throw std::runtime_error("invalid arguments");
			auto lookup = arg_collector.GetFirst("-add");
			if (lookup.first) {
				op_type = L"add";
				op_target = UTF8ToStdWString(lookup.second);
			}
			lookup = arg_collector.GetFirst("-del");
			if (lookup.first) {
				op_type = L"del";
				op_target = UTF8ToStdWString(lookup.second);
			}

			if (op_type.empty() || op_target.empty())throw std::runtime_error("invalid arguments");

			std::set<std::wstring>* edit_set = nullptr;

			if (op_target == L"index") {
				edit_set = &conf.index_files;
			}
			else if (op_target == L"process") {
				edit_set = &conf.monitored_process;
			}

			assert(edit_set != nullptr);

			if (op_type == L"add") {
				edit_set->insert(op_value);
			}
			else if (op_type == L"del") {
				auto iter = edit_set->find(op_value);
				edit_set->erase(iter);
			}

			conf.ToFile(config_path);
			return 0;
		}

	}
	return 0;
}
