#pragma once

#include <string>

namespace EncFS {

// Cloud conflict suffix detection result
struct ConflictSuffixResult {
    std::string core;   // Filename without conflict suffix
    std::string suffix; // The conflict suffix (including leading space/separator)
    bool found;         // Whether a conflict suffix was detected
};

ConflictSuffixResult tryExtractCloudConflictSuffix(const std::string& name);
std::string insertConflictSuffix(const std::string& decoded, const std::string& suffix);

} // namespace EncFS
