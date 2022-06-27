#include "FileDeduplicate.h"
#include "Win32Helper.h"
#include "ConsoleHelper.h"

#include <string_view>
#include <unordered_map>
#include <stdexcept>
#include <memory>

#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#include <wil/resource.h>


static constexpr size_t SHA1_DIGEST_LENGTH = 20;

namespace
{
	struct BCryptContext
	{
		wil::unique_bcrypt_algorithm hAlg;
		wil::unique_bcrypt_hash hHash;

		std::unique_ptr<uint8_t[]> hashObject;
		DWORD hashObjectLength = 0;
		std::unique_ptr<uint8_t[]> hash;
		DWORD hashLength = 0;

		BCryptContext()
		{
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

		void CalculateSHA1(const FileMapping& mapping, uint8_t* out) const
		{
			{
				auto doneHash = wil::scope_exit([&]()
				{
					auto _ = BCryptFinishHash(hHash.get(), hash.get(), hashLength, 0);
				});

				THROW_IF_NTSTATUS_FAILED(BCryptHashData(
					hHash.get(),
					static_cast<PUCHAR>(mapping.GetMappedPointer()),
					static_cast<ULONG>(mapping.GetFileLength()),
					0));
			}
			memcpy(out, hash.get(), SHA1_DIGEST_LENGTH);
		}
	};

	struct FileRecord
	{
		uint8_t sha1[SHA1_DIGEST_LENGTH];
		const std::wstring* path;
	};

	struct RecordChainStorage
	{
		static constexpr size_t RECORD_MAX_CAPACITY = 4;
		FileRecord records[RECORD_MAX_CAPACITY];
		bool lazy = true;
		size_t nextIndex = 0;
		std::unique_ptr<RecordChainStorage> next;
	};
}

std::vector<std::wstring> Deduplicate(const std::vector<std::wstring>& input, const std::vector<uint64_t>& inputSize,
                                      std::atomic<size_t>& progress)
{
	std::unordered_map<uint64_t, RecordChainStorage> lookupTable;
	BCryptContext context;
	FileRecord tRecord;
	size_t addedCount = 0;

	for (size_t idx = 0; idx < input.size(); ++idx)
	{
		auto& file = input[idx];
		auto fileLength = inputSize[idx];
		if (g_cancelToken)
			return {};
		try
		{
			auto& records = lookupTable[fileLength];
			if (records.nextIndex == 0)
			{
				// simply insert
				records.records[records.nextIndex].path = &file;
				++records.nextIndex;
				++addedCount;
			}
			else if (records.nextIndex > 0)
			{
				if (records.lazy)
				{
					// calc sha1 for lazy file
					FileMapping sMapping(records.records[0].path->c_str());
					context.CalculateSHA1(sMapping, records.records[0].sha1);
					records.lazy = false;
				}
				FileMapping mapping(file.c_str());
				context.CalculateSHA1(mapping, tRecord.sha1);
				RecordChainStorage* storage = &records;
				bool hit = false;
				for (size_t i = 0; i < storage->nextIndex; ++i)
				{
					if (memcmp(tRecord.sha1, storage->records[i].sha1, SHA1_DIGEST_LENGTH) == 0)
					{
						hit = true;
						break; // find same
					}
					if (i == RecordChainStorage::RECORD_MAX_CAPACITY - 1)
					{
						// reached last element
						if (storage->next)
						{
							storage = storage->next.get();
							i = -1;
						}
					}
				}
				if (!hit)
				{
					// insert into
					if (storage->nextIndex == RecordChainStorage::RECORD_MAX_CAPACITY)
					{
						storage->next = std::make_unique<RecordChainStorage>();
						storage = storage->next.get();
					}
					auto& thisRecord = storage->records[storage->nextIndex++];
					memcpy(thisRecord.sha1, tRecord.sha1, SHA1_DIGEST_LENGTH);
					thisRecord.path = &file;
					++addedCount;
				}
			}
		}
		catch (std::exception& e)
		{
			EraseLineStruct::EraseLine();
			std::cout << SetOutputRed << e.what() << std::endl << SetOutputDefault;
		}
		++progress;
	}

	// prepare return value
	std::vector<std::wstring> ret;
	ret.reserve(addedCount);
	for (auto& records : lookupTable)
	{
		RecordChainStorage* storage = &records.second;
		for (size_t i = 0; i < storage->nextIndex; ++i)
		{
			ret.emplace_back(*storage->records[i].path);
			if (i == RecordChainStorage::RECORD_MAX_CAPACITY - 1)
			{
				// reached last element
				if (storage->next)
				{
					storage = storage->next.get();
					i = -1;
				}
			}
		}
	}

	return ret;
}
