#pragma once

#include "Common.h"

#include <string>
#include <vector>

std::vector<std::wstring> Deduplicate(const std::vector<std::wstring>& input, const std::vector<uint64_t>& inputSize, std::atomic<size_t>& progress);
