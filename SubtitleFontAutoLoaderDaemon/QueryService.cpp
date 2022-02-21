#include "pch.h"

#include "Common.h"
#include "QueryService.h"
#include "RpcServer.h"

#include <wil/resource.h>

namespace
{
	template <typename T, bool AllowDuplicate = true>
	class QueryTrie
	{
	private:
		struct TrieNode
		{
			std::vector<std::pair<std::wstring, std::unique_ptr<TrieNode>>> m_branch;
			std::vector<T*> m_data;

			void CollectData(std::vector<T*>& ret)
			{
				ret.insert(ret.end(), m_data.begin(), m_data.end());
				for (auto& branch : m_branch)
				{
					branch.second->CollectData(ret);
				}
			}

			void SortList()
			{
				std::sort(m_branch.begin(), m_branch.end());
			}

			std::pair<std::wstring, std::unique_ptr<TrieNode>>* SearchPrefix(wchar_t leading)
			{
				auto left = m_branch.begin();
				auto right = m_branch.end();
				auto mid = left + (right - left) / 2;
				auto result = right;
				while (left != right)
				{
					assert(!mid->first.empty());
					if (mid->first[0] == leading)
					{
						result = mid;
						break;
					}
					else if (mid->first[0] > leading)
					{
						right = mid;
					}
					else
					{
						left = mid + 1;
					}
					mid = left + (right - left) / 2;
				}
				if (result == m_branch.end())
					return nullptr;
				return &(*result);
			}
		};

		TrieNode m_rootNode;
	public:
		void AddEntry(const wchar_t* key, T* value)
		{
			const wchar_t* keyPointer = key;
			TrieNode* node = &m_rootNode;
			// find an arc in list with common prefix of key
			// just search first character
			while (node)
			{
				assert(*keyPointer != 0);
				auto result = node->SearchPrefix(*keyPointer);
				if (result == nullptr)
				{
					// not found in current list
					// create new node in list
					auto newNode = std::make_unique<TrieNode>();
					newNode->m_data.push_back(value);
					node->m_branch.emplace_back(keyPointer, std::move(newNode));
					node->SortList();
					return;
				}
				else
				{
					// found an arc with common prefix
					// advance keyPointer
					auto arcPointer = result->first.c_str();
					while (*keyPointer && *arcPointer && *keyPointer == *arcPointer)
					{
						++arcPointer;
						++keyPointer;
					}
					if (*arcPointer == 0 && *keyPointer == 0)
					{
						// duplicate entry
						if constexpr (AllowDuplicate)
						{
							// append entry
							result->second->m_data.push_back(value);
						}
						return;
					}
					else if (*arcPointer == 0)
					{
						// found key is a prefix of new key
						node = result->second.get();
					}
					else if (*keyPointer == 0)
					{
						// new key is a prefix of found key
						// split arc
						auto keyLength = arcPointer - result->first.c_str();
						auto newNode = std::make_unique<TrieNode>();
						newNode->m_data.push_back(value);
						newNode->m_branch.emplace_back(arcPointer, std::move(result->second));
						result->first.resize(keyLength);
						result->second = std::move(newNode);
						node->SortList();
						return;
					}
					else
					{
						// found key and new key has common part but not equal
						// split arc
						auto keyLength = arcPointer - result->first.c_str();
						auto intermediateNode = std::make_unique<TrieNode>();
						auto newNode = std::make_unique<TrieNode>();
						newNode->m_data.push_back(value);
						intermediateNode->m_branch.emplace_back(arcPointer, std::move(result->second));
						intermediateNode->m_branch.emplace_back(keyPointer, std::move(newNode));
						intermediateNode->SortList();
						result->first.resize(keyLength);
						result->second = std::move(intermediateNode);
						node->SortList();
						return;
					}
				}
			}
		}

		std::vector<T*> QueryEntry(const wchar_t* key, bool truncated)
		{
			std::vector<T*> ret;
			const wchar_t* keyPointer = key;
			TrieNode* node = &m_rootNode;
			// find an arc in list with common prefix of key
			// just search first character
			while (node)
			{
				assert(*keyPointer != 0);
				auto result = node->SearchPrefix(*keyPointer);
				if (result == nullptr)
				{
					// not found
					return ret;
				}
				else
				{
					auto arcPointer = result->first.c_str();
					while (*keyPointer && *arcPointer && *keyPointer == *arcPointer)
					{
						++arcPointer;
						++keyPointer;
					}
					if (*arcPointer == 0 && *keyPointer == 0)
					{
						// exact match
						if (truncated)
						{
							result->second->CollectData(ret);
						}
						else
						{
							ret.insert(ret.end(), result->second->m_data.begin(), result->second->m_data.end());
						}
						return ret;
					}
					else if (*arcPointer == 0)
					{
						// found key is a prefix of new key
						node = result->second.get();
					}
					else if (*keyPointer == 0)
					{
						// not found, we ran out of key
						if (truncated)
						{
							result->second->CollectData(ret);
						}
						return ret;
					}
					else
					{
						// not found
						return ret;
					}
				}
			}
			return ret;
		}
	};
}

