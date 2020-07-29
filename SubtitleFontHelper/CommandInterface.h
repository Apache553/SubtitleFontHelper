
#pragma once

#include <string>
#include <vector>
#include <map>

struct CommandArgDef {
	std::string id{};  // utf8
	bool has_value{ false };
	bool can_dup{ false };
	bool is_mandatory{ false };
};

class CommandLineArgCollector {
private:
	std::vector<CommandArgDef> argdef;
	std::multimap<std::string, std::string> args;
public:
	/**
		@brief Initialize
		@param def - arg def
	*/
	CommandLineArgCollector(const std::vector<CommandArgDef>& def);
	CommandLineArgCollector(std::initializer_list<CommandArgDef> init);

	CommandLineArgCollector() = delete;
	CommandLineArgCollector(CommandLineArgCollector&&) = delete;
	CommandLineArgCollector(const CommandLineArgCollector&) = delete;
	CommandLineArgCollector& operator=(const CommandLineArgCollector&) = delete;
	CommandLineArgCollector& operator=(CommandLineArgCollector&&) = delete;

	/**
		@brief Parse args and store them into a map
		@param argc - count of argv
		@param argv - array of arg
		@param begin_idx - the index to start parse
	*/
	std::multimap<std::string, std::string>& CollectArg(int argc, char** argv, int begin_idx);

	/**
		@brief Get args
		@return const reference to internal dictionary
	*/
	const std::multimap<std::string, std::string>& GetArgs()const;

	/**
		@brief Check if given key exists
		@param key - Key
		@return true - exist, false - nonexist
	*/
	bool IsExist(const std::string& key)const;

	/**
		@brief Get first value of given key
		@param key - Key
		@return value / exception
	*/
	std::pair<bool, std::string> GetFirst(const std::string& key)const;
};


int DoCmdASS(const CommandLineArgCollector& args);
int DoCmdDaemon(const CommandLineArgCollector& args);
int DoCmdIndex(const CommandLineArgCollector& args);
int DoCmdConfig(const CommandLineArgCollector& args);