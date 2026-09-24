#include "Translator.h"
#include "BuiltInModel.h"
#include "Http.h"
#include "Json.h"
#include "LocalDiagnostics.h"
#include "Settings.h"
#include "Util.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace translator {

namespace {

// ---------- 提示词 ----------

std::string styleSuffix(RewriteStyle style) {
    const char* ins = lang::StyleInstruction(style);
    return *ins ? std::string(" ") + ins : std::string();
}

// ---------- LRU 缓存（key = backend|from|to|style|text）----------

class Cache {
public:
    static Cache& shared() {
        static Cache c;
        return c;
    }

    std::optional<std::wstring> get(const std::string& key) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.end(), order_, it->second.orderIt);
        return it->second.value;
    }

    void set(const std::string& key, const std::wstring& value) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.value = value;
            order_.splice(order_.end(), order_, it->second.orderIt);
            return;
        }
        order_.push_back(key);
        map_[key] = {value, std::prev(order_.end())};
        while (map_.size() > kCapacity) {
            map_.erase(order_.front());
            order_.pop_front();
        }
    }

    void clear() {
        std::lock_guard lock(mu_);
        map_.clear();
        order_.clear();
    }

private:
    static constexpr size_t kCapacity = 200;
    struct Entry {
        std::wstring value;
        std::list<std::string>::iterator orderIt;
    };
    std::mutex mu_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;
};

std::string cacheKey(const TranslationRequest& req, const char* backend) {
    std::string k = backend;
    k += '|';
    k += req.source ? lang::Code(*req.source) : "auto";
    k += '|';
    k += lang::Code(req.target);
    k += '|';
    k += lang::StyleCode(req.style);
    k += '|';
    k += util::Narrow(req.text);
    return k;
}

// ---------- 流式行组装（NDJSON / SSE 都按行切）----------

class LineAssembler {
public:
    /// 喂入一块字节，产出完整行（不含换行符）
    void feed(const char* data, size_t len, const std::function<void(const std::string&)>& onLine) {
        // 容忍流最前面的 UTF-8 BOM
        if (buf_.empty() && !bomChecked_ && len >= 3 &&
            (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            data += 3;
            len -= 3;
        }
        bomChecked_ = true;
        buf_.append(data, len);
        size_t pos;
        while ((pos = buf_.find('\n')) != std::string::npos) {
            std::string line = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) onLine(line);
        }
    }

    void finish(const std::function<void(const std::string&)>& onLine) {
        if (!buf_.empty()) {
            if (buf_.back() == '\r') buf_.pop_back();
            if (!buf_.empty()) onLine(buf_);
            buf_.clear();
        }
    }

private:
    std::string buf_;
    bool bomChecked_ = false;
};

// ---------- 后端配置快照 ----------
// 工作线程绝不直接读 Settings::shared()（主线程同时改设置会数据竞争），
// 发起请求时在调用线程拍快照带走。

struct BackendConfig {
    TranslationBackend backend;
    std::string ollamaHost;
    int ollamaPort;
    std::string ollamaModel;
    std::string openAIBaseURL;
    std::string openAIKey;
    std::string openAIModel;

    static BackendConfig snapshot() {
        const Settings& s = Settings::shared();
        return {s.backend, s.ollamaHost, s.ollamaPort, s.ollamaModel,
                s.openAIBaseURL, s.openAIKey, s.openAIModel};
    }
};

// ---------- 活跃请求管理 ----------

struct ActiveRequest {
    std::shared_ptr<std::atomic<bool>> cancel;
};

std::mutex g_mu;
std::unordered_map<uint64_t, ActiveRequest> g_active;
std::atomic<uint64_t> g_nextId{1};

void finishRequest(uint64_t id) {
    std::lock_guard lock(g_mu);
    g_active.erase(id);
}

// ---------- provider 实现（工作线程内运行）----------

struct StreamOutcome {
    bool ok = false;
    std::wstring error;
    std::wstring full;
};

std::wstring providerError(const http::Result& result) {
    if (!result.body.empty()) {
        bool ok = false;
        json::Value body = json::Parse(result.body, &ok);
        if (ok) {
            const json::Value& nested = body["error"];
            if (nested.isObject() && nested["message"].isString())
                return util::Widen(nested["message"].asString());
            if (nested.isString()) return util::Widen(nested.asString());
            if (body["message"].isString()) return util::Widen(body["message"].asString());
        }
    }
    return util::Widen(result.error);
}

