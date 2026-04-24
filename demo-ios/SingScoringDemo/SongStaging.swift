import Foundation
import ZIPFoundation

struct StagedSong {
    let zipURL: URL
    let chorusMP3URL: URL
}

enum StagingError: LocalizedError {
    case http(Int)
    case badURL(String)
    case missingChorusMP3
    case io(String)

    var errorDescription: String? {
        switch self {
        case .http(let c): return "HTTP \(c) downloading zip"
        case .badURL(let s): return "bad zipUrl: \(s)"
        case .missingChorusMP3: return "zip has no *_chorus.mp3 entry"
        case .io(let m): return m
        }
    }
}

enum SongStaging {
    static func songDirectory(for song: Song) -> URL {
        let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first!
        let dir = base.appendingPathComponent("songs", isDirectory: true)
                      .appendingPathComponent(song.id, isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func download(_ song: Song) async throws -> URL {
        let dir = songDirectory(for: song)
        let zip = dir.appendingPathComponent("\(song.id).zip")
        let part = dir.appendingPathComponent("\(song.id).zip.part")

        if let attrs = try? FileManager.default.attributesOfItem(atPath: zip.path),
           let size = attrs[.size] as? NSNumber, size.intValue > 0 {
            return zip
        }

        try? FileManager.default.removeItem(at: part)

        guard let url = URL(string: song.zipUrl) else {
            throw StagingError.badURL(song.zipUrl)
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 60
        let (tempURL, response) = try await URLSession.shared.download(for: request)
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            try? FileManager.default.removeItem(at: tempURL)
            throw StagingError.http(http.statusCode)
        }

        try FileManager.default.moveItem(at: tempURL, to: part)
        try FileManager.default.moveItem(at: part, to: zip)
        return zip
    }

    static func stage(_ song: Song) throws -> StagedSong {
        let dir = songDirectory(for: song)
        let zip = dir.appendingPathComponent("\(song.id).zip")
        let mp3 = dir.appendingPathComponent("\(song.id)_chorus.mp3")

        guard FileManager.default.fileExists(atPath: zip.path) else {
            throw StagingError.io("stage called before download: \(zip.path)")
        }

        if !FileManager.default.fileExists(atPath: mp3.path) {
            let archive = try Archive(url: zip, accessMode: .read)
            guard let entry = archive.first(where: { $0.path.hasSuffix("_chorus.mp3") }) else {
                throw StagingError.missingChorusMP3
            }
            _ = try archive.extract(entry, to: mp3)
        }
        return StagedSong(zipURL: zip, chorusMP3URL: mp3)
    }

    static func readLyrics(for song: Song) -> [LrcLine] {
        let dir = songDirectory(for: song)
        let zip = dir.appendingPathComponent("\(song.id).zip")
        guard FileManager.default.fileExists(atPath: zip.path),
              let archive = try? Archive(url: zip, accessMode: .read) else {
            return []
        }
        let target = "\(song.id)_chorus.lrc"
        guard let entry = archive.first(where: { $0.path.hasSuffix(target) }) else {
            return []
        }
        var data = Data()
        data.reserveCapacity(Int(entry.uncompressedSize))
        do {
            _ = try archive.extract(entry) { chunk in data.append(chunk) }
        } catch {
            return []
        }
        guard let text = String(data: data, encoding: .utf8) else { return [] }
        return LrcParser.parse(text)
    }
}
