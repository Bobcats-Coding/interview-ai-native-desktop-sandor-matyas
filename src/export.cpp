#include "export.h"

#include <fstream>

bool export_filtered_lines(const LogFile& file,
                           const FilterEngine& engine,
                           const std::string& output_path) {
    if (output_path.empty()) return false;

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) return false;

    const auto& results = engine.current_results();
    for (uint32_t idx : results.matching_indices) {
        auto line = file.line(idx);
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
        out.put('\n');
    }

    return out.good();
}
