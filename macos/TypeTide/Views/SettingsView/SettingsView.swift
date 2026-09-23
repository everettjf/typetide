//
//  SettingsView.swift
//  TypeTide
//
//  设置页面 - 稳定的双栏布局，不让 NavigationSplitView 注入多余工具栏。
//

import SwiftUI

// MARK: - Main Settings Window

struct SettingsView: View {
    @State private var selection: PreferencesSection = .general
    @AppStorage(AppSettings.Keys.hasCompletedFirstLaunch) private var hasCompletedFirstLaunch = false

    var body: some View {
        HSplitView {
            VStack(alignment: .leading, spacing: 0) {
                List(PreferencesSection.allCases, selection: $selection) { section in
                    Label {
                        Text(section.rawValue)
                    } icon: {
                        IconBadge(symbol: section.icon, color: section.tint, size: 20)
                    }
                    .tag(section)
                }
                .listStyle(.sidebar)
            }
            .frame(minWidth: 180, idealWidth: 190, maxWidth: 220)

            if !hasCompletedFirstLaunch && selection == .general {
                FirstLaunchView { section in
                    selection = section
                } finish: {
                    hasCompletedFirstLaunch = true
                    selection = .general
                }
            } else {
                switch selection {
                case .general:
                    GeneralSettingsView()
                case .behavior:
                    BehaviorSettingsView()
                case .backend:
                    BackendSettingsView()
                case .language:
                    LanguageSettingsView()
                case .shortcuts:
                    ShortcutsSettingsView()
                case .skipApps:
                    SkipAppsSettingsView()
                case .diagnostics:
                    DiagnosticsSettingsView()
                case .about:
                    AboutView()
                }
            }
        }
        .frame(minWidth: 780, minHeight: 560)
    }
}

#Preview {
    SettingsView()
}
