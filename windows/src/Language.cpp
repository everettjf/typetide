#include "Language.h"
#include <algorithm>
#include <cwctype>
#include <map>
#include <sstream>

namespace lang {

const char* Code(Language l) {
    switch (l) {
    case Language::English: return "en";
    case Language::Chinese: return "zh";
    case Language::Hindi: return "hi";
    case Language::Spanish: return "es";
    case Language::French: return "fr";
    case Language::Arabic: return "ar";
    case Language::Bengali: return "bn";
    case Language::Russian: return "ru";
    case Language::Portuguese: return "pt";
    case Language::Indonesian: return "id";
    case Language::Vietnamese: return "vi";
    }
    return "en";
}

std::optional<Language> FromCode(const std::string& code) {
    for (Language l : All)
        if (code == Code(l)) return l;
    return std::nullopt;
}

const wchar_t* DisplayName(Language l) {
    switch (l) {
    case Language::English: return L"English";
    case Language::Chinese: return L"中文 (Chinese)";
    case Language::Hindi: return L"हिन्दी (Hindi)";
    case Language::Spanish: return L"Español (Spanish)";
    case Language::French: return L"Français (French)";
    case Language::Arabic: return L"العربية (Arabic)";
    case Language::Bengali: return L"বাংলা (Bengali)";
    case Language::Russian: return L"Русский (Russian)";
    case Language::Portuguese: return L"Português (Portuguese)";
    case Language::Indonesian: return L"Bahasa Indonesia (Indonesian)";
    case Language::Vietnamese: return L"Tiếng Việt (Vietnamese)";
    }
    return L"English";
}

std::wstring ShortName(Language l) {
    std::wstring name = DisplayName(l);
    auto pos = name.find(L" (");
    return pos == std::wstring::npos ? name : name.substr(0, pos);
}

const char* PromptName(Language l) {
    switch (l) {
    case Language::English: return "English";
    case Language::Chinese: return "Chinese";
    case Language::Hindi: return "Hindi";
    case Language::Spanish: return "Spanish";
    case Language::French: return "French";
    case Language::Arabic: return "Arabic";
    case Language::Bengali: return "Bengali";
    case Language::Russian: return "Russian";
    case Language::Portuguese: return "Portuguese";
    case Language::Indonesian: return "Indonesian";
    case Language::Vietnamese: return "Vietnamese";
    }
    return "English";
}

namespace {

std::wstring trim(const std::wstring& value) {
    const auto isSpace = [](wchar_t c) { return std::iswspace(c) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return {first, last};
}

std::wstring toLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) {
                       // The default C locale does not fold Vietnamese uppercase letters.
                       if ((c >= 0x1EA0 && c <= 0x1EF8 && c % 2 == 0) ||
                           c == 0x0102 || c == 0x0110 || c == 0x0128 || c == 0x0168 || c == 0x01A0)
                           return static_cast<wchar_t>(c + 1);
                       if (c == 0x01AF) return static_cast<wchar_t>(0x01B0);
                       if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)
                           return static_cast<wchar_t>(c + 0x20);
                       return static_cast<wchar_t>(std::towlower(c));
                   });
    return value;
}

// 拉丁语系（en/es/fr/pt/id）的高频停用词，用于打分区分
const std::map<Language, std::vector<std::wstring>>& latinStopwords() {
    static const std::map<Language, std::vector<std::wstring>> map = {
        {Language::English, {L"the", L"and", L"is", L"of", L"to", L"in", L"that", L"it", L"you", L"was",
                             L"for", L"are", L"with", L"this", L"have", L"not", L"be", L"they", L"his", L"from"}},
        {Language::Spanish, {L"el", L"la", L"de", L"que", L"y", L"en", L"los", L"del", L"las", L"un",
                             L"por", L"con", L"una", L"su", L"para", L"es", L"al", L"lo", L"como", L"más"}},
        {Language::French,  {L"le", L"la", L"de", L"et", L"les", L"des", L"est", L"en", L"un", L"une",
                             L"du", L"que", L"qui", L"dans", L"pour", L"ce", L"pas", L"au", L"sur", L"ne"}},
        {Language::Portuguese, {L"o", L"a", L"de", L"que", L"e", L"do", L"da", L"em", L"um", L"para",
                                L"com", L"não", L"uma", L"os", L"no", L"se", L"na", L"por", L"mais", L"as"}},
        {Language::Indonesian, {L"yang", L"dan", L"di", L"itu", L"dengan", L"untuk", L"tidak", L"ini", L"dari", L"dalam",
                                L"akan", L"pada", L"juga", L"saya", L"ke", L"karena", L"tersebut", L"bisa", L"ada", L"mereka"}},
        {Language::Vietnamese, {L"tôi", L"bạn", L"là", L"và", L"của", L"có", L"không", L"một", L"cho", L"với",
                               L"này", L"những", L"được", L"trong", L"để", L"khi", L"chúng", L"từ", L"các", L"việc",
                               L"xin", L"chào", L"toi", L"khong", L"nhung", L"duoc", L"chung", L"cac", L"viec", L"cua", L"chao"}},
    };
    return map;
}

} // namespace

