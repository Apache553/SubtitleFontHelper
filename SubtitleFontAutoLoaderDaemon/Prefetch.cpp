#include "pch.h"

#include <filesystem>

#include "Prefetch.h"
#include "Common.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


template <typename T>
class SimpleLRU
{
private:
	struct Node
	{
		Node* m_prev;
		Node* m_next;

		T m_data;

		// detach a node from chain, return next node
		static Node& Detach(Node& node)
		{
			if (node.m_prev)
			{
				node.m_prev->m_next = node.m_next;
			}
			node.m_next->m_prev = node.m_prev;
			return *node.m_next;
		}

		// attach node before 'pos',return the attached node
		static Node& Attach(Node& node, Node& pos)
		{
			node.m_next = &pos;
			node.m_prev = pos.m_prev;
			pos.m_prev = &node;
			if (node.m_prev)
				node.m_prev->m_next = &node;
			return node;
		}
	};

	Node m_end{nullptr, nullptr};
	std::vector<Node> m_nodes;
	Node* m_head = &m_end;

	std::unordered_map<T, Node*> m_hashmap;

	size_t m_capacity;

	std::mutex m_lock;

	void AdjustToHead(Node& node, bool inList)
	{
		if (inList)
		{
			if (&node == m_head)
				return;
			Node::Detach(node);
		}
		Node::Attach(node, *m_head);
		m_head = &node;
	}

public:
	SimpleLRU(size_t initialSize)
		: m_capacity(initialSize)
	{
		m_nodes.reserve(initialSize);
	}

	bool Put(T&& value)
	{
		std::lock_guard lg(m_lock);
		auto findResult = m_hashmap.find(value);
		if (m_hashmap.end() == findResult)
		{
			// not found
			if (m_nodes.size() == m_capacity)
			{
				// full
				auto lastNode = m_end.m_prev;
				m_hashmap.erase(lastNode->m_data);
				m_hashmap.insert({value, lastNode});
				lastNode->m_data = std::move(value);
				AdjustToHead(*lastNode, true);
			}
			else
			{
				m_nodes.emplace_back();
				auto node = &m_nodes.back();
				m_hashmap.insert({value, node});
				node->m_data = std::move(value);
				AdjustToHead(*node, false);
			}
			return true;
		}
		else
		{
			AdjustToHead(*findResult->second, true);
			return false;
		}
	}

	bool Put(const T& value)
	{
		T v = value;
		return Put(std::move(v));
	}

	std::vector<T> GetVector()
	{
		std::lock_guard lg(m_lock);
		std::vector<T> ret;
		for (Node* node = m_head; node != &m_end; node = node->m_next)
		{
			ret.emplace_back(node->m_data);
		}
		return ret;
	}
};

class sfh::Prefetch::Implementation : public sfh::IRpcFeedbackHandler
{
	IDaemon* m_daemon;
	SimpleLRU<std::wstring> m_lru;

	std::wstring m_cachePath;

public:
	Implementation(IDaemon* daemon, size_t prefetchCount, const std::wstring& lruPath)
		: m_lru(prefetchCount), m_cachePath(lruPath)
	{
		LoadLruCache(m_cachePath);
	}

	~Implementation()
	{
		SaveLruCache(m_cachePath);
	}

	void Load(const std::wstring& path)
	{
		if (m_lru.Put(path))
		{
			AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
		}
	}

private:
	void LoadLruCache(const std::filesystem::path& path)
	{
		std::ifstream input(path);
		if (!input.is_open())
			return;
		std::string line;
		while (!input.eof())
		{
			std::getline(input, line);
			if (line.empty())
				continue;
			auto wideLine = Utf8ToWideString(line);
			Load(wideLine);
		}
	}

	void SaveLruCache(const std::filesystem::path& path)
	{
		std::ofstream output(path, std::ios::out);
		if (!output.is_open())
			return;
		auto snapshot = m_lru.GetVector();
		for (auto iter = snapshot.rbegin(); iter != snapshot.rend(); ++iter)
		{
			auto line = WideToUtf8String(*iter);
			output << line << '\n';
		}
	}

public:
	void HandleFeedback(const FontQueryRequest& request) override
	{
		const auto& data = request.feedbackdata();
		for (const auto& item : data.path())
		{
			auto path = Utf8ToWideString(item);
			Load(path);
		}
	}

	IRpcFeedbackHandler* GetRpcFeedbackHandler()
	{
		return this;
	}
};

sfh::Prefetch::Prefetch(IDaemon* daemon, size_t prefetchCount, const std::wstring& lruPath)
	: m_impl(std::make_unique<Implementation>(daemon, prefetchCount, lruPath))
{
}

sfh::Prefetch::~Prefetch() = default;

sfh::IRpcFeedbackHandler* sfh::Prefetch::GetRpcFeedbackHandler()
{
	return m_impl->GetRpcFeedbackHandler();
}
