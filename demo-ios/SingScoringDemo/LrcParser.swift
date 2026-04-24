import Foundation

struct LrcLine: Hashable {
    let timeMs: Int
    let text: String
}

enum LrcParser {
    private static let tagRegex: NSRegularExpression = {
        try! NSRegularExpression(pattern: #"\[(\d+):(\d+)(?:\.(\d+))?\]"#)
    }()

    static func parse(_ input: String) -> [LrcLine] {
        var out: [LrcLine] = []
        for rawLine in input.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" }) {
            var line = String(rawLine)
            if line.hasSuffix("\r") { line.removeLast() }
            if line.isEmpty { continue }

            let range = NSRange(line.startIndex..<line.endIndex, in: line)
            let matches = tagRegex.matches(in: line, options: [], range: range)
            if matches.isEmpty { continue }

            let lastMatch = matches.last!.range
            let textStart = line.index(line.startIndex, offsetBy: lastMatch.location + lastMatch.length)
            let text = line[textStart...].trimmingCharacters(in: .whitespaces)
            if text.isEmpty { continue }

            for m in matches {
                let mm = Int(capture(line, m.range(at: 1))) ?? 0
                let ss = Int(capture(line, m.range(at: 2))) ?? 0
                let fracStr = m.range(at: 3).location == NSNotFound ? "" : capture(line, m.range(at: 3))
                let fracMs: Int
                switch fracStr.count {
                case 0: fracMs = 0
                case 1: fracMs = (Int(fracStr) ?? 0) * 100
                case 2: fracMs = (Int(fracStr) ?? 0) * 10
                case 3: fracMs = Int(fracStr) ?? 0
                default: fracMs = Int(fracStr.prefix(3)) ?? 0
                }
                out.append(LrcLine(timeMs: mm * 60_000 + ss * 1_000 + fracMs, text: text))
            }
        }
        return out.sorted { $0.timeMs < $1.timeMs }
    }

    private static func capture(_ s: String, _ nsr: NSRange) -> String {
        guard nsr.location != NSNotFound, let r = Range(nsr, in: s) else { return "" }
        return String(s[r])
    }
}
