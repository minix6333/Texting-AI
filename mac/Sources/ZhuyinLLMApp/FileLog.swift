import Foundation

/// Minimal append-only log at ~/Library/Logs/ZhuyinLLM.log — events and timings only, never typed text.
enum FileLog {
    private static let queue = DispatchQueue(label: "com.minix.inputmethod.ZhuyinLLM.log")
    private static let url = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/ZhuyinLLM.log")
    private static let formatter: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return f
    }()

    static func write(_ message: String) {
        let line = "\(formatter.string(from: Date())) \(message)\n"
        queue.async {
            if let h = try? FileHandle(forWritingTo: url) {
                h.seekToEndOfFile()
                h.write(line.data(using: .utf8)!)
                try? h.close()
            } else {
                try? line.data(using: .utf8)!.write(to: url)
            }
        }
    }
}