StreamOutcome runOllama(const TranslationRequest& req, const BackendConfig& s, uint64_t id,
                        const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    StreamOutcome out;

    auto makeBody = [&](bool disableThink) {
        json::Object options;
        options["temperature"] = 0.3;
        options["top_p"] = 0.9;
        options["top_k"] = 40;

        json::Object body;
        body["model"] = s.ollamaModel;
        body["prompt"] = PlainPrompt(req.text, req.target, req.source, req.style);
        body["stream"] = true;
        body["keep_alive"] = "10m";
        // 思考型模型（qwen3 系等）默认会先生成大段隐藏推理，翻译一句要几分钟；
        // 必须显式关闭。不支持该字段的模型会 400，调用方去掉后重试。
        if (disableThink) body["think"] = false;
        body["options"] = json::Value(std::move(options));
        return json::Value(std::move(body)).dump();
    };

    std::wstring url = util::Widen(s.ollamaHost) + L":" + std::to_wstring(s.ollamaPort) + L"/api/generate";

    LineAssembler lines;
    auto onLine = [&](const std::string& line) {
            bool ok = false;
            json::Value v = json::Parse(line, &ok);
            if (!ok) return;
            const std::string& delta = v["response"].asString();
            if (!delta.empty()) {
                std::wstring wdelta = util::Widen(delta);
                out.full += wdelta;
                onDelta(id, wdelta);
            }
    };
    auto onChunk = [&](const char* data, size_t len) {
        lines.feed(data, len, onLine);
    };

    http::Result r = http::PostStream(url, makeBody(true), L"", onChunk, cancel);
    if (r.status == 400 && !cancel.load()) {
        // 模型不支持 think 字段 → 去掉重试
        util::Log("ollama: think rejected (400), retrying without");
        lines = LineAssembler();
        out.full.clear();
        r = http::PostStream(url, makeBody(false), L"", onChunk, cancel);
    }
    if (!r.ok) {
        out.error = r.status == 0 && !cancel.load()
                        ? L"Can't reach Ollama — is `ollama serve` running?"
                        : util::Widen(r.error);
        return out;
    }
    lines.finish(onLine);
    out.ok = true;
    return out;
}

StreamOutcome runOpenAI(const TranslationRequest& req, const BackendConfig& s, uint64_t id,
                        const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    StreamOutcome out;

    std::string base = s.openAIBaseURL;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
        out.error = L"Missing cloud API base URL";
        return out;
    }
    if (s.openAIModel.empty()) {
        out.error = L"Missing cloud model name";
        return out;
    }
    const bool localEndpoint = base.rfind("http://127.0.0.1", 0) == 0 ||
                               base.rfind("http://localhost", 0) == 0 ||
                               base.rfind("http://[::1]", 0) == 0;
    const bool secureEndpoint = base.rfind("https://", 0) == 0;
    if (s.openAIKey.empty() && !localEndpoint) {
        out.error = L"Missing cloud API key";
        return out;
    }
    if (!s.openAIKey.empty() && !secureEndpoint && !localEndpoint) {
        out.error = L"Refusing to send an API key over an insecure HTTP connection";
        return out;
    }

    json::Array messages;
    {
        json::Object sys;
        sys["role"] = "system";
        sys["content"] = SystemPrompt(req.target, req.source, req.style);
        messages.push_back(json::Value(std::move(sys)));
        json::Object user;
        user["role"] = "user";
        user["content"] = util::Narrow(req.text);
        messages.push_back(json::Value(std::move(user)));
    }
    json::Object body;
    body["model"] = s.openAIModel;
    body["stream"] = true;
    body["messages"] = json::Value(std::move(messages));

    std::wstring headers;
    if (!s.openAIKey.empty())
        headers += L"Authorization: Bearer " + util::Widen(s.openAIKey) + L"\r\n";
    if (base.find("openrouter.ai") != std::string::npos) {
        headers += L"HTTP-Referer: https://xnu.app/TypeTide/\r\n";
        headers += L"X-OpenRouter-Title: TypeTide\r\n";
    }

    LineAssembler lines;
    auto onLine = [&](const std::string& line) {
            if (auto delta = ParseOpenAISSELine(line)) {
                out.full += *delta;
                onDelta(id, *delta);
            }
    };
    auto onChunk = [&](const char* data, size_t len) {
        lines.feed(data, len, onLine);
    };

    http::Result r = http::PostStream(util::Widen(base) + L"/chat/completions",
                                      json::Value(std::move(body)).dump(), headers, onChunk, cancel);
    if (!r.ok) {
        out.error = providerError(r);
        return out;
    }
    lines.finish(onLine);
    out.ok = true;
    return out;
}

