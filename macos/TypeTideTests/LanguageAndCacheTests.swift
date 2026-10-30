import XCTest
@testable import TypeTide

final class LanguageAndCacheTests: XCTestCase {
    func testVietnameseCanBeSelectedAndDetected() throws {
        let vietnamese = try XCTUnwrap(Language(rawValue: "vi"))
        XCTAssertTrue(Language.allCases.contains(vietnamese))
        XCTAssertEqual(vietnamese.displayName, "Tiếng Việt (Vietnamese)")
        XCTAssertEqual(vietnamese.promptName, "Vietnamese")
        let text = "Xin chào, tôi muốn đọc tài liệu kỹ thuật bằng tiếng Việt."
        XCTAssertEqual(Language.detect(in: text), vietnamese)
        XCTAssertEqual(Language.detect(in: text.decomposedStringWithCanonicalMapping), vietnamese)
        let route = TranslationRouting.resolve(text: "The quick brown fox jumps over the lazy dog.",
            mode: .auto, isWrite: false, native: vietnamese, foreign: .english)
        XCTAssertEqual(route, TranslationRoute(source: .english, target: vietnamese))
        let prompt = try LocalTranslationPrompt.make(.init(text: "Hello", source: .english, target: vietnamese))
        XCTAssertTrue(prompt.contains("English (en) to Vietnamese (vi) translator"))
    }

    @MainActor
    func testTideStateTracksStreamingLifecycle() {
        let model = TranslationPopupModel(original: "你好", target: .english)
        XCTAssertEqual(model.tidePhase, .waiting)

        model.appendTranslation("Hel")
        XCTAssertEqual(model.translation, "Hel")
        XCTAssertEqual(model.tidePhase, .flowing)

        model.appendTranslation("lo")
        model.finishTranslation()
        XCTAssertEqual(model.tidePhase, .settling)

        model.finishTide()
        XCTAssertEqual(model.tidePhase, .complete)

        model.resetTranslation()
        XCTAssertEqual(model.tidePhase, .waiting)
        XCTAssertTrue(model.translation.isEmpty)

        model.failTranslation("offline")
        XCTAssertEqual(model.tidePhase, .failed)
        XCTAssertEqual(model.errorText, "offline")
    }

    func testLanguageDetectionAndUnknownInput() {
        XCTAssertEqual(Language.detect(in: "今天天气很好，我们出去散步吧。"), .chinese)
        XCTAssertEqual(Language.detect(in: "The quick brown fox jumps over the lazy dog."), .english)
        XCTAssertNil(Language.detect(in: "   \n"))
    }

    func testDirectionLabels() {
        XCTAssertEqual(TranslationDirection.nativeToForeign.label(native: .chinese, foreign: .english), "中文 → English")
        XCTAssertEqual(TranslationDirection.foreignToNative.label(native: .chinese, foreign: .english), "English → 中文")
    }

    func testCacheKeyIncludesBackendDirectionAndText() {
        let cache = TranslationCache(capacity: 2)
        let a = cache.key(backend: "openai", from: .chinese, to: .english, text: "你好")
        let b = cache.key(backend: "ollama", from: .chinese, to: .english, text: "你好")

        XCTAssertNotEqual(a, b)
        cache.set("hello", for: a)
        XCTAssertEqual(cache.value(for: a), "hello")
    }

    func testCacheEvictsLeastRecentlyUsedEntry() {
        let cache = TranslationCache(capacity: 2)
        cache.set("A", for: "a")
        cache.set("B", for: "b")
        XCTAssertEqual(cache.value(for: "a"), "A") // a is now most recent
        cache.set("C", for: "c")

        XCTAssertNil(cache.value(for: "b"))
        XCTAssertEqual(cache.value(for: "a"), "A")
        XCTAssertEqual(cache.value(for: "c"), "C")
    }
}