std::optional<Language> Detect(const std::wstring& text) {
    std::wstring t = trim(text);
    if (t.empty()) return std::nullopt;

    // 1) 按 Unicode 脚本区段计数
    int cjk = 0, devanagari = 0, arabic = 0, bengali = 0, cyrillic = 0, latin = 0, letters = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        wchar_t c = t[i];
        unsigned cp = c;
        // 代理对（扩展 CJK 等）
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < t.size()) {
            cp = 0x10000 + ((c - 0xD800) << 10) + (t[i + 1] - 0xDC00);
            ++i;
        }
        if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
            (cp >= 0x3000 && cp <= 0x303F)) { ++cjk; ++letters; }
        else if (cp >= 0x0900 && cp <= 0x097F) { ++devanagari; ++letters; }
        else if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F)) { ++arabic; ++letters; }
        else if (cp >= 0x0980 && cp <= 0x09FF) { ++bengali; ++letters; }
        else if (cp >= 0x0400 && cp <= 0x04FF) { ++cyrillic; ++letters; }
        else if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                 (cp >= 0x00C0 && cp <= 0x024F) ||
                 (cp >= 0x1E00 && cp <= 0x1EFF)) { ++latin; ++letters; }
    }
    if (letters == 0) return std::nullopt;

    // 非拉丁脚本：占比过半即判定
    struct { int count; Language lang; } scripts[] = {
        {cjk, Language::Chinese}, {devanagari, Language::Hindi}, {arabic, Language::Arabic},
        {bengali, Language::Bengali}, {cyrillic, Language::Russian},
    };
    for (auto& s : scripts)
        if (s.count * 2 > letters) return s.lang;

    if (latin * 2 <= letters) return std::nullopt;

    // 2) 拉丁语系：分词后按停用词打分
    std::wstring lower = toLower(t);
    std::vector<std::wstring> words;
    std::wstring cur;
    bool vietnameseMarks = false;
    for (wchar_t c : lower) {
        if ((c >= 0x1EA0 && c <= 0x1EF9) || c == 0x0111 || c == 0x01A1 || c == 0x01B0 ||
            c == 0x0309 || c == 0x0323) vietnameseMarks = true;
        // Preserve decomposed words instead of splitting at each combining accent.
        if (c >= 0x0300 && c <= 0x036F) continue;
        bool isWordChar = (c >= 'a' && c <= 'z') || (c >= 0x00C0 && c <= 0x024F) ||
                          (c >= 0x1E00 && c <= 0x1EFF) || c == '\'';
        if (isWordChar) cur += c;
        else if (!cur.empty()) { words.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) words.push_back(cur);
    if (words.empty()) return std::nullopt;

    Language best = Language::English;
    int bestScore = 0, secondScore = 0;
    for (auto& [langKey, stops] : latinStopwords()) {
        int score = langKey == Language::Vietnamese && vietnameseMarks ? 1 : 0;
        for (auto& w : words)
            if (std::find(stops.begin(), stops.end(), w) != stops.end()) ++score;
        if (score > bestScore) { secondScore = bestScore; bestScore = score; best = langKey; }
        else if (score > secondScore) { secondScore = score; }
    }
    // 无停用词命中或并列 → 不确定
    if (bestScore == 0 || bestScore == secondScore) return std::nullopt;
    return best;
}

const char* DirectionCode(TranslationDirection d) {
    switch (d) {
    case TranslationDirection::Auto: return "auto";
    case TranslationDirection::NativeToForeign: return "nativeToForeign";
    case TranslationDirection::ForeignToNative: return "foreignToNative";
    }
    return "auto";
}

std::optional<TranslationDirection> DirectionFromCode(const std::string& s) {
    if (s == "auto") return TranslationDirection::Auto;
    if (s == "nativeToForeign") return TranslationDirection::NativeToForeign;
    if (s == "foreignToNative") return TranslationDirection::ForeignToNative;
    return std::nullopt;
}

ResolvedDirection ResolveDirection(const std::wstring& text, TranslationDirection mode,
                                   bool isWrite, Language native, Language foreign) {
    switch (mode) {
    case TranslationDirection::NativeToForeign:
        return {native, foreign};
    case TranslationDirection::ForeignToNative:
        return {foreign, native};
    case TranslationDirection::Auto:
    default: {
        std::optional<Language> detected = Detect(text);
        Language target;
        if (detected == native) target = foreign;
        else if (detected == foreign) target = native;
        else target = isWrite ? foreign : native;
        return {detected, target};
    }
    }
}

const char* StyleCode(RewriteStyle s) {
    switch (s) {
    case RewriteStyle::Faithful: return "faithful";
    case RewriteStyle::Formal: return "formal";
    case RewriteStyle::Casual: return "casual";
    case RewriteStyle::Polished: return "polished";
    }
    return "faithful";
}

std::optional<RewriteStyle> StyleFromCode(const std::string& s) {
    if (s == "faithful") return RewriteStyle::Faithful;
    if (s == "formal") return RewriteStyle::Formal;
    if (s == "casual") return RewriteStyle::Casual;
    if (s == "polished") return RewriteStyle::Polished;
    return std::nullopt;
}

const wchar_t* StyleDisplayName(RewriteStyle s) {
    switch (s) {
    case RewriteStyle::Faithful: return L"Faithful (plain translation)";
    case RewriteStyle::Formal: return L"Formal";
    case RewriteStyle::Casual: return L"Casual / spoken";
    case RewriteStyle::Polished: return L"Polished";
    }
    return L"Faithful";
}

const char* StyleInstruction(RewriteStyle s) {
    switch (s) {
    case RewriteStyle::Faithful: return "";
    case RewriteStyle::Formal: return "Use a professional, formal tone.";
    case RewriteStyle::Casual: return "Use a natural, casual, conversational tone.";
    case RewriteStyle::Polished: return "Improve clarity and flow so it reads polished and native, while keeping the original meaning.";
    }
    return "";
}

} // namespace lang
