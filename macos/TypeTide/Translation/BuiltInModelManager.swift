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
    private var operation = UUID()
    private var sample: (Date, Double)?
    @Published private(set) var transferDetail = ""
    private var downloadTask: Task<Void, Never>?

    private init() {
        guard !installed else { return }
        let staging = BuiltInModel.directory.appendingPathExtension("download")
        let saved = BuiltInModel.files.reduce(Int64(0)) {
            $0 + ResumableModelDownload.retainedBytes(file: $1, target: staging.appendingPathComponent($1.name))
        }
        if saved > 0 {
            progress = Double(saved) / Double(BuiltInModel.totalBytes)
            status = "Saved download found. Resume to finish installation."
        }
    }

    func download() {
        guard !isDownloading, !installed, BuiltInModel.supported else { return }
        let operation = UUID()
        self.operation = operation
        isDownloading = true
        error = nil
        sample = nil
        transferDetail = ""
        status = "Preparing download…"
        downloadTask = Task {
            do {
                try await Self.install { fraction, message in
                    Task { @MainActor in
                        guard self.isDownloading, self.operation == operation else { return }
                        let now = Date()
                        if let (time, previous) = self.sample, now.timeIntervalSince(time) >= 1 {
                            let rate = max(0, fraction - previous) * Double(BuiltInModel.totalBytes) / now.timeIntervalSince(time)
                            self.transferDetail = ByteCountFormatter.string(fromByteCount: Int64(fraction * Double(BuiltInModel.totalBytes)), countStyle: .decimal) + " / 2.22 GB · " + ByteCountFormatter.string(fromByteCount: Int64(rate), countStyle: .decimal) + "/s"
                            if rate > 0, fraction < 1, message.hasPrefix("Downloading") {
                                let seconds = Int((1 - fraction) * Double(BuiltInModel.totalBytes) / rate)
                                self.transferDetail += seconds < 60 ? " · < 1 min remaining" : " · ~\(max(1, seconds / 60)) min remaining"
                            }
                            self.sample = (now, fraction)
                        } else if self.sample == nil { self.sample = (now, fraction) }
                        self.progress = fraction
                        self.status = message
                    }
                }
                installed = BuiltInModel.isInstalled()
                status = "Ready for offline translation"
            } catch {
                if Task.isCancelled {
                    status = "Download paused. Saved segments will resume, even after restarting TypeTide."
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
        try Task.checkCancellation()
        let fm = FileManager.default
        let staging = destination.appendingPathExtension("download")
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 45
        config.timeoutIntervalForResource = 15 * 60
        config.httpMaximumConnectionsPerHost = 2
        let session = URLSession(configuration: config)
        defer { session.invalidateAndCancel() }
        var retained = BuiltInModel.files.map { ResumableModelDownload.retainedBytes(file: $0, target: staging.appendingPathComponent($0.name)) }
        let required = BuiltInModel.totalBytes - retained.reduce(0, +) + (BuiltInModel.files.map(\.size).max() ?? 0)
        if let available = try? staging.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]).volumeAvailableCapacityForImportantUsage,
           available < required {
            throw TranslationError.notConfigured("Not enough disk space. Free about " + ByteCountFormatter.string(fromByteCount: required, countStyle: .decimal) + " to download and verify the model. Saved progress is kept.")
        }
        progress(Double(retained.reduce(0, +)) / Double(BuiltInModel.totalBytes), "Checking saved download…")
        for (index, file) in BuiltInModel.files.enumerated() {
            try Task.checkCancellation()
            let target = staging.appendingPathComponent(file.name)
            if (try? BuiltInModel.validate(target, file: file)) == nil {
                try Task.checkCancellation()
                retained[index] = 0
                let otherBytes = retained.reduce(0, +)
                try await ResumableModelDownload.fetch(file: file, target: target, session: session) { written, message in
                    progress(Double(otherBytes + written) / Double(BuiltInModel.totalBytes), message)
                }
            }
            retained[index] = file.size
            progress(Double(retained.reduce(0, +)) / Double(BuiltInModel.totalBytes), "Verified \(file.name)")
        }
        try Task.checkCancellation()
        try Data(BuiltInModel.revision.utf8).write(to: staging.appendingPathComponent("complete"), options: .atomic)
        // Publish only a complete, verified installation.
        if fm.fileExists(atPath: destination.path) { try fm.removeItem(at: destination) }
        try fm.moveItem(at: staging, to: destination)
    }
}
