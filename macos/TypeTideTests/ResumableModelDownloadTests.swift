import XCTest
import CryptoKit
@testable import TypeTide

final class ResumableModelDownloadTests: XCTestCase {
    func testLiveRangeDownload() async throws {
        guard ProcessInfo.processInfo.environment["TYPETIDE_TEST_RANGE_DOWNLOAD"] == "1" else {
            throw XCTSkip("Opt in to the 33 MB Hugging Face range integration test")
        }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let file = BuiltInModel.files.first { $0.name == "tokenizer.json" }!
        let session = URLSession(configuration: .ephemeral)
        defer { session.invalidateAndCancel() }
        let target = root.appendingPathComponent(file.name)
        try await ResumableModelDownload.fetch(file: file, target: target, session: session) { _, _ in }
        try BuiltInModel.validate(target, file: file)
    }

    func testRejectsWrongRangeAndWholeFileResponses() throws {
        let part = ResumableModelDownload.Part(index: 1, start: 16, end: 31)
        let url = URL(string: "https://example.com/model")!
        for (status, range) in [(200, "bytes 16-31/32"), (206, "bytes 0-15/32"), (206, "bytes 16-31/33")] {
            let response = HTTPURLResponse(url: url, statusCode: status, httpVersion: nil, headerFields: ["Content-Range": range])!
            XCTAssertThrowsError(try ResumableModelDownload.validateResponse(response, part: part, total: 32))
        }
        let valid = HTTPURLResponse(url: url, statusCode: 206, httpVersion: nil, headerFields: ["Content-Range": "bytes 16-31/32"])!
        XCTAssertNoThrow(try ResumableModelDownload.validateResponse(valid, part: part, total: 32))
    }

    func testResumeSkipsSavedPartsAndPublishesOnlyVerifiedFile() async throws {
        let data = Data(repeating: 42, count: Int(ResumableModelDownload.partSize) + 7)
        let file = BuiltInModel.File(name: "fixture", size: Int64(data.count), digest: SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined(), sha256: true)
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let target = root.appendingPathComponent(file.name)
        let parts = target.appendingPathExtension("parts")
        try FileManager.default.createDirectory(at: parts, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        try data.prefix(Int(ResumableModelDownload.partSize)).write(to: parts.appendingPathComponent("0"))
        XCTAssertEqual(ResumableModelDownload.retainedBytes(file: file, target: target), ResumableModelDownload.partSize)
        let session = URLSession(configuration: .ephemeral)
        defer { session.invalidateAndCancel() }
        // Simulate interruption after the first saved chunk. No installation is published.
        do {
            try await ResumableModelDownload.fetch(file: file, target: target, session: session, transport: { _, _, _ in
                throw URLError(.badServerResponse)
            }) { _, _ in }
            XCTFail("Should fail")
        } catch {}
        XCTAssertFalse(FileManager.default.fileExists(atPath: target.path))
        XCTAssertEqual(ResumableModelDownload.retainedBytes(file: file, target: target), ResumableModelDownload.partSize)
        try await ResumableModelDownload.fetch(file: file, target: target, session: session, transport: { request, count, update in
            XCTAssertEqual(request.value(forHTTPHeaderField: "Range"), "bytes=16777216-16777222")
            XCTAssertEqual(count, 7)
            let temp = root.appendingPathComponent(UUID().uuidString)
            try data.suffix(7).write(to: temp)
            update(7)
            return (temp, HTTPURLResponse(url: request.url!, statusCode: 206, httpVersion: nil, headerFields: ["Content-Range": "bytes 16777216-16777222/16777223"])!)
        }) { _, _ in }
        XCTAssertEqual(try Data(contentsOf: target), data)
        XCTAssertFalse(FileManager.default.fileExists(atPath: parts.path))
    }

    func testDamagedSavedSegmentsAreClearedAfterHashFailure() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let target = root.appendingPathComponent("fixture")
        let parts = target.appendingPathExtension("parts")
        try FileManager.default.createDirectory(at: parts, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        try Data([1, 2, 3]).write(to: parts.appendingPathComponent("0"))
        let file = BuiltInModel.File(name: "fixture", size: 3, digest: "invalid", sha256: true)
        let session = URLSession(configuration: .ephemeral)
        defer { session.invalidateAndCancel() }
        do {
            try await ResumableModelDownload.fetch(file: file, target: target, session: session, transport: { _, _, _ in
                XCTFail("Saved segment should avoid network")
                throw URLError(.badURL)
            }) { _, _ in }
            XCTFail("Corrupt data must not be published")
        } catch {}
        XCTAssertFalse(FileManager.default.fileExists(atPath: target.path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: parts.path))
    }
}
