#include "EncFSCloudConflict.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace EncFS {

namespace {

ConflictSuffixResult tryExtractDropboxConflict(const std::string& name) {
    ConflictSuffixResult result = { "", "", false };

    size_t parenPos = name.rfind('(');
    if (parenPos == std::string::npos || parenPos == 0) {
        return result;
    }

    size_t closePos = name.find(')', parenPos);
    if (closePos == std::string::npos) {
        return result;
    }

    std::string parenContent = name.substr(parenPos + 1, closePos - parenPos - 1);
    if (parenContent.find("conflict") == std::string::npos) {
        return result;
    }

    size_t suffixStart = parenPos;
    if (suffixStart > 0 && name[suffixStart - 1] == ' ') {
        --suffixStart;
    }

    std::string beforeParen = name.substr(0, suffixStart);
    std::string parenPart = name.substr(suffixStart, closePos - suffixStart + 1);
    std::string afterParen = name.substr(closePos + 1);

    result.core = beforeParen + afterParen;
    while (!result.core.empty() && result.core.back() == ' ') {
        result.core.pop_back();
    }

    result.suffix = parenPart;
    if (!result.suffix.empty() && result.suffix[0] != ' ') {
        result.suffix.insert(result.suffix.begin(), ' ');
    }

    result.found = !result.core.empty() && !result.suffix.empty();
    return result;
}

ConflictSuffixResult tryExtractGoogleDriveConflict(const std::string& name) {
    ConflictSuffixResult result = { "", "", false };

    size_t confPos = name.rfind("_conf(");
    if (confPos == std::string::npos || confPos == 0) {
        return result;
    }

    size_t closePos = name.find(')', confPos);
    if (closePos == std::string::npos) {
        return result;
    }

    std::string numStr = name.substr(confPos + 6, closePos - confPos - 6);
    if (numStr.empty()) {
        return result;
    }
    for (char c : numStr) {
        if (!isdigit(static_cast<unsigned char>(c))) {
            return result;
        }
    }

    std::string beforeConf = name.substr(0, confPos);
    std::string confPart = name.substr(confPos, closePos - confPos + 1);
    std::string afterConf = name.substr(closePos + 1);

    result.core = beforeConf + afterConf;
    result.suffix = confPart;
    result.found = !result.core.empty();
    return result;
}

} // namespace

ConflictSuffixResult tryExtractCloudConflictSuffix(const std::string& name) {
    ConflictSuffixResult result = tryExtractDropboxConflict(name);
    if (result.found) {
        return result;
    }

    result = tryExtractGoogleDriveConflict(name);
    return result;
}

std::string insertConflictSuffix(const std::string& decoded, const std::string& suffix) {
    // Insert conflict suffix before extension when an extension exists.
    // This matches the expected plaintext naming convention used by OneDrive-style conflicts
    // in this project (e.g., "file-DESKTOP-123.xlsx").
    size_t dotPos = decoded.find_last_of('.');
    if (dotPos != std::string::npos && dotPos != 0) {
        std::string result = decoded;
        result.insert(dotPos, suffix);
        return result;
    }

    return decoded + suffix;
}

} // namespace EncFS
