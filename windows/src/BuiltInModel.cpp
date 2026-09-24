#include "BuiltInModel.h"
#include "Http.h"
#include "Json.h"
#include "Language.h"
#include "Util.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace builtin {
namespace {

constexpr wchar_t kModelName[] = L"translategemma-4b-it.Q4_K_M.gguf";
constexpr wchar_t kModelURL[] = L"https://huggingface.co/mradermacher/translategemma-4b-it-GGUF/resolve/"
    L"b89d835d2f2b802f50f3a30f1f963c7c43de4723/translategemma-4b-it.Q4_K_M.gguf";
constexpr char kSha256[] = "81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330";

std::mutex g_serverMutex;
HANDLE g_server = nullptr;
int g_port = 0;
std::wstring g_key;

struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET h = nullptr) : value(h) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    explicit operator bool() const { return value != nullptr; }
};

std::wstring dataDirectory() {
    wchar_t overridePath[32768]{};
    DWORD n = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", overridePath, 32768);
    if (n > 0 && n < 32768) return std::wstring(overridePath) + L"\\Models";
    wchar_t path[32768]{};
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32768);
    if (n == 0 || n >= 32768) return {};
    return std::wstring(path) + L"\\TypeTide\\Models";
}

std::wstring helperPath() {
    wchar_t path[32768]{};
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (n == 0 || n >= 32768) return {};
    return (std::filesystem::path(path).parent_path() / L"llama" / L"llama-server.exe").wstring();
}

bool hashMatches(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    bool ok = false;
    do {
        DWORD objectSize = 0, size = sizeof(objectSize);
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) break;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                              size, &size, 0) < 0) break;
        object.resize(objectSize);
        if (BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) break;
        std::vector<unsigned char> buffer(1024 * 1024);
        DWORD got = 0;
        bool readOk = true;
        for (;;) {
            if (!ReadFile(file, buffer.data(), (DWORD)buffer.size(), &got, nullptr)) { readOk = false; break; }
            if (!got) break;
            if (BCryptHashData(hash, buffer.data(), got, 0) < 0) { readOk = false; break; }
        }
        if (!readOk) break;
        unsigned char digest[32]{};
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) break;
        char hex[65]{};
        constexpr char digits[] = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) {
            hex[i * 2] = digits[digest[i] >> 4];
            hex[i * 2 + 1] = digits[digest[i] & 15];
        }
        ok = strcmp(hex, kSha256) == 0;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

int freeLoopbackPort() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) { WSACleanup(); return 0; }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    int len = sizeof(address);
    int port = 0;
    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), len) == 0 &&
        getsockname(socketHandle, reinterpret_cast<sockaddr*>(&address), &len) == 0)
        port = ntohs(address.sin_port);
    closesocket(socketHandle);
    WSACleanup();
    return port;
}

std::wstring newKey() {
    unsigned char bytes[24]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring key;
    for (unsigned char b : bytes) { key += digits[b >> 4]; key += digits[b & 15]; }
    return key;
}

std::wstring endpoint(const wchar_t* path) {
    return L"http://127.0.0.1:" + std::to_wstring(g_port) + path;
}

std::wstring authHeader() { return L"Authorization: Bearer " + g_key + L"\r\n"; }

