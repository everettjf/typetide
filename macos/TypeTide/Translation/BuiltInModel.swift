import Foundation
import CryptoKit

/// Pinned text-only TranslateGemma weights. Never download user-supplied code.
nonisolated enum BuiltInModel {
    static let repository = "mlx-community/translategemma-4b-it-4bit"
    static let revision = "5788ec08c047f3f2e17808101b8d9566ac930d58"
    static let name = "TranslateGemma 4B · 4-bit"
    static var supported: Bool {
#if arch(arm64)
        return true
#else
        return false
#endif
    }
    static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("TypeTide/Models/translategemma-4b-\(revision)")
    }
    struct File: Sendable {
        let name: String
        let size: Int64
        let digest: String
        let sha256: Bool
        var url: URL {
            URL(string: "https://huggingface.co/\(repository)/resolve/\(revision)/\(name)")!
        }
    }
    static let files: [File] = [
        .init(name: "added_tokens.json", size: 35, digest: "e17bde03d42feda32d1abfca6d3b598b9a020df7", sha256: false),
        .init(name: "config.json", size: 2815, digest: "0e99f7ca3dfb3669512bad9a8cad9309bc695e4e", sha256: false),
        .init(name: "generation_config.json", size: 128, digest: "2f21cf06d0ec7276182c026cdca4787c59cdc4d0", sha256: false),
        .init(name: "model.safetensors", size: 2183295977, digest: "113acb0c29997a3015af84bec2c8f967cb7b15f8959d1c26b9628b921e324c40", sha256: true),
        .init(name: "model.safetensors.index.json", size: 79768, digest: "b5177528e6593460b36e13bcc3c74b133f5bb74f", sha256: false),
        .init(name: "special_tokens_map.json", size: 662, digest: "1a6193244714d3d78be48666cb02cdbfac62ad86", sha256: false),
        .init(name: "tokenizer.json", size: 33384568, digest: "4667f2089529e8e7657cfb6d1c19910ae71ff5f28aa7ab2ff2763330affad795", sha256: true),
        .init(name: "tokenizer_config.json", size: 1155413, digest: "1808e290e254686735ff6ee74dc147b61da1aa20", sha256: false),
    ]
    static var totalBytes: Int64 { files.reduce(0) { $0 + $1.size } }

    static func isInstalled(at directory: URL = directory) -> Bool {
        guard (try? String(contentsOf: directory.appendingPathComponent("complete"), encoding: .utf8)) == revision else { return false }
        return files.allSatisfy { file in
            let attributes = try? FileManager.default.attributesOfItem(atPath: directory.appendingPathComponent(file.name).path)
            return (attributes?[.size] as? NSNumber)?.int64Value == file.size
        }
    }

    /// Bounded memory validation, including Git blob hashes for small configuration files.
    static func validate(_ url: URL, file: File) throws {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var sha256 = SHA256()
        var sha1 = Insecure.SHA1()
        if !file.sha256 { sha1.update(data: Data("blob \(file.size)\0".utf8)) }
        var count: Int64 = 0
        while let data = try handle.read(upToCount: 1024 * 1024), !data.isEmpty {
            try Task.checkCancellation()
            count += Int64(data.count)
            if file.sha256 { sha256.update(data: data) } else { sha1.update(data: data) }
        }
        let hash = file.sha256 ? sha256.finalize().map { String(format: "%02x", $0) }.joined()
            : sha1.finalize().map { String(format: "%02x", $0) }.joined()
        guard count == file.size, hash == file.digest else {
            throw TranslationError.notConfigured("Model file verification failed (\(file.name)). Please retry the download.")
        }
    }
}
