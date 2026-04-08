#pragma once

#include "log_file.h"
#include "filter_engine.h"

#include <string>

// Write the currently filtered lines to output_path, one per line.
// Returns true on success, false on failure (bad path, write error, etc).
bool export_filtered_lines(const LogFile& file,
                           const FilterEngine& engine,
                           const std::string& output_path);