StreamOutcome runBuiltIn(const TranslationRequest& req, uint64_t id,
                         const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    StreamOutcome out;
    out.ok = builtin::Stream(req, id, onDelta, cancel, &out.full, &out.error);
    return out;
}

const char* backendName(TranslationBackend backend) {
    switch (backend) {
    case TranslationBackend::BuiltIn: return "builtin-translategemma-q4km";
    case TranslationBackend::OpenAI: return "openai";
    case TranslationBackend::Ollama: return "ollama";
    }
    return "unknown";
}

} // namespace

std::string SystemPrompt(Language target, std::optional<Language> source, RewriteStyle style) {
    std::string from = source ? lang::PromptName(*source) : "the detected language";
    std::string s = "You are a professional translation engine. Translate the user's text from ";
    s += from;
    s += " into ";
    s += lang::PromptName(target);
    s += ".";
    s += styleSuffix(style);
    s += " Output ONLY the translation, with no quotes, no explanations, no extra notes. "
         "Preserve the original meaning and formatting.";
    return s;
}

std::string PlainPrompt(const std::wstring& text, Language target, std::optional<Language> source, RewriteStyle style) {
    std::string from = source ? lang::PromptName(*source) : "the source language";
    std::string s = "Translate the following text from ";
    s += from;
    s += " to ";
    s += lang::PromptName(target);
    s += ".";
    s += styleSuffix(style);
    s += " Only output the translation, no explanation.\n\n";
    s += util::Narrow(text);
    return s;
}

uint64_t Stream(const TranslationRequest& req, DeltaFn onDelta, DoneFn onDone) {
    uint64_t id = g_nextId.fetch_add(1);
    const auto start = std::chrono::steady_clock::now();

    // 配置快照在调用线程（主线程）拍好带进工作线程
    BackendConfig cfg = BackendConfig::snapshot();

    // 缓存命中：同步回调全文
    const char* provider = backendName(cfg.backend);
    std::string key = cacheKey(req, provider);
    if (auto cached = Cache::shared().get(key)) {
        onDelta(id, *cached);
        onDone(id, true, L"");
        diagnostics::Record("translation", "cacheHit",
                            provider,
                            {}, {}, 0, 0, (int)req.text.size());
        return id;
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(g_mu);
        g_active[id] = {cancel};
    }

    std::thread([req, cfg, id, key, cancel, start,
                 onDelta = std::move(onDelta), onDone = std::move(onDone)] {
        std::optional<std::chrono::steady_clock::time_point> firstToken;
        DeltaFn trackedDelta = [&](uint64_t requestId, const std::wstring& delta) {
            if (!delta.empty() && !firstToken) firstToken = std::chrono::steady_clock::now();
            onDelta(requestId, delta);
        };
        StreamOutcome out = cfg.backend == TranslationBackend::BuiltIn
                                ? runBuiltIn(req, id, trackedDelta, *cancel)
                            : cfg.backend == TranslationBackend::OpenAI
                                ? runOpenAI(req, cfg, id, trackedDelta, *cancel)
                                : runOllama(req, cfg, id, trackedDelta, *cancel);
        bool cancelled = cancel->load();
        finishRequest(id);
        const auto end = std::chrono::steady_clock::now();
        const int totalMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        const int firstMs = firstToken
            ? (int)std::chrono::duration_cast<std::chrono::milliseconds>(*firstToken - start).count()
            : -1;
        const char* backend = backendName(cfg.backend);
        if (cancelled) {
            diagnostics::Record("translation", "cancelled", backend, {}, "cancelled",
                                firstMs, totalMs, (int)req.text.size());
            return;  // 取消的请求不再回调
        }
        const std::wstring cleaned = util::Trim(out.full);
        if (out.ok && !cleaned.empty()) {
            Cache::shared().set(key, cleaned);
            onDone(id, true, L"");
            diagnostics::Record("translation", "success", backend, {}, {},
                                firstMs, totalMs, (int)req.text.size());
        } else {
            const bool emptyResponse = out.ok && cleaned.empty();
            const std::wstring error = emptyResponse ? L"Empty translation" : out.error;
            onDone(id, false, error);
            diagnostics::Record("translation", "failure", backend, {},
                                emptyResponse ? "emptyResponse" : "provider",
                                firstMs, totalMs, (int)req.text.size());
        }
    }).detach();

    return id;
}

