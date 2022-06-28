#include "FileDeduplicate.h"
#include "Win32Helper.h"
#include "ConsoleHelper.h"

#include <string_view>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <queue>

#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#include <wil/resource.h>

extern DWORD g_WorkerCount;

namespace
{
	constexpr size_t SHA1_DIGEST_LENGTH = 20;
	constexpr size_t FILE_BUFFER_SIZE = 8 * 1024 * 1024; // 8 MiB

	struct BCryptContext
	{
		wil::unique_bcrypt_algorithm hAlg;
		wil::unique_bcrypt_hash hHash;

		std::unique_ptr<uint8_t[]> hashObject;
		DWORD hashObjectLength = 0;
		std::unique_ptr<uint8_t[]> hash;
		DWORD hashLength = 0;

		wil::unique_virtualalloc_ptr<uint8_t> fileBuffer;

		BCryptContext()
		{
			fileBuffer.reset(
				static_cast<uint8_t*>(
					THROW_LAST_ERROR_IF_NULL(VirtualAlloc(
						nullptr,
						FILE_BUFFER_SIZE,
						MEM_COMMIT | MEM_RESERVE,
						PAGE_READWRITE))));

			THROW_IF_NTSTATUS_FAILED(BCryptOpenAlgorithmProvider(
				hAlg.put(),
				BCRYPT_SHA1_ALGORITHM,
				nullptr,
				BCRYPT_HASH_REUSABLE_FLAG
			));

			ULONG result = 0;

			THROW_IF_NTSTATUS_FAILED(BCryptGetProperty(
				hAlg.get(),
				BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&hashObjectLength),
				sizeof(hashObjectLength),
				&result, 0
			));
			hashObject = std::make_unique<uint8_t[]>(hashObjectLength);

			THROW_IF_NTSTATUS_FAILED(BCryptGetProperty(
				hAlg.get(),
				BCRYPT_HASH_LENGTH,
				reinterpret_cast<PUCHAR>(&hashLength),
				sizeof(hashLength),
				&result, 0
			));
			hash = std::make_unique<uint8_t[]>(hashLength);

			THROW_IF_NTSTATUS_FAILED(BCryptCreateHash(
				hAlg.get(),
				hHash.put(),
				hashObject.get(),
				hashObjectLength,
				nullptr,
				0,
				BCRYPT_HASH_REUSABLE_FLAG
			));
		}

		void CalculateSHA1(const wchar_t* path, uint8_t* out) const
		{
			{
				auto doneHash = wil::scope_exit([&]()
				{
					if (BCryptFinishHash(hHash.get(), hash.get(), hashLength, 0))
					{
						// do nothing
					}
				});

				wil::unique_hfile hFile(
					CreateFileW(
						path,
						GENERIC_READ,
						FILE_SHARE_READ,
						nullptr,
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
						nullptr)
				);
				THROW_LAST_ERROR_IF(!hFile.is_valid());

				DWORD readBytes = 0;
				while (
					THROW_IF_WIN32_BOOL_FALSE(
						ReadFile(hFile.get(),fileBuffer.get(),FILE_BUFFER_SIZE,&readBytes,nullptr)))
				{
					if (readBytes == 0)
						break;
					THROW_IF_NTSTATUS_FAILED(BCryptHashData(
						hHash.get(),
						fileBuffer.get(),
						readBytes,
						0));
				}
			}
			memcpy(out, hash.get(), SHA1_DIGEST_LENGTH);
		}
	};

	struct FileRecord
	{
		uint8_t sha1[SHA1_DIGEST_LENGTH];
		const std::wstring* path;

		bool operator<(const FileRecord& rhs) const
		{
			return memcmp(sha1, rhs.sha1, SHA1_DIGEST_LENGTH) < 0;
		}

		bool operator==(const FileRecord& rhs) const
		{
			return memcmp(sha1, rhs.sha1, SHA1_DIGEST_LENGTH) == 0;
		}
	};

	struct RecordStorage
	{
		static constexpr size_t RECORD_MAX_CAPACITY = 5;
		FileRecord records[RECORD_MAX_CAPACITY];
		size_t nextIndex = 0;
		std::unique_ptr<std::vector<FileRecord>> vec;
	};
}

std::vector<std::wstring> Deduplicate(const std::vector<std::wstring>& input, const std::vector<uint64_t>& inputSize,
                                      std::atomic<size_t>& progress)
{
	std::vector<std::wstring> ret;
	std::unordered_map<uint64_t, RecordStorage> lookupTable;

	ret.reserve(input.size());

	for (size_t idx = 0; idx < input.size(); ++idx)
	{
		auto& file = input[idx];
		auto fileLength = inputSize[idx];
		auto& storage = lookupTable[fileLength];
		if (storage.nextIndex != RecordStorage::RECORD_MAX_CAPACITY)
		{
			storage.records[storage.nextIndex++].path = &file;
		}
		else
		{
			if (!storage.vec)
			{
				storage.vec = std::make_unique<std::vector<FileRecord>>(
					storage.records,
					storage.records + storage.nextIndex
				);
			}
			storage.vec->emplace_back();
			storage.vec->back().path = &file;
		}
	}

	std::mutex queueMutex;
	std::queue<FileRecord*> needHash;

	for (auto& group : lookupTable)
	{
		if (group.second.nextIndex == 1)
		{
			ret.emplace_back(*group.second.records[0].path);
		}
		else
		{
			if (group.second.vec)
			{
				for (auto& item : *group.second.vec)
					needHash.push(&item);
			}
			else
			{
				for (size_t i = 0; i < group.second.nextIndex; ++i)
					needHash.push(&group.second.records[i]);
			}
		}
	}

	progress += ret.size();

	std::vector<std::thread> workers;
	for (size_t i = 0; i < g_WorkerCount; ++i)
	{
		workers.emplace_back([&]()
		{
			BCryptContext context;
			std::unique_lock lock(queueMutex);
			while (!needHash.empty())
			{
				if (g_cancelToken)
					return;
				auto item = needHash.front();
				needHash.pop();
				lock.unlock();
				try
				{
					context.CalculateSHA1(item->path->c_str(), item->sha1);
				}
				catch (std::exception& e)
				{
					EraseLineStruct::EraseLine();
					std::cout << SetOutputRed << e.what() << std::endl << SetOutputDefault;
					item->path = nullptr;
				}
				++progress;
				lock.lock();
			}
		});
	}
	for (auto& thr : workers)
	{
		if (thr.joinable())
			thr.join();
	}
	if (g_cancelToken)
		return {};

	for (auto& group : lookupTable)
	{
		auto& storage = group.second;
		if (storage.nextIndex <= 1)
			continue;
		if (storage.vec)
		{
			std::sort(storage.vec->begin(), storage.vec->end());
			auto last = std::unique(storage.vec->begin(), storage.vec->end());
			for (auto it = storage.vec->begin(); it != last; ++it)
			{
				if (!it->path)
					continue;
				ret.emplace_back(*it->path);
			}
		}
		else
		{
			std::sort(storage.records, storage.records + storage.nextIndex);
			auto last = std::unique(storage.records, storage.records + storage.nextIndex);
			for (auto it = storage.records; it != last; ++it)
			{
				if (!it->path)
					continue;
				ret.emplace_back(*it->path);
			}
		}
	}

	return ret;
}