class sfh::QueryService::Implementation : public sfh::IRpcRequestHandler
{
private:
	std::mutex m_accessLock;

	QueryTrie<FontDatabase::FontFaceElement, false> m_fullName;
	QueryTrie<FontDatabase::FontFaceElement, false> m_postScriptName;
	QueryTrie<FontDatabase::FontFaceElement, true> m_win32FamilyName;
	std::vector<std::unique_ptr<FontDatabase>> m_dbs;

	IDaemon* m_daemon;

	wil::unique_handle m_version;
	wil::unique_mapview_ptr<uint32_t> m_versionMem;
public:
	Implementation(IDaemon* daemon)
		: m_daemon(daemon)
	{
		std::wstring versionShmName = L"SubtitleFontAutoLoaderSHM-";
		versionShmName += GetCurrentProcessUserSid();
		m_version.reset(CreateFileMappingW(
			INVALID_HANDLE_VALUE,
			nullptr,
			PAGE_READWRITE,
			0, 4,
			versionShmName.c_str()));
		THROW_LAST_ERROR_IF(!m_version.is_valid());
		m_versionMem.reset(static_cast<uint32_t*>(MapViewOfFile(
			m_version.get(),
			FILE_MAP_WRITE,
			0, 0,
			sizeof(uint32_t))));
		THROW_LAST_ERROR_IF(m_versionMem.get() == nullptr);
	}

	void UpdateVerison()
	{
		InterlockedIncrement(m_versionMem.get());
	}

	void Load(std::vector<std::unique_ptr<FontDatabase>>&& dbs)
	{
		QueryTrie<FontDatabase::FontFaceElement, true> win32FamilyName;
		QueryTrie<FontDatabase::FontFaceElement, false> fullName;
		QueryTrie<FontDatabase::FontFaceElement, false> postScriptName;
		for (auto& db : dbs)
		{
			for (auto& font : db->m_fonts)
			{
				for (auto& name : font.m_names)
				{
					if (name.m_type == name.Win32FamilyName)
					{
						win32FamilyName.AddEntry(name.m_name.c_str(), &font);
					}
					else if (name.m_type == name.FullName)
					{
						fullName.AddEntry(name.m_name.c_str(), &font);
					}
					else if (name.m_type == name.PostScriptName)
					{
						postScriptName.AddEntry(name.m_name.c_str(), &font);
					}
				}
			}
		}
		std::lock_guard lg(m_accessLock);
		m_dbs = std::move(dbs);
		m_win32FamilyName = std::move(win32FamilyName);
		m_fullName = std::move(fullName);
		m_postScriptName = std::move(postScriptName);
		UpdateVerison();
	}

	std::vector<std::reference_wrapper<std::wstring>> HandleRequest(const std::wstring& str) override
	{
		std::lock_guard lg(m_accessLock);
		std::vector<std::reference_wrapper<std::wstring>> ret;
		std::vector<FontDatabase::FontFaceElement*> tmp;
		bool doTruncated = false;
		// enable truncated query for GDI LOGFONT::lfFaceName's 31 wchar_t limit
		if (str.size() == 31)
			doTruncated = true;
		auto family = m_win32FamilyName.QueryEntry(str.c_str(), doTruncated);
		auto fullname = m_fullName.QueryEntry(str.c_str(), doTruncated);
		auto postscript = m_postScriptName.QueryEntry(str.c_str(), doTruncated);
		tmp.reserve(family.size() + fullname.size() + postscript.size());
		tmp.insert(tmp.end(), family.begin(), family.end());
		tmp.insert(tmp.end(), fullname.begin(), fullname.end());
		tmp.insert(tmp.end(), postscript.begin(), postscript.end());
		// deduplicate, preserve original order
		for (auto item : tmp)
		{
			if (std::find_if(ret.begin(), ret.end(),
			                 [&](auto& path) { return path.get() == item->m_path; }) != ret.end())
				continue;
			ret.emplace_back(std::ref(item->m_path));
		}
		return ret;
	}

	IRpcRequestHandler* GetRpcRequestHandler()
	{
		return this;
	}
};

sfh::QueryService::QueryService(IDaemon* daemon)
	: m_impl(std::make_unique<Implementation>(daemon))
{
}

sfh::QueryService::~QueryService() = default;

void sfh::QueryService::Load(std::vector<std::unique_ptr<FontDatabase>>&& dbs)
{
	m_impl->Load(std::move(dbs));
}

sfh::IRpcRequestHandler* sfh::QueryService::GetRpcRequestHandler()
{
	return m_impl->GetRpcRequestHandler();
}