bool ensureServer(const std::atomic<bool>& cancel, std::wstring* error) {
    std::lock_guard lock(g_serverMutex);
    if (g_server && WaitForSingleObject(g_server, 0) != WAIT_TIMEOUT) {
        CloseHandle(g_server);
        g_server = nullptr;
    }
    if (!g_server) {
        if (!Installed()) { *error = L"Download the built-in model in Settings → Translation."; return false; }
        const std::wstring exe = helperPath();
        if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
            *error = L"Bundled inference runtime is missing. Reinstall TypeTide.";
            return false;
        }
        g_port = freeLoopbackPort();
        g_key = newKey();
        if (!g_port || g_key.empty()) { *error = L"Cannot initialize local inference."; return false; }
        std::wstring command = L"\"" + exe + L"\" --model \"" + ModelPath() + L"\" --host 127.0.0.1 --port " +
            std::to_wstring(g_port) + L" --ctx-size 4096 --parallel 1 --n-gpu-layers 0 --no-webui --no-warmup"
            L" --no-jinja --chat-template gemma --sleep-idle-seconds 120 --api-key " + g_key;
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(0);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            *error = L"Cannot start bundled inference runtime.";
            return false;
        }
        CloseHandle(process.hThread);
        g_server = process.hProcess;
    }
    for (int attempt = 0; attempt < 600; ++attempt) {
        if (cancel.load()) { *error = L"Cancelled"; return false; }
        if (WaitForSingleObject(g_server, 0) != WAIT_TIMEOUT) {
            *error = L"Bundled inference runtime exited during model loading.";
            return false;
        }
        http::Result health = http::Get(endpoint(L"/health"));
        if (health.ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    *error = L"Timed out while loading the built-in model.";
    return false;
}

std::string promptFor(const TranslationRequest& request, std::wstring* error) {
    if (request.style != RewriteStyle::Faithful) {
        *error = L"Built-in translation supports Faithful only. Choose Ollama or API for other styles.";
        return {};
    }
    auto source = request.source ? request.source : lang::Detect(request.text);
    if (!source) {
        *error = L"Source language is unclear. Choose a fixed translation direction in Languages.";
        return {};
    }
    std::string from = lang::PromptName(*source), to = lang::PromptName(request.target);
    std::string result = "<bos><start_of_turn>user\nYou are a professional " + from + " (" + lang::Code(*source) +
        ") to " + to + " (" + lang::Code(request.target) + ") translator. Your goal is to accurately convey the meaning and nuances of the original " +
        from + " text while adhering to " + to + " grammar, vocabulary, and cultural sensitivities.\n" +
        "Produce only the " + to + " translation, without any additional explanations or commentary. Please translate the following " +
        from + " text into " + to + ":\n\n\n" + util::Narrow(util::Trim(request.text)) +
        "<end_of_turn>\n<start_of_turn>model\n";
    return result;
}

} // namespace

std::wstring ModelPath() {
    const std::wstring dir = dataDirectory();
    return dir.empty() ? std::wstring() : (std::filesystem::path(dir) / kModelName).wstring();
}

bool Installed() {
    const std::wstring path = ModelPath();
    if (path.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
    const int64_t size = (int64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return size == kModelBytes;
}

bool Download(const std::atomic<bool>& cancel,
              const std::function<void(double, const std::wstring&)>& progress,
              std::wstring* error) {
    if (Installed()) return true;
    const std::wstring path = ModelPath();
    if (path.empty()) { *error = L"Cannot locate local model directory."; return false; }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) { *error = L"Cannot create local model directory."; return false; }
    const std::wstring staging = path + L".download";
    if (std::filesystem::exists(staging) && hashMatches(staging)) {
        if (MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    }
    HANDLE file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { *error = L"Cannot write model download."; return false; }
    bool success = false;
    do {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256]{}, urlPath[2048]{};
        parts.lpszHostName = host; parts.dwHostNameLength = 256;
        parts.lpszUrlPath = urlPath; parts.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(kModelURL, 0, 0, &parts)) { *error = L"Invalid model URL."; break; }
        InternetHandle session(WinHttpOpen(L"TypeTide/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) { *error = L"Cannot start model download."; break; }
        WinHttpSetTimeouts(session.value, 10000, 10000, 60000, 60000);
        InternetHandle connection(WinHttpConnect(session.value, host, parts.nPort, 0));
        if (!connection) { *error = L"Cannot connect to model host."; break; }
        InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", urlPath, nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE));
        if (!request || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) {
            *error = L"Model download failed. Please retry."; break;
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) { *error = L"Model download returned HTTP " + std::to_wstring(status); break; }
        std::vector<char> buffer(1024 * 1024);
        int64_t received = 0;
        bool readOk = true;
        for (;;) {
            if (cancel.load()) { *error = L"Download cancelled."; readOk = false; break; }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available)) { readOk = false; break; }
            if (!available) break;
            while (available) {
                DWORD got = 0, written = 0;
                DWORD count = std::min<DWORD>(available, (DWORD)buffer.size());
                if (!WinHttpReadData(request.value, buffer.data(), count, &got) || !got ||
                    !WriteFile(file, buffer.data(), got, &written, nullptr) || written != got) {
                    readOk = false; break;
                }
                received += got;
                available -= got;
                progress(std::min(1.0, double(received) / double(kModelBytes)), L"Downloading TranslateGemma…");
            }
            if (!readOk) break;
        }
        if (!readOk) { if (error->empty()) *error = L"Model download interrupted. Please retry."; break; }
        if (received != kModelBytes) { *error = L"Model download size mismatch. Please retry."; break; }
        FlushFileBuffers(file);
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
        progress(1.0, L"Verifying SHA-256…");
        if (!hashMatches(staging)) { *error = L"Model SHA-256 mismatch. Please retry."; break; }
        if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            *error = L"Cannot finish model installation."; break;
        }
        success = true;
    } while (false);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!success) DeleteFileW(staging.c_str());
    return success;
}

