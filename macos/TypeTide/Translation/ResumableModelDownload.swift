import Foundation

/// A bounded range downloader. Completed parts survive cancellation and relaunch;
/// only two 16 MiB ranges are in flight, and the final pinned hash is still required.
nonisolated enum ResumableModelDownload {
    static let partSize: Int64 = 16 * 1024 * 1024
    struct Part: Sendable {
        let index: Int
        let start: Int64
        let end: Int64
        var size: Int64 { end - start + 1 }
        var range: String { "bytes=\(start)-\(end)" }
    }
    static func parts(size: Int64) -> [Part] {
        guard size > 0 else { return [] }
        return stride(from: Int64(0), to: size, by: Int(partSize)).enumerated().map {
            Part(index: $0.offset, start: $0.element, end: min(size - 1, $0.element + partSize - 1))
        }
    }
    static func size(at url: URL) -> Int64 {
        ((try? FileManager.default.attributesOfItem(atPath: url.path))?[.size] as? NSNumber)?.int64Value ?? 0
    }
    static func retainedBytes(file: BuiltInModel.File, target: URL) -> Int64 {
        if size(at: target) == file.size { return file.size }
        let directory = target.appendingPathExtension("parts")
        return parts(size: file.size).reduce(0) { result, part in
            result + (size(at: directory.appendingPathComponent("\(part.index)")) == part.size ? part.size : 0)
        }
    }
    static func validateResponse(_ response: HTTPURLResponse, part: Part, total: Int64) throws {
        guard response.statusCode == 206,
              response.value(forHTTPHeaderField: "Content-Range") == "bytes \(part.start)-\(part.end)/\(total)" else {
            throw DownloadFailure.response(response.statusCode)
        }
    }
    enum DownloadFailure: LocalizedError {
        case response(Int), invalidPart, corrupt
        var errorDescription: String? {
            switch self {
            case .response(let status): return "The download server returned an unexpected response (HTTP \(status)). Saved progress is kept. Please retry."
            case .invalidPart: return "An incomplete model segment was received. Saved progress is kept. Please retry."
            case .corrupt: return "Model verification failed. The damaged file was cleared; retry to download it again."
            }
        }
    }

    typealias Transport = @Sendable (URLRequest, Int64, @escaping @Sendable (Int64) -> Void) async throws -> (URL, URLResponse)

    @concurrent
    static func fetch(file: BuiltInModel.File, target: URL,
                      session: URLSession, transport: Transport? = nil, progress: @escaping @Sendable (Int64, String) -> Void) async throws {
        let fm = FileManager.default
        let directory = target.appendingPathExtension("parts")
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let all = parts(size: file.size)
        let meter = RangeProgress(total: file.size, progress: progress, filename: file.name)
        var pending = [Part]()
        for part in all {
            if size(at: directory.appendingPathComponent("\(part.index)")) == part.size {
                meter.update(part.index, bytes: part.size)
            } else { pending.append(part) }
        }
        try await withThrowingTaskGroup(of: Void.self) { group in
            var next = 0
            func enqueue(_ part: Part) {
                group.addTask {
                    let output = directory.appendingPathComponent("\(part.index)")
                    for attempt in 0..<3 {
                        try Task.checkCancellation()
                        meter.update(part.index, bytes: 0, status: attempt == 0 ? "Connecting to download server…" : "Connection interrupted. Retrying…")
                        do {
                            var request = URLRequest(url: file.url)
                            request.setValue(part.range, forHTTPHeaderField: "Range")
                            request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
                            request.cachePolicy = .reloadIgnoringLocalCacheData
                            let update: @Sendable (Int64) -> Void = { bytes in meter.update(part.index, bytes: min(bytes, part.size)) }
                            let (temporary, response): (URL, URLResponse)
                            if let transport { (temporary, response) = try await transport(request, part.size, update) }
                            else {
                                let delegate = RangeDownloadProgress(limit: part.size, update: update)
                                (temporary, response) = try await session.download(for: request, delegate: delegate)
                            }
                            defer { try? fm.removeItem(at: temporary) }
                            guard let http = response as? HTTPURLResponse else { throw DownloadFailure.invalidPart }
                            // Small metadata files may be served whole without Range support.
                            if !(all.count == 1 && http.statusCode == 200) {
                                try validateResponse(http, part: part, total: file.size)
                            }
                            guard size(at: temporary) == part.size else { throw DownloadFailure.invalidPart }
                            try Task.checkCancellation()
                            if fm.fileExists(atPath: output.path) { try fm.removeItem(at: output) }
                            try fm.moveItem(at: temporary, to: output)
                            meter.update(part.index, bytes: part.size)
                            return
                        } catch {
                            meter.update(part.index, bytes: 0)
                            try Task.checkCancellation()
                            if (error as? URLError)?.code == .cancelled { throw DownloadFailure.invalidPart }
                            guard attempt < 2, retryable(error) else { throw error }
                            try await Task.sleep(for: .seconds(Double(1 << attempt)))
                        }
                    }
                }
            }
            while next < min(2, pending.count) { enqueue(pending[next]); next += 1 }
            while try await group.next() != nil {
                if next < pending.count { enqueue(pending[next]); next += 1 }
            }
        }
        try Task.checkCancellation()
        progress(file.size, "Verifying \(file.name)…")
        let assembled = target.appendingPathExtension("assembling")
        // An interrupted assembly can always be rebuilt from complete parts.
        _ = fm.createFile(atPath: assembled.path, contents: nil)
        let output = try FileHandle(forWritingTo: assembled)
        defer { try? output.close(); try? fm.removeItem(at: assembled) }
        try output.truncate(atOffset: 0)
        for part in all {
            let input = try FileHandle(forReadingFrom: directory.appendingPathComponent("\(part.index)"))
            defer { try? input.close() }
            while let data = try input.read(upToCount: 1024 * 1024), !data.isEmpty {
                try Task.checkCancellation()
                try output.write(contentsOf: data)
            }
        }
        try output.synchronize()
        do { try BuiltInModel.validate(assembled, file: file) }
        catch is CancellationError { throw CancellationError() }
        catch {
            try? fm.removeItem(at: directory)
            throw DownloadFailure.corrupt
        }
        if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
        try fm.moveItem(at: assembled, to: target)
        try fm.removeItem(at: directory)
    }
    static func retryable(_ error: Error) -> Bool {
        if let e = error as? URLError {
            return [.timedOut, .networkConnectionLost, .notConnectedToInternet, .cannotConnectToHost, .cannotFindHost].contains(e.code)
        }
        if case DownloadFailure.response(let status) = error { return status == 429 || status >= 500 }
        return false
    }
}

