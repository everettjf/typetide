import AppKit
import ApplicationServices
import XCTest
@testable import TypeTide

@MainActor
final class RewriteCaptureTests: XCTestCase {
    func testBrowserEditorWithoutAXValueCopiesWholeField() async throws {
        let context = RewriteCaptureContext(selectedRange: CFRange(location: 7, length: 0), isEditable: true)
        var requests: [Bool] = []
        let result = await SelectionCapture.captureForRewrite(context: context) { selectAll in
            requests.append(selectAll)
            return selectAll ? "你好，请帮我分析这个问题。" : nil
        }
        let captured = try XCTUnwrap(result)
        XCTAssertEqual(captured.text, "你好，请帮我分析这个问题。")
        XCTAssertTrue(captured.isWholeField)
        XCTAssertEqual(captured.method, .clipboardFallback)
        XCTAssertEqual(requests, [true])
    }

    func testExistingSelectionDoesNotExpandToWholeEditor() async throws {
        let context = RewriteCaptureContext(selectedRange: CFRange(location: 3, length: 2),
                                            value: "前缀 你好 后缀", isEditable: true)
        var requests: [Bool] = []
        let result = await SelectionCapture.captureForRewrite(context: context) {
            requests.append($0)
            return $0 ? "前缀 你好 后缀" : "你好"
        }
        let captured = try XCTUnwrap(result)
        XCTAssertEqual(captured.text, "你好")
        XCTAssertFalse(captured.isWholeField)
        XCTAssertEqual(requests, [false])
    }

    func testUnreportedSelectionIsCopiedBeforeSelectingWholeField() async throws {
        let context = RewriteCaptureContext(isEditable: true)
        var requests: [Bool] = []
        let result = await SelectionCapture.captureForRewrite(context: context) {
            requests.append($0)
            return "selected text"
        }
        XCTAssertFalse(try XCTUnwrap(result).isWholeField)
        XCTAssertEqual(requests, [false])
    }

    func testReadOnlyAndSecureElementsCannotSelectAll() async {
        XCTAssertTrue(SelectionCapture.canSelectWholeField(role: kAXTextAreaRole, subrole: nil))
        XCTAssertTrue(SelectionCapture.canSelectWholeField(role: kAXTextFieldRole, subrole: nil))
        XCTAssertFalse(SelectionCapture.canSelectWholeField(role: "AXWebArea", subrole: nil))
        XCTAssertFalse(SelectionCapture.canSelectWholeField(role: kAXTextFieldRole, subrole: kAXSecureTextFieldSubrole))
        let readOnly = await SelectionCapture.captureForRewrite(context: .init()) { selectAll in
            XCTAssertFalse(selectAll)
            return nil
        }
        XCTAssertNil(readOnly)
        let protected = await SelectionCapture.captureForRewrite(context: .init(value: "secret", isEditable: true, isProtected: true)) { _ in
            XCTFail("Protected fields must not be copied")
            return nil
        }
        XCTAssertNil(protected)
    }

    func testWholeFieldCopyRestoresClipboardAndOriginalSelection() async {
        let pasteboard = NSPasteboard(name: .init("TypeTideTests.\(UUID())"))
        defer { pasteboard.releaseGlobally() }
        let item = NSPasteboardItem()
        item.setString("original clipboard", forType: .string)
        item.setData(Data([1, 2, 3]), forType: .init("com.typetide.test.binary"))
        pasteboard.writeObjects([item])
        var keys: [CGKeyCode] = []
        var restored = false
        let operations = ClipboardCaptureOperations(press: { key in
            keys.append(key)
            if key == Keyboard.cKey {
                pasteboard.clearContents()
                pasteboard.setString("browser input", forType: .string)
            }
            return true
        }, hasFocus: { true }, restoreSelection: { restored = true })
        let text = await SelectionCapture.copyViaPasteboard(selectAll: true, pasteboard: pasteboard, operations: operations)
        XCTAssertEqual(text, "browser input")
        XCTAssertEqual(keys, [Keyboard.aKey, Keyboard.cKey])
        XCTAssertTrue(restored)
        XCTAssertEqual(pasteboard.string(forType: .string), "original clipboard")
        XCTAssertEqual(pasteboard.data(forType: .init("com.typetide.test.binary")), Data([1, 2, 3]))
    }

    func testFailedCopyDoesNotTranslateStaleClipboard() async {
        let pasteboard = NSPasteboard(name: .init("TypeTideTests.\(UUID())"))
        defer { pasteboard.releaseGlobally() }
        pasteboard.setString("unrelated clipboard", forType: .string)
        let operations = ClipboardCaptureOperations(press: { _ in true }, hasFocus: { true }, restoreSelection: {})
        let text = await SelectionCapture.copyViaPasteboard(selectAll: false, pasteboard: pasteboard, operations: operations)
        XCTAssertNil(text)
        XCTAssertEqual(pasteboard.string(forType: .string), "unrelated clipboard")
    }

    func testFocusChangePreventsCopyingAnotherApplication() async {
        let pasteboard = NSPasteboard(name: .init("TypeTideTests.\(UUID())"))
        defer { pasteboard.releaseGlobally() }
        var keys: [CGKeyCode] = []
        var focused = true
        let operations = ClipboardCaptureOperations(press: { key in
            keys.append(key)
            focused = false
            return true
        }, hasFocus: { focused }, restoreSelection: {})
        let text = await SelectionCapture.copyViaPasteboard(selectAll: true, pasteboard: pasteboard, operations: operations)
        XCTAssertNil(text)
        XCTAssertEqual(keys, [Keyboard.aKey])
    }

    func testCancellationAfterSelectAllRestoresStateWithoutCopying() async {
        let pasteboard = NSPasteboard(name: .init("TypeTideTests.\(UUID())"))
        defer { pasteboard.releaseGlobally() }
        pasteboard.setString("original clipboard", forType: .string)
        let (events, continuation) = AsyncStream<CGKeyCode>.makeStream()
        var keys: [CGKeyCode] = []
        var restored = false
        let operations = ClipboardCaptureOperations(press: { key in
            keys.append(key)
            continuation.yield(key)
            return true
        }, hasFocus: { true }, restoreSelection: { restored = true })
        let task = Task { await SelectionCapture.copyViaPasteboard(selectAll: true, pasteboard: pasteboard, operations: operations) }
        var iterator = events.makeAsyncIterator()
        let firstKey = await iterator.next()
        XCTAssertEqual(firstKey, Keyboard.aKey)
        task.cancel()
        let result = await task.value
        continuation.finish()
        XCTAssertNil(result)
        XCTAssertTrue(restored)
        XCTAssertEqual(keys, [Keyboard.aKey])
        XCTAssertEqual(pasteboard.string(forType: .string), "original clipboard")
    }
}
