#include "Settings.h"
#include "Credentials.h"
#include "Json.h"
#include "Util.h"
#include <fstream>
#include <sstream>

std::wstring Hotkey::displayString() const {
    if (!isConfigured()) return L"Not set";
    std::wstring s;
    if (modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (modifiers & MOD_SHIFT) s += L"Shift+";
    if (modifiers & MOD_ALT) s += L"Alt+";
    if (modifiers & MOD_WIN) s += L"Win+";

    wchar_t name[64]{};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG l = (LONG)(scan << 16);
    switch (vk) {  // 扩展键需要置扩展位才能取到正确名字
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
        l |= 1 << 24; break;
    }
    if (GetKeyNameTextW(l, name, 64) > 0) s += name;
    else s += (wchar_t)vk;
    return s;
}

Settings& Settings::shared() {
    static Settings s = [] {
        Settings v;
        v.load();
        return v;
    }();
    return s;
}

std::wstring Settings::filePath() {
    return util::AppDataDir() + L"\\settings.json";
}

void Settings::load() {
    std::ifstream f(filePath(), std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    const auto length = f.tellg();
    if (length < 0 || length > 1024 * 1024) {
        util::Log("settings: refusing invalid file size");
        return;
    }
    f.seekg(0, std::ios::beg);
    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = false;
    json::Value root = json::Parse(ss.str(), &ok);
    if (!ok || !root.isObject()) {
        util::Log("settings: invalid JSON, using safe defaults");
        return;
    }

    auto str = [&](const char* k, const std::string& def) {
        return root[k].isString() ? root[k].asString() : def;
    };
    auto boolean = [&](const char* k, bool def) {
        return root[k].type() == json::Value::Type::Bool ? root[k].asBool() : def;
    };

    enabled = boolean("enabled", enabled);
    const std::string backendCode = str("backend", "builtin");
    backend = backendCode == "builtin" ? TranslationBackend::BuiltIn
            : backendCode == "openai" ? TranslationBackend::OpenAI : TranslationBackend::Ollama;

    ollamaHost = str("ollamaHost", ollamaHost);
    ollamaPort = root["ollamaPort"].isNull() ? ollamaPort : root["ollamaPort"].asInt(ollamaPort);
    if (ollamaPort < 1 || ollamaPort > 65535) ollamaPort = 11434;
    ollamaModel = str("ollamaModel", ollamaModel);
    if (ollamaHost.size() > 2048) ollamaHost = "http://127.0.0.1";
    if (ollamaModel.size() > 256) ollamaModel = "qwen2.5:3b";

    openAIBaseURL = str("openAIBaseURL", openAIBaseURL);
    if (openAIBaseURL.size() > 2048) openAIBaseURL = "https://api.openai.com/v1";
    const std::string legacyKey = str("openAIKey", "");
    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    bool migratedLegacyKey = false;
    if (isolatedTest) {
        openAIKey = legacyKey;
    } else {
        openAIKey = credentials::LoadCloudApiKey();
        if (openAIKey.empty() && !legacyKey.empty()) {
            openAIKey = legacyKey;
            credentials::SaveCloudApiKey(openAIKey); // migrate old plaintext settings
            migratedLegacyKey = true;
        }
    }
    openAIModel = str("openAIModel", openAIModel);
    if (openAIModel.size() > 256) openAIModel = "gpt-5-mini";

    if (auto l = lang::FromCode(str("nativeLanguage", ""))) nativeLanguage = *l;
    if (auto l = lang::FromCode(str("foreignLanguage", ""))) foreignLanguage = *l;
    if (auto d = lang::DirectionFromCode(str("readDirection", ""))) readDirection = *d;
    if (auto d = lang::DirectionFromCode(str("rewriteDirection", ""))) rewriteDirection = *d;

    auto loadHotkey = [&](const char* k, Hotkey& hk) {
        const json::Value& v = root[k];
        if (v.isObject()) {
            UINT modifiers = (UINT)v["modifiers"].asInt((int)hk.modifiers);
            UINT vk = (UINT)v["vk"].asInt((int)hk.vk);
            constexpr UINT allowed = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
            if ((modifiers & ~allowed) == 0 && vk < 256) {
                hk.modifiers = modifiers;
                hk.vk = vk;
            }
        }
    };
    loadHotkey("readShortcut", readShortcut);
    loadHotkey("rewriteShortcut", rewriteShortcut);
    auto loadAction = [&](const char* key, ShortcutAction fallback) {
        const std::string value = str(key, "");
        if (value == "nativePopup") return ShortcutAction::NativePopup;
        if (value == "foreignPopup") return ShortcutAction::ForeignPopup;
        if (value == "smartReplace") return ShortcutAction::SmartReplace;
        if (value == "nativeReplace") return ShortcutAction::NativeReplace;
        if (value == "foreignReplace") return ShortcutAction::ForeignReplace;
        if (value == "smartPopup") return ShortcutAction::SmartPopup;
        return fallback;
    };
    readShortcutAction = loadAction("readShortcutAction", ShortcutAction::SmartPopup);
    rewriteShortcutAction = loadAction("rewriteShortcutAction", ShortcutAction::SmartReplace);

    std::string trig = str("selectionTrigger", "none");
    selectionTrigger = trig == "icon" ? SelectionTrigger::Icon
                     : trig == "auto" ? SelectionTrigger::Auto
                     : SelectionTrigger::None;
    rewritePreview = boolean("rewritePreview", rewritePreview);
    if (auto s2 = lang::StyleFromCode(str("readStyle", ""))) readStyle = *s2;
    if (auto s2 = lang::StyleFromCode(str("rewriteStyle", ""))) rewriteStyle = *s2;

    skipApps.clear();
    if (const json::Array* arr = root["skipApps"].array())
        for (auto& item : *arr) {
            if (!item.isString() || skipApps.size() >= 100) continue;
            std::wstring app = util::Trim(util::Widen(item.asString()));
            if (app.empty() || app.size() > 260) continue;
            bool duplicate = false;
            for (const auto& existing : skipApps)
                if (util::ToLower(existing) == util::ToLower(app)) duplicate = true;
            if (!duplicate) skipApps.push_back(std::move(app));
        }

    hasCompletedFirstLaunch = boolean("hasCompletedFirstLaunch", false);
    lastUpdateCheck = (long long)root["lastUpdateCheck"].asNumber(0);
    if (migratedLegacyKey) save(); // rewrite JSON without the plaintext key
}

bool Settings::save() const {
    json::Object root;
    root["enabled"] = enabled;
    root["backend"] = backend == TranslationBackend::BuiltIn ? "builtin"
                    : backend == TranslationBackend::OpenAI ? "openai" : "ollama";

    root["ollamaHost"] = ollamaHost;
    root["ollamaPort"] = ollamaPort;
    root["ollamaModel"] = ollamaModel;

    root["openAIBaseURL"] = openAIBaseURL;
    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    if (isolatedTest) root["openAIKey"] = openAIKey;
    else if (!credentials::SaveCloudApiKey(openAIKey)) {
        util::Log("settings: credential save failed (%lu)", GetLastError());
        return false;
    }
    root["openAIModel"] = openAIModel;

    root["nativeLanguage"] = lang::Code(nativeLanguage);
    root["foreignLanguage"] = lang::Code(foreignLanguage);
    root["readDirection"] = lang::DirectionCode(readDirection);
    root["rewriteDirection"] = lang::DirectionCode(rewriteDirection);

    auto hotkeyJson = [](const Hotkey& hk) {
        json::Object o;
        o["modifiers"] = (int)hk.modifiers;
        o["vk"] = (int)hk.vk;
        return json::Value(std::move(o));
    };
    root["readShortcut"] = hotkeyJson(readShortcut);
    root["rewriteShortcut"] = hotkeyJson(rewriteShortcut);
    auto actionCode = [](ShortcutAction action) -> std::string {
        switch (action) {
        case ShortcutAction::NativePopup: return "nativePopup";
        case ShortcutAction::ForeignPopup: return "foreignPopup";
        case ShortcutAction::SmartReplace: return "smartReplace";
        case ShortcutAction::NativeReplace: return "nativeReplace";
        case ShortcutAction::ForeignReplace: return "foreignReplace";
        case ShortcutAction::SmartPopup:
        default: return "smartPopup";
        }
    };
    root["readShortcutAction"] = actionCode(readShortcutAction);
    root["rewriteShortcutAction"] = actionCode(rewriteShortcutAction);

    root["selectionTrigger"] = selectionTrigger == SelectionTrigger::Icon ? "icon"
                             : selectionTrigger == SelectionTrigger::Auto ? "auto"
                             : "none";
    root["rewritePreview"] = rewritePreview;
    root["readStyle"] = lang::StyleCode(readStyle);
    root["rewriteStyle"] = lang::StyleCode(rewriteStyle);

    json::Array apps;
    for (auto& a : skipApps) apps.push_back(util::Narrow(a));
    root["skipApps"] = json::Value(std::move(apps));

    root["hasCompletedFirstLaunch"] = hasCompletedFirstLaunch;
    root["lastUpdateCheck"] = (double)lastUpdateCheck;

    // 原子写：先写临时文件再 rename 替换，进程中途被杀也不会留下半截 JSON
    std::wstring path = filePath();
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            util::Log("settings: cannot open temp file (%lu)", GetLastError());
            return false;
        }
        f << json::Value(std::move(root)).dump();
        f.flush();
        if (!f) {
            util::Log("settings: temp write failed");
            f.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    // 杀毒软件/索引器可能短暂占用目标文件，做有限重试但不阻塞太久。
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (attempt < 3) Sleep(15 * (attempt + 1));
    }
    util::Log("settings: atomic replace failed (%lu)", GetLastError());
    DeleteFileW(tmp.c_str());
    return false;
}
