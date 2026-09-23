import XCTest
@testable import TypeTide

final class BuiltInTranslationTests: XCTestCase {
    func testDedicatedTranslationTemplateAndLanguageDirection() throws {
        let prompt = try LocalTranslationPrompt.make(.init(text: "你好", source: .chinese, target: .english))
        XCTAssertTrue(prompt.hasPrefix("<bos><start_of_turn>user\n"))
        XCTAssertTrue(prompt.contains("Chinese (zh) to English (en) translator"))
        XCTAssertTrue(prompt.contains("text into English:\n\n\n你好<end_of_turn>"))
        XCTAssertTrue(prompt.hasSuffix("<start_of_turn>model\n"))
        XCTAssertFalse(prompt.contains("<start_of_turn>system"))
    }

    func testStylesCannotSilentlyBecomePlainTranslations() {
        for style in [RewriteStyle.formal, .casual, .polished] {
            XCTAssertThrowsError(try LocalTranslationPrompt.make(.init(text: "Hello", source: .english, target: .chinese, style: style)))
        }
    }

    func testMissingAndIncompleteModelsAreNotReady() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        XCTAssertFalse(BuiltInModel.isInstalled(at: directory))
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try Data(BuiltInModel.revision.utf8).write(to: directory.appendingPathComponent("complete"))
        XCTAssertFalse(BuiltInModel.isInstalled(at: directory))
    }

    func testRejectsCorruptDownloadEvenWhenSizeMatches() throws {
        let file = BuiltInModel.files.first { $0.name == "added_tokens.json" }!
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: url) }
        try Data(repeating: 0, count: Int(file.size)).write(to: url)
        XCTAssertThrowsError(try BuiltInModel.validate(url, file: file))
    }

    func testCancelledDownloadCannotPublishAnInstallation() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory.appendingPathExtension("download")) }
        let task = Task {
            try await BuiltInModelManager.install(at: directory) { _, _ in }
        }
        task.cancel()
        do {
            try await task.value
            XCTFail("Cancelled installation should throw")
        } catch is CancellationError {}
        XCTAssertFalse(BuiltInModel.isInstalled(at: directory))
    }

    @MainActor
    func testDefaultIsBuiltInAndExplicitOllamaChoicePersists() {
        let key = AppSettings.Keys.backend
        let old = UserDefaults.standard.object(forKey: key)
        defer {
            if let old { UserDefaults.standard.set(old, forKey: key) }
            else { UserDefaults.standard.removeObject(forKey: key) }
        }
        UserDefaults.standard.removeObject(forKey: key)
        XCTAssertEqual(AppSettings.backend, .builtIn)
        AppSettings.backend = .ollama
        XCTAssertEqual(AppSettings.backend, .ollama)
        AppSettings.backend = .openai
        XCTAssertEqual(AppSettings.backend, .openai)
    }

    /// Explicit opt-in because this downloads 2.22 GB. Inference loads only the local directory.
    func testLiveModelDownloadAndOfflineTranslation() async throws {
        guard ProcessInfo.processInfo.environment["TYPETIDE_RUN_LOCAL_MODEL_TEST"] == "1" else {
            throw XCTSkip("Set TEST_RUNNER_TYPETIDE_RUN_LOCAL_MODEL_TEST=1 when invoking xcodebuild to run the 2.22 GB integration test.")
        }
        let directory = URL(fileURLWithPath: "/tmp/typetide-live-model")
        if !BuiltInModel.isInstalled(at: directory) {
            try await BuiltInModelManager.install(at: directory) { _, _ in }
        }
        XCTAssertTrue(BuiltInModel.isInstalled(at: directory))
        let engine = LocalTranslationEngine()
        for request in [
            TranslationRequest(text: "你好世界，今天天气很好。", source: .chinese, target: .english),
            TranslationRequest(text: "Please save the document before closing the window.", source: .english, target: .chinese)
        ] {
            let stream = AsyncThrowingStream<String, Error> { continuation in
                Task {
                    do {
                        try await engine.translate(request, continuation: continuation, directory: directory)
                        continuation.finish()
                    } catch { continuation.finish(throwing: error) }
                }
            }
            let start = ContinuousClock.now
            var firstToken: ContinuousClock.Instant?
            var chunks = [String]()
            for try await chunk in stream {
                if firstToken == nil { firstToken = .now }
                chunks.append(chunk)
            }
            let result = chunks.joined()
            XCTAssertFalse(result.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            XCTAssertNotEqual(result, request.text)
            XCTAssertGreaterThan(chunks.count, 1)
            if request.target == .english { XCTAssertTrue(result.lowercased().contains("hello")) }
            else { XCTAssertTrue(result.contains("保存")) }
            print("LIVE_MLX_TRANSLATION=\(result)")
            print("LIVE_MLX_LATENCY=first \(start.duration(to: firstToken ?? .now)), total \(start.duration(to: .now))")
        }
        let longRequest = TranslationRequest(text: String(repeating: "This sentence is too long. ", count: 700), source: .english, target: .chinese)
        let (unusedStream, continuation) = AsyncThrowingStream<String, Error>.makeStream()
        _ = unusedStream
        do {
            try await engine.translate(longRequest, continuation: continuation, directory: directory)
            XCTFail("Long input must be rejected, not truncated")
        } catch let error as TranslationError {
            XCTAssertTrue(error.localizedDescription.contains("too long"))
        }
        continuation.finish()

        let (cancelStream, cancelContinuation) = AsyncThrowingStream<String, Error>.makeStream()
        let cancelTask = Task {
            do {
                try await engine.translate(.init(text: "Hello, world.", source: .english, target: .chinese),
                                           continuation: cancelContinuation, directory: directory)
                cancelContinuation.finish()
            } catch { cancelContinuation.finish(throwing: error) }
        }
        // Cancel before generation starts; the engine must surface cancellation, not success.
        cancelTask.cancel()
        do {
            for try await _ in cancelStream {}
            XCTFail("Cancelled inference should throw")
        } catch is CancellationError {}
        await cancelTask.value
    }
}
