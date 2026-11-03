#include "Credentials.h"
#include <windows.h>
#include <wincred.h>

namespace {
constexpr wchar_t kTarget[] = L"TypeTide/OpenAICompatibleApiKey";
}

namespace credentials {

LoadResult LoadCloudApiKey(std::string* key) {
    key->clear();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kTarget, CRED_TYPE_GENERIC, 0, &credential))
        return GetLastError() == ERROR_NOT_FOUND ? LoadResult::NotFound : LoadResult::Error;

    if (credential->CredentialBlob && credential->CredentialBlobSize) {
        const char* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        key->assign(bytes, bytes + credential->CredentialBlobSize);
    }
    CredFree(credential);
    return LoadResult::Found;
}

bool SaveCloudApiKey(const std::string& key) {
    if (key.empty()) {
        return CredDeleteW(kTarget, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
    }

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(kTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"TypeTide");
    return CredWriteW(&credential, 0) != FALSE;
}

} // namespace credentials
