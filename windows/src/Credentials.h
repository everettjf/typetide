#pragma once

#include <string>

namespace credentials {

enum class LoadResult {
    Found,     // key read successfully
    NotFound,  // Credential Manager says no such credential
    Error,     // read failed for another reason (e.g. vault not ready right after logon)
};

LoadResult LoadCloudApiKey(std::string* key);
bool SaveCloudApiKey(const std::string& key);

} // namespace credentials
