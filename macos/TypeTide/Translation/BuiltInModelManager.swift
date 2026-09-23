import Foundation
import Combine

@MainActor
final class BuiltInModelManager: ObservableObject {
    static let shared = BuiltInModelManager()
    @Published private(set) var installed = BuiltInModel.isInstalled()
    @Published private(set) var isDownloading = false
    @Published private(set) var progress = 0.0
    @Published private(set) var status = ""
    @Published private(set) var error: String?
    private var downloadTask: Task<Void, Never>?

    func download() {
        guard !isDownloading, !installed, BuiltInModel.supported else { return }
        isDownloading = true
        error = nil
        progress = 0
        status = "Preparing download…"
        downloadTask = Task {
            do {
                try await Self.install { fraction, message in
                    Task { @MainActor in
                        guard self.isDownloading else { return }
                        self.progress = fraction
                        self.status = message
                    }
                }
                installed = BuiltInModel.isInstalled()
                status = "Ready for offline translation"
            } catch {
                if Task.isCancelled || (error as? URLError)?.code == .cancelled {
                    status = "Download cancelled. Completed files are kept for retry."
                } else {
                    self.error = error.localizedDescription
                    status = "Download failed"
                }
            }
            isDownloading = false
            downloadTask = nil
        }
    }

    func cancel() { downloadTask?.cancel() }

    /// All network access is confined to explicit model installation, never inference.
    @concurrent
    nonisolated static func install(
        at destination: URL = BuiltInModel.directory,
        progress: @escaping @Sendable (Double, String) -> Void
    ) async throws {
        let fm = FileManager.default
        let staging = destination.appendingPathExtension("download")
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        var completed: Int64 = 0
        for file in BuiltInModel.files {
            try Task.checkCancellation()
            let target = staging.appendingPathComponent(file.name)
            let priorBytes = completed
            if (try? BuiltInModel.validate(target, file: file)) == nil {
                try Task.checkCancellation()
                let delegate = ModelDownloadProgress { written in
                    progress(Double(priorBytes + written) / Double(BuiltInModel.totalBytes), "Downloading \(file.name)…")
                }
                let config = URLSessionConfiguration.ephemeral
                config.timeoutIntervalForRequest = 60
                config.timeoutIntervalForResource = 60 * 60
                let session = URLSession(configuration: config)
                defer { session.invalidateAndCancel() }
                let (temporary, response) = try await session.download(from: file.url, delegate: delegate)
                defer { try? fm.removeItem(at: temporary) }
                guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
                    throw TranslationError.network("Model download failed (HTTP \((response as? HTTPURLResponse)?.statusCode ?? 0)). Please retry.")
                }
                progress(Double(completed) / Double(BuiltInModel.totalBytes), "Verifying \(file.name)…")
                try BuiltInModel.validate(temporary, file: file)
                if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
                try fm.moveItem(at: temporary, to: target)
            }
            completed += file.size
            progress(Double(completed) / Double(BuiltInModel.totalBytes), "Verified \(file.name)")
        }
        try Task.checkCancellation()
        try Data(BuiltInModel.revision.utf8).write(to: staging.appendingPathComponent("complete"), options: .atomic)
        // Publish only a complete, verified installation.
        if fm.fileExists(atPath: destination.path) { try fm.removeItem(at: destination) }
        try fm.moveItem(at: staging, to: destination)
    }
}

private final class ModelDownloadProgress: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    private let update: @Sendable (Int64) -> Void
    init(update: @escaping @Sendable (Int64) -> Void) { self.update = update }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64, totalBytesWritten: Int64,
                    totalBytesExpectedToWrite: Int64) { update(totalBytesWritten) }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didFinishDownloadingTo location: URL) {}
}
