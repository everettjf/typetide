//
//  AppSettings.swift
//  TypeTide
//
//  统一配置入口（替代散落的 UserDefaults 读写）。
//

import AppKit

struct StoredShortcutPreference: Codable {
    let shortcut: KeyboardShortcutPreference?

    private enum CodingKeys: String, CodingKey { case shortcut }

    init(shortcut: KeyboardShortcutPreference?) {
        self.shortcut = shortcut
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        guard container.contains(.shortcut) else {
            throw DecodingError.keyNotFound(
                CodingKeys.shortcut,
                .init(codingPath: decoder.codingPath,
                      debugDescription: "Stored shortcut wrapper requires a shortcut key")
            )
        }
        shortcut = try container.decodeIfPresent(KeyboardShortcutPreference.self, forKey: .shortcut)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        if let shortcut {
            try container.encode(shortcut, forKey: .shortcut)
        } else {
            try container.encodeNil(forKey: .shortcut)
        }
    }

    static func decode(_ data: Data,
                       defaultValue: KeyboardShortcutPreference) -> KeyboardShortcutPreference? {
        if let stored = try? JSONDecoder().decode(Self.self, from: data) {
            return stored.shortcut
        }
        // Migration: older releases encoded KeyboardShortcutPreference directly.
        return (try? JSONDecoder().decode(KeyboardShortcutPreference.self, from: data)) ?? defaultValue
    }
}

/// 翻译后端
enum TranslationBackend: String, CaseIterable, Identifiable {
    case builtIn
    case ollama
    case openai

    var id: String { rawValue }
    var displayName: String {
        switch self {
        case .builtIn: return "Built-in (Offline)"
        case .ollama: return "Ollama"
        case .openai: return "OpenAI-compatible"
        }
    }
}

/// 划词触发方式
enum SelectionTrigger: String, CaseIterable, Identifiable {
    case none      // 仅快捷键
    case icon      // 划词后显示小图标，点击翻译
    case auto      // 划词后直接弹出翻译

    var id: String { rawValue }
    var displayName: String {
        switch self {
        case .none: return "Off (shortcut only)"
        case .icon: return "Show floating icon"
        case .auto: return "Auto-translate"
        }
    }
}

enum ShortcutAction: String, CaseIterable, Identifiable {
    case smartPopup
    case nativePopup
    case foreignPopup
    case smartReplace
    case nativeReplace
    case foreignReplace

    var id: String { rawValue }
    var displayName: String {
        switch self {
        case .smartPopup: return "Smart translate · popup"
        case .nativePopup: return "Translate to native language · popup"
        case .foreignPopup: return "Translate to foreign language · popup"
        case .smartReplace: return "Smart translate · replace"
        case .nativeReplace: return "Translate to native language · replace"
        case .foreignReplace: return "Translate to foreign language · replace"
        }
    }
    var usesPopup: Bool {
        switch self {
        case .smartPopup, .nativePopup, .foreignPopup: return true
        case .smartReplace, .nativeReplace, .foreignReplace: return false
        }
    }
}

/// 改写/翻译风格（仅改写使用；读翻译固定 faithful）
enum RewriteStyle: String, CaseIterable, Identifiable {
    case faithful  // 纯翻译
    case formal    // 正式
    case casual    // 口语
    case polished  // 润色

    var id: String { rawValue }
    var displayName: String {
        switch self {
        case .faithful: return "Faithful (plain translation)"
        case .formal: return "Formal"
        case .casual: return "Casual / spoken"
        case .polished: return "Polished"
        }
    }
    /// 注入到 prompt 的风格指令；faithful 返回 nil
    var instruction: String? {
        switch self {
        case .faithful: return nil
        case .formal: return "Use a professional, formal tone."
        case .casual: return "Use a natural, casual, conversational tone."
        case .polished: return "Improve clarity and flow so it reads polished and native, while keeping the original meaning."
        }
    }
}

/// 全局设置。非 View 代码用静态访问；View 用 @AppStorage 直接绑定同名 key。
enum AppSettings {
    static let defaultReadShortcut = KeyboardShortcutPreference(keyCode: 2, modifiers: [.option])
    static let defaultRewriteShortcut = KeyboardShortcutPreference(keyCode: 15, modifiers: [.option])

    enum Keys {
        static let enabled = "isTypeTideEnabled"
        static let backend = "translationBackend"

        // OpenAI 兼容
        static let openAIBaseURL = "openAIBaseURL"
        static let openAIKey = "openAIKey"
        static let openAIModel = "openAIModel"

        // Ollama
        static let ollamaModel = "selectedModel"

        // 语言：sourceLanguage = 用户母语，targetLanguage = 外语（见 LanguageConfig）
        // 触发快捷键
        static let readShortcut = "readShortcut"
        static let rewriteShortcut = "rewriteShortcut"
        static let readShortcutAction = "readShortcutAction"
        static let rewriteShortcutAction = "rewriteShortcutAction"

        // 翻译方向（读 / 写 各自独立；默认 auto 双向）
        static let readDirection = "readDirection"
        static let rewriteDirection = "rewriteDirection"

        // 行为
        static let selectionTrigger = "selectionTrigger"
        static let rewritePreview = "rewritePreview"
        static let rewriteStyle = "rewriteStyle"
        static let skipApps = "appSkipList"
        static let hasCompletedFirstLaunch = "hasCompletedFirstLaunch"
    }

    private static let d = UserDefaults.standard

    static var isEnabled: Bool {
        get { d.object(forKey: Keys.enabled) as? Bool ?? true }
        set { d.set(newValue, forKey: Keys.enabled) }
    }

