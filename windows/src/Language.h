//
//  Language.h — 语言定义、提示词与检测（对应 macOS 的 LanguageConfig）。
//
#pragma once
#include <optional>
#include <string>
#include <vector>

enum class Language {
    English,
    Chinese,
    Hindi,
    Spanish,
    French,
    Arabic,
    Bengali,
    Russian,
    Portuguese,
    Indonesian,
    Vietnamese,
};

/// 翻译方向模式（读 / 写 各自独立配置）
enum class TranslationDirection {
    Auto,             // 自动双向：检测选中文字语言，在母语/外语间互译
    NativeToForeign,  // 固定：母语 → 外语
    ForeignToNative,  // 固定：外语 → 母语
};

struct ResolvedDirection {
    std::optional<Language> source;
    Language target;
};

/// 改写/翻译风格
enum class RewriteStyle {
    Faithful,
    Formal,
    Casual,
    Polished,
};

namespace lang {

inline constexpr Language All[] = {
    Language::English, Language::Chinese, Language::Hindi, Language::Spanish,
    Language::French, Language::Arabic, Language::Bengali, Language::Russian,
    Language::Portuguese, Language::Indonesian, Language::Vietnamese,
};

/// 序列化用短代码（"en"…），与 macOS 侧一致
const char* Code(Language l);
std::optional<Language> FromCode(const std::string& code);

/// 展示名（"中文 (Chinese)"）
const wchar_t* DisplayName(Language l);
/// 短名（"中文"）
std::wstring ShortName(Language l);
/// 提示词里用的英文名（"Chinese"）
const char* PromptName(Language l);

/// 检测文本主语言（脚本区段 + 拉丁语系停用词打分）。
/// 无法确定时返回 nullopt，交由调用方走兜底方向。
std::optional<Language> Detect(const std::wstring& text);

const char* DirectionCode(TranslationDirection d);
std::optional<TranslationDirection> DirectionFromCode(const std::string& s);

ResolvedDirection ResolveDirection(const std::wstring& text, TranslationDirection mode,
                                   bool isWrite, Language native, Language foreign);

const char* StyleCode(RewriteStyle s);
std::optional<RewriteStyle> StyleFromCode(const std::string& s);
const wchar_t* StyleDisplayName(RewriteStyle s);
/// 注入 prompt 的风格指令；Faithful 返回空串
const char* StyleInstruction(RewriteStyle s);

} // namespace lang
