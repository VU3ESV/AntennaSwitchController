import Foundation

/// The firmware asset of a published GitHub release.
struct FirmwareRelease: Equatable {
    let tag: String           // e.g. "v0.1.33"
    let assetName: String
    let downloadURL: URL
    let size: Int
    let pageURL: URL?

    var version: String { FirmwareVersion.normalized(tag) }
}

enum FirmwareReleaseError: LocalizedError {
    case noFirmwareAsset(String)
    case http(Int)
    case transport(String)

    var errorDescription: String? {
        switch self {
        case .noFirmwareAsset(let tag):
            return "Release \(tag) has no \(FirmwareReleaseClient.assetName) to download."
        case .http(let code):
            return "GitHub returned HTTP \(code)."
        case .transport(let m):
            return "Couldn’t reach GitHub: \(m)"
        }
    }
}

/// Finds and downloads the controller firmware from this project's GitHub
/// Releases (public repo, unauthenticated API). Every release ships the image as
/// `AntennaSwitchController-firmware.bin` (see .github/workflows/release.yml).
struct FirmwareReleaseClient {
    static let latestURL = URL(string: "https://api.github.com/repos/VU3ESV/AntennaSwitchController/releases/latest")!
    static let assetName = "AntennaSwitchController-firmware.bin"

    var session: URLSession = .shared

    func latest() async throws -> FirmwareRelease {
        var request = URLRequest(url: Self.latestURL, timeoutInterval: 15)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        let data = try await fetch(request)
        let release: GitHubRelease
        do { release = try JSONDecoder().decode(GitHubRelease.self, from: data) } catch {
            throw FirmwareReleaseError.transport("unexpected response (\(error.localizedDescription))")
        }
        return try Self.firmware(in: release)
    }

    func download(_ release: FirmwareRelease) async throws -> Data {
        try await fetch(URLRequest(url: release.downloadURL, timeoutInterval: 60))
    }

    /// Picks the firmware asset out of a decoded release (split out for tests).
    static func firmware(in release: GitHubRelease) throws -> FirmwareRelease {
        guard let asset = release.assets.first(where: { $0.name == assetName }),
              let url = URL(string: asset.browserDownloadURL)
        else { throw FirmwareReleaseError.noFirmwareAsset(release.tagName) }
        return FirmwareRelease(tag: release.tagName, assetName: asset.name, downloadURL: url,
                               size: asset.size, pageURL: release.htmlURL.flatMap(URL.init(string:)))
    }

    private func fetch(_ request: URLRequest) async throws -> Data {
        let data: Data, response: URLResponse
        do { (data, response) = try await session.data(for: request) } catch {
            throw FirmwareReleaseError.transport(error.localizedDescription)
        }
        if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            throw FirmwareReleaseError.http(http.statusCode)
        }
        return data
    }

    struct GitHubRelease: Decodable {
        let tagName: String
        let htmlURL: String?
        let assets: [Asset]

        struct Asset: Decodable {
            let name: String
            let size: Int
            let browserDownloadURL: String
            enum CodingKeys: String, CodingKey {
                case name, size
                case browserDownloadURL = "browser_download_url"
            }
        }

        enum CodingKeys: String, CodingKey {
            case tagName = "tag_name"
            case htmlURL = "html_url"
            case assets
        }
    }
}