    static var backend: TranslationBackend {
        get { TranslationBackend(rawValue: d.string(forKey: Keys.backend) ?? "") ?? .builtIn }
        set { d.set(newValue.rawValue, forKey: Keys.backend) }
    }

    // MARK: OpenAI 兼容
    static var openAIBaseURL: String {
        get { d.string(forKey: Keys.openAIBaseURL) ?? "https://api.openai.com/v1" }
        set { d.set(newValue, forKey: Keys.openAIBaseURL) }
    }
    static var openAIKey: String {
        get {
            let stored = Credentials.cloudAPIKey()
            if !stored.isEmpty { return stored }
            // One-time migration from legacy plaintext UserDefaults storage.
            let legacy = d.string(forKey: Keys.openAIKey) ?? ""
            if !legacy.isEmpty, Credentials.saveCloudAPIKey(legacy) {
                d.removeObject(forKey: Keys.openAIKey)
            }
            return legacy
        }
        set {
            if Credentials.saveCloudAPIKey(newValue) {
                d.removeObject(forKey: Keys.openAIKey)
            }
        }
    }
    static var openAIModel: String {
        get { d.string(forKey: Keys.openAIModel) ?? "gpt-4o-mini" }
        set { d.set(newValue, forKey: Keys.openAIModel) }
    }

    // MARK: Ollama
    static var ollamaModel: String {
        get { d.string(forKey: Keys.ollamaModel) ?? "qwen2.5:3b" }
        set { d.set(newValue, forKey: Keys.ollamaModel) }
    }
    static var ollamaHost: String { OllamaConfig.host }
    static var ollamaPort: Int { OllamaConfig.port }

    // MARK: 快捷键
    static var readShortcut: KeyboardShortcutPreference? {
        get { shortcut(forKey: Keys.readShortcut, defaultValue: defaultReadShortcut) }   // ⌥D
        set { saveShortcut(newValue, forKey: Keys.readShortcut) }
    }
    static var rewriteShortcut: KeyboardShortcutPreference? {
        get { shortcut(forKey: Keys.rewriteShortcut, defaultValue: defaultRewriteShortcut) } // ⌥R
        set { saveShortcut(newValue, forKey: Keys.rewriteShortcut) }
    }
    static var readShortcutAction: ShortcutAction {
        get { ShortcutAction(rawValue: d.string(forKey: Keys.readShortcutAction) ?? "") ?? .smartPopup }
        set { d.set(newValue.rawValue, forKey: Keys.readShortcutAction) }
    }
    static var rewriteShortcutAction: ShortcutAction {
        get { ShortcutAction(rawValue: d.string(forKey: Keys.rewriteShortcutAction) ?? "") ?? .smartReplace }
        set { d.set(newValue.rawValue, forKey: Keys.rewriteShortcutAction) }
    }

    // MARK: 翻译方向
    static var readDirection: TranslationDirection {
        get { TranslationDirection(rawValue: d.string(forKey: Keys.readDirection) ?? "") ?? .auto }
        set { d.set(newValue.rawValue, forKey: Keys.readDirection) }
    }
    static var rewriteDirection: TranslationDirection {
        get { TranslationDirection(rawValue: d.string(forKey: Keys.rewriteDirection) ?? "") ?? .auto }
        set { d.set(newValue.rawValue, forKey: Keys.rewriteDirection) }
    }

    // MARK: 行为
    static var selectionTrigger: SelectionTrigger {
        get { SelectionTrigger(rawValue: d.string(forKey: Keys.selectionTrigger) ?? "") ?? .none }
        set { d.set(newValue.rawValue, forKey: Keys.selectionTrigger) }
    }
    static var rewritePreview: Bool {
        get { d.bool(forKey: Keys.rewritePreview) }   // 默认 false = 直接替换
        set { d.set(newValue, forKey: Keys.rewritePreview) }
    }
    static var rewriteStyle: RewriteStyle {
        get { RewriteStyle(rawValue: d.string(forKey: Keys.rewriteStyle) ?? "") ?? .faithful }
        set { d.set(newValue.rawValue, forKey: Keys.rewriteStyle) }
    }
    /// 跳过列表兼容现有逗号分隔存储；匹配应用名、bundle id 或 .app 名称。
    static var skipApps: [String] {
        d.string(forKey: Keys.skipApps)?
            .split(separator: ",")
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty } ?? []
    }

    static func shouldSkipFrontmostApplication() -> Bool {
        guard !skipApps.isEmpty, let app = NSWorkspace.shared.frontmostApplication else { return false }
        let candidates = [
            app.localizedName,
            app.bundleIdentifier,
            app.bundleURL?.deletingPathExtension().lastPathComponent
        ].compactMap { $0?.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() }

        return skipApps.contains { entry in
            var normalized = entry.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            if normalized.hasSuffix(".app") { normalized.removeLast(4) }
            return candidates.contains(normalized)
        }
    }

    private static func shortcut(forKey key: String,
                                 defaultValue: KeyboardShortcutPreference) -> KeyboardShortcutPreference? {
        guard let data = d.data(forKey: key) else { return defaultValue }
        return StoredShortcutPreference.decode(data, defaultValue: defaultValue)
    }
    private static func saveShortcut(_ shortcut: KeyboardShortcutPreference?, forKey key: String) {
        if let data = try? JSONEncoder().encode(StoredShortcutPreference(shortcut: shortcut)) {
            d.set(data, forKey: key)
        }
    }
}