bool Stream(const TranslationRequest& request, uint64_t id,
            const translator::DeltaFn& onDelta, const std::atomic<bool>& cancel,
            std::wstring* full, std::wstring* error) {
    if (request.text.size() > 16384) {
        *error = L"Selection is too long for the built-in model. Select a shorter passage.";
        return false;
    }
    const std::string prompt = promptFor(request, error);
    if (prompt.empty()) return false;
    if (!ensureServer(cancel, error)) return false;
    const std::wstring auth = authHeader();
    json::Object tokenize;
    tokenize["content"] = prompt;
    tokenize["add_special"] = false;
    tokenize["parse_special"] = true;
    std::string tokenResponse;
    auto tokensResult = http::PostStream(endpoint(L"/tokenize"), json::Value(std::move(tokenize)).dump(), auth,
        [&](const char* bytes, size_t count) { tokenResponse.append(bytes, count); }, cancel);
    if (!tokensResult.ok) { *error = L"Built-in tokenizer failed: " + util::Widen(tokensResult.error); return false; }
    bool parsed = false;
    json::Value tokens = json::Parse(tokenResponse, &parsed);
    const json::Array* tokenArray = tokens["tokens"].array();
    if (!parsed || !tokenArray) { *error = L"Built-in tokenizer returned invalid data."; return false; }
    if (tokenArray->size() > 2048) {
        *error = L"Selection is too long for the built-in model. Select a shorter passage.";
        return false;
    }
    json::Object body;
    // An integer token array avoids llama-server inserting a second BOS token.
    body["prompt"] = tokens["tokens"];
    body["stream"] = true;
    body["temperature"] = 0.0;
    body["n_predict"] = 2048;
    json::Array stops;
    stops.emplace_back("<end_of_turn>");
    body["stop"] = json::Value(std::move(stops));
    std::string lineBuffer;
    bool limit = false, truncated = false, sawStop = false;
    auto processLine = [&](std::string line) {
        if (line.ends_with('\r')) line.pop_back();
        if (line.rfind("data: ", 0) != 0) return;
        bool ok = false;
        json::Value event = json::Parse(line.substr(6), &ok);
        if (!ok) return;
        const std::string& delta = event["content"].asString();
        if (!delta.empty()) {
            std::wstring chunk = util::Widen(delta);
            *full += chunk;
            if (!cancel.load()) onDelta(id, chunk);
        }
        if (event["stop"].asBool()) {
            sawStop = true;
            limit = event["stop_type"].asString() == "limit";
            truncated = event["truncated"].asBool();
        }
    };
    http::Result result = http::PostStream(endpoint(L"/completion"), json::Value(std::move(body)).dump(), auth,
        [&](const char* bytes, size_t count) {
            lineBuffer.append(bytes, count);
            size_t end = 0;
            while ((end = lineBuffer.find('\n')) != std::string::npos) {
                processLine(lineBuffer.substr(0, end));
                lineBuffer.erase(0, end + 1);
            }
        }, cancel);
    if (!lineBuffer.empty()) processLine(lineBuffer);
    if (!result.ok) { *error = L"Built-in inference failed: " + util::Widen(result.error); return false; }
    if (!sawStop) { *error = L"Built-in inference ended without a completion signal."; return false; }
    if (limit || truncated) { *error = L"Translation exceeded the built-in model limit; partial output was discarded."; return false; }
    return true;
}

void Shutdown() {
    std::lock_guard lock(g_serverMutex);
    if (g_server) {
        TerminateProcess(g_server, 0);
        CloseHandle(g_server);
        g_server = nullptr;
    }
}

} // namespace builtin
