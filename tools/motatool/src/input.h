// Read a firmware blob from a local file OR an http(s):// URL. URLs are fetched with the system `curl`
// (or `wget`) binary — no link-time dependency, so the tool stays self-contained and builds anywhere.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mota {

// Returns "" on success (fills `out`), else an error string. `what` is a label for error messages.
std::string read_input(const std::string& path_or_url, std::vector<uint8_t>& out);

bool is_url(const std::string& s);

} // namespace mota
