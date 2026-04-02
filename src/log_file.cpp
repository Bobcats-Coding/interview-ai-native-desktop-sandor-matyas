#include "log_file.h"

#include <fstream>
#include <cstring>

// ── Encoding helpers ──────────────────────────────────────────────────────────

// Check if a byte sequence is valid UTF-8. Returns false on first invalid sequence.
static bool is_valid_utf8(const char* data, size_t len) {
    const auto* p   = reinterpret_cast<const uint8_t*>(data);
    const auto* end = p + len;

    while (p < end) {
        if (*p < 0x80) {
            ++p;
        } else if ((*p & 0xE0) == 0xC0) {
            if (p + 1 >= end || (p[1] & 0xC0) != 0x80) return false;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            if (p + 2 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            if (p + 3 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
            p += 4;
        } else {
            return false;
        }
    }
    return true;
}

// Convert Latin-1 buffer to UTF-8 in-place.
static void latin1_to_utf8(std::string& buf) {
    // First pass: count how many bytes we need
    size_t extra = 0;
    for (unsigned char c : buf) {
        if (c >= 0x80) ++extra;
    }
    if (extra == 0) return; // already ASCII-safe

    size_t old_size = buf.size();
    buf.resize(old_size + extra);

    // Second pass: convert backwards to avoid overwriting unprocessed bytes
    auto* src = reinterpret_cast<uint8_t*>(buf.data()) + old_size;
    auto* dst = reinterpret_cast<uint8_t*>(buf.data()) + old_size + extra;

    while (src != reinterpret_cast<uint8_t*>(buf.data())) {
        --src;
        if (*src >= 0x80) {
            --dst; *dst = 0x80 | (*src & 0x3F);
            --dst; *dst = 0xC0 | (*src >> 6);
        } else {
            --dst; *dst = *src;
        }
    }
}

// Skip BOM if present at start of buffer. Returns number of bytes to skip.
static size_t detect_and_skip_bom(const std::string& buf) {
    if (buf.size() >= 3 &&
        static_cast<uint8_t>(buf[0]) == 0xEF &&
        static_cast<uint8_t>(buf[1]) == 0xBB &&
        static_cast<uint8_t>(buf[2]) == 0xBF) {
        return 3; // UTF-8 BOM
    }
    return 0;
}

// ── Line offset building ──────────────────────────────────────────────────────

static std::vector<uint32_t> build_line_offsets(const std::string& buffer) {
    std::vector<uint32_t> offsets;
    offsets.reserve(buffer.size() / 60); // rough estimate: 60 chars per line

    offsets.push_back(0);
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == '\n') {
            offsets.push_back(static_cast<uint32_t>(i + 1));
        }
    }

    // If file doesn't end with newline, add sentinel at end
    if (!buffer.empty() && buffer.back() != '\n') {
        offsets.push_back(static_cast<uint32_t>(buffer.size()));
    }

    // Remove trailing empty line if file ends with newline
    // (the last offset == buffer.size() is the sentinel, not a real line)
    // offsets already has sentinel from the \n scan

    return offsets;
}

// ── Public API ────────────────────────────────────────────────────────────────

LogFile load_log_file(const std::string& path, std::string& out_error) {
    LogFile result;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        out_error = "Could not open file: " + path;
        return result;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        out_error = "File is empty.";
        return result;
    }
    if (file_size > 500LL * 1024 * 1024) {
        out_error = "File exceeds 500MB limit.";
        return result;
    }

    file.seekg(0, std::ios::beg);
    result.buffer.resize(static_cast<size_t>(file_size));
    file.read(result.buffer.data(), file_size);

    if (!file) {
        out_error = "Failed to read file.";
        result.buffer.clear();
        return result;
    }

    // Handle BOM
    size_t bom_skip = detect_and_skip_bom(result.buffer);
    if (bom_skip > 0) {
        result.buffer.erase(0, bom_skip);
    }

    // Encoding detection: check first 8KB
    size_t sample_len = std::min(result.buffer.size(), size_t(8192));
    if (!is_valid_utf8(result.buffer.data(), sample_len)) {
        // Assume Latin-1 and convert
        latin1_to_utf8(result.buffer);
    }

    // Build line offset table
    result.offsets = build_line_offsets(result.buffer);
    result.file_path = path;

    return result;
}
