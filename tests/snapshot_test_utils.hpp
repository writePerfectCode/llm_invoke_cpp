#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#ifndef LLM_INVOKE_CPP_SOURCE_DIR
#define LLM_INVOKE_CPP_SOURCE_DIR "."
#endif

namespace test_support {

inline std::string normalizeSnapshotText(std::string text)
{
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '\r')
        {
            continue;
        }

        normalized.push_back(text[index]);
    }

    if (!normalized.empty() && normalized.back() == '\n')
    {
        normalized.pop_back();
    }

    return normalized;
}

inline std::filesystem::path snapshotPath(std::string_view relative_path)
{
    return std::filesystem::path(LLM_INVOKE_CPP_SOURCE_DIR) / "tests" / "snapshots" / relative_path;
}

inline std::string readSnapshotText(std::string_view relative_path)
{
    const auto path = snapshotPath(relative_path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("failed to open snapshot file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return normalizeSnapshotText(buffer.str());
}

inline void checkSnapshot(std::string_view relative_path, const std::string& actual_text)
{
    INFO("snapshot", snapshotPath(relative_path).string());
    CHECK(readSnapshotText(relative_path) == normalizeSnapshotText(actual_text));
}

} // namespace test_support