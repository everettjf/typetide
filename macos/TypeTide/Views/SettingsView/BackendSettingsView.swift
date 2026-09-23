//
//  BackendSettingsView.swift
//  TypeTide
//
//  翻译后端设置：本地 Ollama / OpenAI 兼容。
//

import SwiftUI

struct BackendSettingsView: View {
    @AppStorage(AppSettings.Keys.backend) private var backendRaw = TranslationBackend.builtIn.rawValue
    @AppStorage(AppSettings.Keys.openAIBaseURL) private var openAIBaseURL = "https://api.openai.com/v1"
    @State private var openAIKey = AppSettings.openAIKey
    @AppStorage(AppSettings.Keys.openAIModel) private var openAIModel = "gpt-4o-mini"
    @AppStorage(AppSettings.Keys.ollamaModel) private var ollamaModel = "qwen2.5:3b"
    @State private var installedModels: [String] = []
    @State private var isLoadingModels = false
    @State private var ollamaError: String?
    @State private var isTestingBackend = false
    @State private var healthResult: BackendHealthResult?

    private var modelRecommendation: OllamaModelResolver.ModelRecommendation? {
        OllamaModelResolver.recommendation(from: installedModels)
    }

    var body: some View {
        Form {
            Section {
                Picker("Translation backend", selection: $backendRaw) {
                    ForEach(TranslationBackend.allCases) { backend in
                        Text(backend.displayName).tag(backend.rawValue)
                    }
                }
                .pickerStyle(.segmented)
            } header: {
                SettingsSectionHeader(symbol: "server.rack", color: .teal,
                                      title: "Backend", subtitle: "Where translations are generated")
            }

            if backendRaw == TranslationBackend.openai.rawValue {
                Section {
                    TextField("Base URL", text: $openAIBaseURL)
                        .textFieldStyle(.roundedBorder)
                    SecureField("API Key", text: $openAIKey)
                        .textFieldStyle(.roundedBorder)
                        .onChange(of: openAIKey) { _, value in AppSettings.openAIKey = value }
                    TextField("Model", text: $openAIModel)
                        .textFieldStyle(.roundedBorder)
                    SettingsNote(text: "Works with any OpenAI-compatible /chat/completions endpoint (official API, proxies, local servers).")
                    SettingsNote(
                        text: "When you trigger a translation or rewrite, the selected text is sent to this endpoint. Avoid cloud backends for passwords, private messages, or other sensitive text.",
                        symbol: "exclamationmark.shield.fill",
                        tint: .orange
                    )
                } header: {
                    SettingsSectionHeader(symbol: "cloud.fill", color: .indigo, title: "OpenAI-compatible")
                }
            } else if backendRaw == TranslationBackend.builtIn.rawValue {
                Section { BuiltInModelSettingsView() } header: {
                    SettingsSectionHeader(symbol: "cpu", color: .green, title: "Built-in translation", subtitle: "Runs privately on this Mac")
                }
            } else {
                Section {
                    HStack {
                        LabeledContent("Service", value: "\(OllamaConfig.host):\(OllamaConfig.port)")
                        Circle()
                            .fill(ollamaError == nil && !installedModels.isEmpty ? Color.green : Color.orange)
                            .frame(width: 8, height: 8)
                            .accessibilityLabel(ollamaError == nil ? "Connected" : "Unavailable")
                    }

                    if isLoadingModels {
                        HStack(spacing: 8) {
                            ProgressView().controlSize(.small)
                            Text("Checking Ollama…").foregroundStyle(.secondary)
                        }
                    } else if installedModels.isEmpty {
                        SettingsNote(
                            text: ollamaError ?? "No generation models are installed. Install one with `ollama pull qwen2.5:7b`.",
                            symbol: "exclamationmark.triangle.fill",
                            tint: .orange
                        )
                    } else {
                        Picker("Translation model", selection: $ollamaModel) {
                            ForEach(installedModels, id: \.self) { model in
                                Text(model == modelRecommendation?.model ? "\(model) · Recommended" : model)
                                    .tag(model)
                            }
                        }
                        .pickerStyle(.menu)
                        if let recommendation = modelRecommendation {
                            SettingsNote(
                                text: "Recommended for this Mac: \(recommendation.model) (comfortable target up to about \(formatBillions(recommendation.maximumParameterBillions))B parameters).",
                                symbol: "memorychip.fill",
                                tint: .blue
                            )
                        }
                    }

                    HStack {
                        SettingsNote(text: "Embedding-only models are hidden because they cannot translate text.")
                        Spacer()
                        Button {
                            Task { await refreshModels() }
                        } label: {
                            Label("Refresh", systemImage: "arrow.clockwise")
                        }
                        .controlSize(.small)
                        .disabled(isLoadingModels)
                    }
                    SettingsNote(
                        text: "Ollama processes selected text on this Mac. TypeTide does not send it to a TypeTide service.",
                        symbol: "hand.raised.fill",
                        tint: .green
                    )
                } header: {
                    SettingsSectionHeader(symbol: "desktopcomputer", color: .green,
                                          title: "Ollama", subtitle: "Local and private")
                }
            }

            Section {
                HStack {
                    Button {
                        Task { await testBackend() }
                    } label: {
                        if isTestingBackend {
                            Label("Testing…", systemImage: "hourglass")
                        } else {
                            Label("Test Connection", systemImage: "stethoscope")
                        }
                    }
                    .disabled(isTestingBackend)
                    Spacer()
                    if let result = healthResult {
                        StatusPill(text: result.isSuccess ? "Ready" : "Needs attention",
                                   style: result.isSuccess ? .success : .warning)
                    }
                }
                if let result = healthResult {
                    SettingsNote(
                        text: healthDescription(result),
                        symbol: result.isSuccess ? "bolt.fill" : "wrench.and.screwdriver.fill",
                        tint: result.isSuccess ? .green : .orange
                    )
                }
            } header: {
                SettingsSectionHeader(symbol: "stethoscope", color: .blue,
                                      title: "Connection Test", subtitle: "Uses fixed synthetic text, never your clipboard")
            }
        }
        .settingsPage("Backend")
        .task(id: backendRaw) {
            healthResult = nil
            if backendRaw == TranslationBackend.ollama.rawValue {
                await refreshModels()
            }
        }
    }

    @MainActor
    private func testBackend() async {
        isTestingBackend = true
        defer { isTestingBackend = false }
        healthResult = await BackendHealthChecker.check()
    }

    private func healthDescription(_ result: BackendHealthResult) -> String {
        guard result.isSuccess else { return result.message }
        let first = result.firstTokenMilliseconds.map { "first token \($0) ms, " } ?? ""
        return "\(result.message) \(first)total \(result.totalMilliseconds) ms."
    }

    private func formatBillions(_ value: Double) -> String {
        String(Int(value))
    }

    @MainActor
    private func refreshModels() async {
        isLoadingModels = true
        ollamaError = nil
        defer { isLoadingModels = false }

        let allModels = await OllamaModelResolver.installedModels()
        installedModels = allModels.filter { model in
            let name = model.lowercased()
            return !name.contains("embed") && !name.contains("bge") && !name.contains("minilm")
        }

        guard !installedModels.isEmpty else {
            ollamaError = allModels.isEmpty
                ? "Ollama is not responding. Start Ollama, then refresh."
                : "Only embedding models are installed. Install a generation model to translate."
            return
        }

        if !installedModels.contains(ollamaModel), let first = installedModels.first {
            ollamaModel = first
        }
    }
}
