import Foundation
import MLX
import MLXLLM
import MLXLMCommon
import Tokenizers

struct LocalTranslationProvider: TranslationProvider {
    let id = "builtin-\(BuiltInModel.revision)"
    func stream(_ request: TranslationRequest) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            let task = Task {
                do {
                    try await LocalTranslationEngine.shared.translate(request, continuation: continuation)
                    continuation.finish()
                } catch { continuation.finish(throwing: error) }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
}

/// Mirrors the upstream TranslateGemma text template; raw tokenization avoids a
/// generic chat processor changing its structured source/target language fields.
@MainActor
enum LocalTranslationPrompt {
    static func make(_ request: TranslationRequest) throws -> String {
        guard request.style == .faithful else {
            throw TranslationError.notConfigured("The built-in model supports Faithful translation. Choose Faithful in Behavior settings, or use Ollama/API for other writing styles.")
        }
        guard let source = request.source ?? Language.detect(in: request.text) else {
            throw TranslationError.notConfigured("Couldn't detect the source language. Choose a fixed translation direction in Language settings.")
        }
        let from = source.promptName, to = request.target.promptName
        return """
        <bos><start_of_turn>user
        You are a professional \(from) (\(source.rawValue)) to \(to) (\(request.target.rawValue)) translator. Your goal is to accurately convey the meaning and nuances of the original \(from) text while adhering to \(to) grammar, vocabulary, and cultural sensitivities.
        Produce only the \(to) translation, without any additional explanations or commentary. Please translate the following \(from) text into \(to):


        \(request.text.trimmingCharacters(in: .whitespacesAndNewlines))<end_of_turn>
        <start_of_turn>model

        """
    }
}

actor LocalTranslationEngine {
    static let shared = LocalTranslationEngine()
    private var container: ModelContainer?
    private var busy = false
    private var idleTask: Task<Void, Never>?

    func translate(_ request: TranslationRequest,
                   continuation: AsyncThrowingStream<String, Error>.Continuation,
                   directory: URL = BuiltInModel.directory) async throws {
        guard BuiltInModel.supported else {
            throw TranslationError.notConfigured("Built-in translation requires an Apple Silicon Mac. Choose Ollama or an API backend on this Mac.")
        }
        let prompt = try await LocalTranslationPrompt.make(request)
        guard BuiltInModel.isInstalled(at: directory) else {
            throw TranslationError.notConfigured("Download the built-in model in Settings → Backend (2.22 GB). After download, translations work offline without Ollama.")
        }
        // Serialize loading and generation. Waiting never blocks the UI, and is cancellable.
        while busy { try await Task.sleep(for: .milliseconds(25)) }
        try Task.checkCancellation()
        busy = true
        idleTask?.cancel()
        defer {
            busy = false
            idleTask = Task {
                do {
                    try await Task.sleep(for: .seconds(120))
                    self.unload()
                } catch {}
            }
        }
        if container == nil {
            container = try await LLMModelFactory.shared.loadContainer(configuration: .init(
                directory: directory, extraEOSTokens: ["<end_of_turn>"]
            ))
        }
        try Task.checkCancellation()
        guard let container else { return }
        try await container.perform { context in
            let tokens = context.tokenizer.encode(text: prompt, addSpecialTokens: false)
            // TranslateGemma was trained with 2K input context. Never silently truncate a selection.
            guard tokens.count <= 2048 else {
                throw TranslationError.notConfigured("This selection is too long for the built-in model. Translate a shorter passage or choose Ollama/API.")
            }
            let iterator = try TokenIterator(input: LMInput(tokens: MLXArray(tokens)), model: context.model,
                                             parameters: .init(maxTokens: 2048, temperature: 0))
            let (stream, generationTask) = MLXLMCommon.generateTask(
                promptTokenCount: tokens.count, modelConfiguration: context.configuration,
                tokenizer: context.tokenizer, iterator: iterator)
            try await withTaskCancellationHandler {
                var reachedLimit = false
                for await event in stream {
                    if Task.isCancelled { break }
                    switch event {
                    case .chunk(let text): continuation.yield(text)
                    case .info(let info): reachedLimit = info.stopReason == .length
                    default: break
                    }
                }
                if Task.isCancelled { generationTask.cancel() }
                await generationTask.value
                try Task.checkCancellation()
                if reachedLimit {
                    throw TranslationError.notConfigured("Translation exceeded the built-in model's output limit. Select a shorter passage; the partial translation has not been applied.")
                }
            } onCancel: { generationTask.cancel() }
        }
    }

    private func unload() {
        guard !busy else { return }
        container = nil
        Memory.clearCache()
    }
}
