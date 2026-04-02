#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct LogFile {
    std::string          buffer;     // entire file content, UTF-8 normalized
    std::vector<uint32_t> offsets;   // byte offset of each line start; last entry = buffer.size()
    std::string          file_path;

    size_t line_count() const {
        return offsets.size() <= 1 ? 0 : offsets.size() - 1;
    }

    std::string_view line(size_t i) const {
        auto start = offsets[i];
        auto end   = offsets[i + 1];
        // strip trailing \n (and \r if present)
        if (end > start && buffer[end - 1] == '\n') --end;
        if (end > start && buffer[end - 1] == '\r') --end;
        return {buffer.data() + start, end - start};
    }
};

// Load a file into a LogFile. Returns empty LogFile on failure, with error message in out_error.
LogFile load_log_file(const std::string& path, std::string& out_error);
