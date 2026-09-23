//
//  BehaviorSettingsView.swift
//  TypeTide
//
//  触发与改写行为设置。
//

import SwiftUI

struct BehaviorSettingsView: View {
    @AppStorage(AppSettings.Keys.backend) private var backendRaw = TranslationBackend.builtIn.rawValue
    @AppStorage(AppSettings.Keys.selectionTrigger) private var selectionRaw = SelectionTrigger.none.rawValue
    @AppStorage(AppSettings.Keys.rewritePreview) private var rewritePreview = false
    @AppStorage(AppSettings.Keys.rewriteStyle) private var rewriteStyleRaw = RewriteStyle.faithful.rawValue

    var body: some View {
        Form {
            Section {
                Picker("After selecting text", selection: $selectionRaw) {
                    ForEach(SelectionTrigger.allCases) { t in
                        Text(t.displayName).tag(t.rawValue)
                    }
                }
                .onChange(of: selectionRaw) { _, _ in
                    TriggerController.shared.applyEnabledState()
                }
                SettingsNote(text: "“Show floating icon” pops a small button next to your selection; “Auto-translate” shows the translation immediately. The shortcut always works regardless.")

            } header: {
                SettingsSectionHeader(symbol: "text.viewfinder", color: .blue,
                                      title: "Selection translation", subtitle: "Choose what happens after selecting text")
            }

            Section {
                Toggle(isOn: $rewritePreview) {
                    SettingsLabel(symbol: "eye.fill", color: .teal, title: "Preview before replacing")
                }
                SettingsNote(text: rewritePreview
                             ? "Rewrite shows the result in a popup; click Replace to apply."
                             : "Rewrite replaces the text in place immediately (undo with ⌘Z).")

                Picker("Style", selection: $rewriteStyleRaw) {
                    ForEach(RewriteStyle.allCases) { s in
                        Text(s.displayName).tag(s.rawValue)
                    }
                }
                if backendRaw == TranslationBackend.builtIn.rawValue {
                    SettingsNote(text: "The built-in model supports Faithful translation only. Select Faithful here, or choose Ollama/API in Backend for other styles.", tint: .orange)
                }
            } header: {
                SettingsSectionHeader(symbol: "pencil.and.outline", color: .purple,
                                      title: "Rewrite & replace", subtitle: "Control preview and writing style")
            }
        }
        .settingsPage("Behavior")
    }
}