/// URLSession delegates can run on different queues. Serialize accounting and
/// callbacks so the UI never receives an older snapshot after a newer one.
nonisolated private final class RangeProgress: @unchecked Sendable {
    private let lock = NSLock()
    private var bytes = [Int: Int64]()
    private var lastEmission = Date.distantPast
    private let total: Int64
    private let progress: @Sendable (Int64, String) -> Void
    private let filename: String
    init(total: Int64, progress: @escaping @Sendable (Int64, String) -> Void, filename: String) {
        self.total = total; self.progress = progress; self.filename = filename
    }
    func update(_ part: Int, bytes count: Int64, status: String? = nil) {
        lock.lock(); defer { lock.unlock() }
        bytes[part] = count
        let sum = min(total, bytes.values.reduce(0, +))
        if status != nil || sum == total || Date().timeIntervalSince(lastEmission) >= 0.2 {
            lastEmission = Date()
            progress(sum, status ?? "Downloading \(filename)…")
        }
    }
}
nonisolated private final class RangeDownloadProgress: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    private let limit: Int64
    private let update: @Sendable (Int64) -> Void
    init(limit: Int64, update: @escaping @Sendable (Int64) -> Void) { self.limit = limit; self.update = update }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64, totalBytesWritten: Int64,
                    totalBytesExpectedToWrite: Int64) {
        guard totalBytesExpectedToWrite <= limit, totalBytesWritten <= limit else {
            downloadTask.cancel()
            return
        }
        update(totalBytesWritten)
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {}
}
