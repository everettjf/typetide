//
//  LanguageConfig.swift
//  TypeTide
//
//  语言配置 - 定义支持的检测语言
//

import Foundation
import NaturalLanguage

/// 通用语言类型（用于检测和目标翻译）
/// Keep the picker, detection and prompt names aligned on both platforms.
enum Language: String, CaseIterable, Identifiable {
    case english = "en"
    case chinese = "zh"
    case hindi = "hi"
    case spanish = "es"
    case french = "fr"
    case arabic = "ar"
    case bengali = "bn"
    case russian = "ru"
    case portuguese = "pt"
    case indonesian = "id"
    case vietnamese = "vi"

    var id: String { rawValue }

    /// 语言显示名称
    var displayName: String {
        switch self {
        case .english: return "English"
        case .chinese: return "中文 (Chinese)"
        case .hindi: return "हिन्दी (Hindi)"
        case .spanish: return "Español (Spanish)"
        case .french: return "Français (French)"
        case .arabic: return "العربية (Arabic)"
        case .bengali: return "বাংলা (Bengali)"
        case .russian: return "Русский (Russian)"
        case .portuguese: return "Português (Portuguese)"
        case .indonesian: return "Bahasa Indonesia (Indonesian)"
        case .vietnamese: return "Tiếng Việt (Vietnamese)"
        }
    }

    /// 简短名（菜单/方向标签用）：取 displayName 中括号前的部分
    var shortName: String {
        displayName.components(separatedBy: " (").first ?? displayName
    }

    var promptName: String {
        switch self {
        case .english: return "English"
        case .chinese: return "Chinese"
        case .hindi: return "Hindi"
        case .spanish: return "Spanish"
        case .french: return "French"
        case .arabic: return "Arabic"
        case .bengali: return "Bengali"
        case .russian: return "Russian"
        case .portuguese: return "Portuguese"
        case .indonesian: return "Indonesian"
        case .vietnamese: return "Vietnamese"
        }
    }

    /// 翻译提示词模板
    func translationPrompt(for text: String, targetLanguage: Language) -> String {
        return """
        Translate the following \(displayName) text to \(targetLanguage.displayName). Only provide the translation, no explanation or additional text.

        \(displayName): \(text)
        \(targetLanguage.displayName):
        """
    }

    /// 用 NaturalLanguage 检测文本的主语言，映射到支持的语言之一。
    /// 文本为空、无法判断、或不在支持集合内时返回 nil（交由调用方走兜底方向）。
    static func detect(in text: String) -> Language? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        let recognizer = NLLanguageRecognizer()
        recognizer.processString(trimmed)
        guard let dominant = recognizer.dominantLanguage else { return nil }

        switch dominant {
        case .english: return .english
        case .simplifiedChinese, .traditionalChinese: return .chinese
        case .hindi: return .hindi
        case .spanish: return .spanish
        case .french: return .french
        case .arabic: return .arabic
        case .bengali: return .bengali
        case .russian: return .russian
        case .portuguese: return .portuguese
        case .indonesian: return .indonesian
        case .vietnamese: return .vietnamese
        default: return nil
        }
    }
}

/// 翻译方向模式（读 ⌥D / 写 ⌥R 各自独立配置）
enum TranslationDirection: String, CaseIterable, Identifiable {
    case auto              // 自动双向：检测选中文字语言，在母语/外语间互译
    case nativeToForeign   // 固定：母语 → 外语（跳过检测）
    case foreignToNative   // 固定：外语 → 母语（跳过检测）

    var id: String { rawValue }

    /// 依据当前配置的母语 / 外语生成可读标签
    func label(native: Language, foreign: Language) -> String {
        switch self {
        case .auto: return "Auto · bidirectional"
        case .nativeToForeign: return "\(native.shortName) → \(foreign.shortName)"
        case .foreignToNative: return "\(foreign.shortName) → \(native.shortName)"
        }
    }
}

/// 语言检测配置管理器
struct LanguageConfig {
    /// 用户选择的源语言（检测语言）
    static var sourceLanguage: Language {
        get {
            if let savedLang = UserDefaults.standard.string(forKey: "sourceLanguage"),
               let language = Language(rawValue: savedLang) {
                return language
            }
            return .chinese // 默认为中文
        }
        set {
            UserDefaults.standard.set(newValue.rawValue, forKey: "sourceLanguage")
        }
    }

    /// 用户选择的目标语言（翻译到）
    static var targetLanguage: Language {
        get {
            if let savedLang = UserDefaults.standard.string(forKey: "targetLanguage"),
               let language = Language(rawValue: savedLang) {
                return language
            }
            return .english // 默认为英语
        }
        set {
            UserDefaults.standard.set(newValue.rawValue, forKey: "targetLanguage")
        }
    }

    /// 检查是否选择了相同的源语言和目标语言
    static var isSameLanguage: Bool {
        return sourceLanguage == targetLanguage
    }
}
