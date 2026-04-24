import Foundation

struct Song: Identifiable, Hashable, Codable {
    let id: String
    let name: String
    let singer: String
    let rhythm: String
    let zipUrl: String
}

enum SongCatalogError: LocalizedError {
    case http(Int)
    case api(String)
    case decode(String)

    var errorDescription: String? {
        switch self {
        case .http(let c): return "HTTP \(c)"
        case .api(let m): return m
        case .decode(let m): return "decode: \(m)"
        }
    }
}

enum SongCatalog {
    static let baseURL = URL(string: "http://210.22.95.26:30027")!
    private static let pageSize = 100
    private static let maxPages = 50

    static func fetchAll() async throws -> [Song] {
        var acc: [Song] = []
        for page in 1...maxPages {
            let result = try await fetchPage(pageNo: page)
            acc.append(contentsOf: result.songs)
            if page >= result.totalPages { break }
        }
        return acc
    }

    private struct Page {
        let songs: [Song]
        let totalPages: Int
    }

    private static func fetchPage(pageNo: Int) async throws -> Page {
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)!
        components.path = "/api/audio/querySongInfo"
        components.queryItems = [
            URLQueryItem(name: "pageNo", value: String(pageNo)),
            URLQueryItem(name: "pageSize", value: String(pageSize)),
        ]
        var request = URLRequest(url: components.url!)
        request.timeoutInterval = 15
        request.httpMethod = "GET"

        let (data, response) = try await URLSession.shared.data(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw SongCatalogError.api("no response")
        }
        guard http.statusCode == 200 else {
            throw SongCatalogError.http(http.statusCode)
        }
        return try parse(data: data)
    }

    private static func parse(data: Data) throws -> Page {
        struct Envelope: Decodable {
            let code: Int
            let message: String?
            let data: Payload?
        }
        struct Payload: Decodable {
            let totalPages: Int?
            let content: [Item]?
        }
        struct Item: Decodable {
            let id: String?
            let name: String?
            let singer: String?
            let rhythm: String?
            let resourceUrl: String?
        }

        let envelope: Envelope
        do {
            envelope = try JSONDecoder().decode(Envelope.self, from: data)
        } catch {
            throw SongCatalogError.decode(error.localizedDescription)
        }
        guard envelope.code == 0 else {
            throw SongCatalogError.api(envelope.message ?? "code \(envelope.code)")
        }
        guard let payload = envelope.data, let items = payload.content else {
            throw SongCatalogError.api("missing data")
        }
        let songs = items.compactMap { item -> Song? in
            guard let id = item.id, !id.isEmpty,
                  let name = item.name, !name.isEmpty,
                  let zipUrl = item.resourceUrl, !zipUrl.isEmpty else { return nil }
            return Song(
                id: id,
                name: name,
                singer: item.singer ?? "",
                rhythm: item.rhythm ?? "",
                zipUrl: zipUrl
            )
        }
        return Page(songs: songs, totalPages: max(payload.totalPages ?? 1, 1))
    }
}
