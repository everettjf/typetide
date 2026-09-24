import SwiftUI

struct BuiltInModelSettingsView: View {
    @ObservedObject private var model = BuiltInModelManager.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            LabeledContent("Model", value: BuiltInModel.name)
            if !BuiltInModel.supported {
                SettingsNote(text: "Requires Apple Silicon (M1 or later). Select Ollama or OpenAI-compatible on this Mac.", tint: .orange)
            } else if model.installed {
                Label("Ready for offline translation", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            } else if model.isDownloading {
                ProgressView(value: model.progress)
                if !model.transferDetail.isEmpty { Text(model.transferDetail).font(.caption).monospacedDigit() }
                HStack {
                    Text("\(model.status) \(Int(model.progress * 100))%")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Pause") { model.cancel() }
                }
            } else {
                Button(model.status.isEmpty ? "Download Model · 2.22 GB" : "Resume Download") { model.download() }
                    .buttonStyle(.borderedProminent)
                if !model.status.isEmpty { Text(model.status).font(.caption).foregroundStyle(.secondary) }
            }
            if let error = model.error { SettingsNote(text: error, tint: .orange) }
            if !model.installed { SettingsNote(text: "Source: Hugging Face. Downloads use two connections and save completed 16 MB segments. Pause or quit safely; allow about 4.5 GB of free disk space during installation.") }
            SettingsNote(text: "No Ollama or account required. The first download comes from Hugging Face; selected text stays on this Mac. Afterwards, translation works without internet.", symbol: "hand.raised.fill", tint: .green)
            SettingsNote(text: "Supports Faithful translation, including Replace. For Formal, Casual or Polished writing styles, choose Ollama or an API backend. Model memory is released after two idle minutes.")
            HStack {
                Link("Model details", destination: URL(string: "https://huggingface.co/\(BuiltInModel.repository)")!)
                Link("Gemma terms", destination: URL(string: "https://ai.google.dev/gemma/terms")!)
            }.font(.caption)
        }
    }
}