void Cancel(uint64_t reqId) {
    std::lock_guard lock(g_mu);
    auto it = g_active.find(reqId);
    if (it != g_active.end()) it->second.cancel->store(true);
}

void CancelAll() {
    std::lock_guard lock(g_mu);
    for (auto& [id, req] : g_active) req.cancel->store(true);
}

std::optional<std::wstring> TranslateFully(const TranslationRequest& req, std::wstring* error) {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false, ok = false;
    std::wstring full, err;

    Stream(
        req,
        [&](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(mu);
            full += delta;
        },
        [&](uint64_t, bool success, const std::wstring& e) {
            std::lock_guard lock(mu);
            ok = success;
            err = e;
            done = true;
            cv.notify_all();
        });

    std::unique_lock lock(mu);
    cv.wait(lock, [&] { return done; });

    std::wstring cleaned = util::Trim(full);
    if (!ok || cleaned.empty()) {
        if (error) *error = ok ? L"Empty translation" : err;
        return std::nullopt;
    }
    return cleaned;
}

void CheckHealthAsync(std::function<void(HealthCheckResult)> completion) {
    const auto start = std::chrono::steady_clock::now();
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    struct State {
        std::mutex mutex;
        std::wstring full;
        std::optional<std::chrono::steady_clock::time_point> first;
    };
    auto state = std::make_shared<State>();
    const std::string backend = backendName(Settings::shared().backend);
    Stream(
        {L"TypeTide health check " + std::to_wstring(stamp), Language::English,
         Language::Chinese, RewriteStyle::Faithful},
        [state](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(state->mutex);
            if (!delta.empty() && !state->first) state->first = std::chrono::steady_clock::now();
            state->full += delta;
        },
        [state, start, backend, completion = std::move(completion)](
            uint64_t, bool success, const std::wstring& message) mutable {
            const auto end = std::chrono::steady_clock::now();
            std::lock_guard lock(state->mutex);
            HealthCheckResult result;
            result.ok = success && !util::Trim(state->full).empty();
            result.message = result.ok ? L"Connected and translated successfully."
                                       : (message.empty() ? L"No translation returned." : message);
            result.firstTokenMs = state->first
                ? (int)std::chrono::duration_cast<std::chrono::milliseconds>(*state->first - start).count()
                : -1;
            result.totalMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            diagnostics::Record("backendHealthCheck", result.ok ? "success" : "failure",
                                backend, {}, result.ok ? "" : "backend",
                                result.firstTokenMs, result.totalMs);
            completion(std::move(result));
        });
}

void ClearCacheForTesting() {
    Cache::shared().clear();
}

std::optional<std::wstring> ParseOpenAISSELine(const std::string& line) {
    if (line.rfind("data:", 0) != 0) return std::nullopt;
    const std::string payload = util::Trim(line.substr(5));
    if (payload == "[DONE]") return std::nullopt;
    bool ok = false;
    json::Value value = json::Parse(payload, &ok);
    if (!ok) return std::nullopt;
    const json::Value& delta = value["choices"].at(0)["delta"]["content"];
    if (!delta.isString() || delta.asString().empty()) return std::nullopt;
    return util::Widen(delta.asString());
}

std::vector<std::string> AssembleLinesForTesting(const std::vector<std::string>& fragments) {
    LineAssembler assembler;
    std::vector<std::string> lines;
    auto append = [&](const std::string& line) { lines.push_back(line); };
    for (const auto& fragment : fragments) assembler.feed(fragment.data(), fragment.size(), append);
    assembler.finish(append);
    return lines;
}

} // namespace translator
