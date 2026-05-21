import Foundation

struct AutoEqItem: Codable, Identifiable, Hashable {
    var id: String { path }
    let path: String      // Relative path under AutoEq master, e.g. "results/oratory1990/.../ParametricEQ.txt"
    let name: String      // Clean headphone name, e.g. "Sennheiser HD 600"
    let source: String    // E.g. "oratory1990", "rtings", "innerfidelity"
}

struct GitHubTreeResponse: Codable {
    let tree: [GitHubTreeEntry]
    let truncated: Bool
}

struct GitHubTreeEntry: Codable {
    let path: String
    let type: String
}

class AutoEqService: ObservableObject {
    static let shared = AutoEqService()

    @Published var isIndexing: Bool = false
    @Published var isSearching: Bool = false
    @Published var indexLoaded: Bool = false
    @Published var searchResults: [AutoEqItem] = []
    
    private var allItems: [AutoEqItem] = []
    private let cacheURL: URL

    private init() {
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        cacheURL = appSupport.appendingPathComponent("Krisha").appendingPathComponent("autoeq_index.json")
        loadCachedIndex()
    }

    /// Load the cached AutoEq index if available
    func loadCachedIndex() {
        guard FileManager.default.fileExists(atPath: cacheURL.path) else { return }
        do {
            let data = try Data(contentsOf: cacheURL)
            let items = try JSONDecoder().decode([AutoEqItem].self, from: data)
            self.allItems = items
            self.indexLoaded = !items.isEmpty
            print("[AutoEqService] Loaded \(items.count) items from cache.")
        } catch {
            print("[AutoEqService] Failed to load cached index: \(error)")
        }
    }

    /// Recursively indexes the jaakkopasanen/AutoEq repository via GitHub API
    func fetchAndIndex() async {
        guard !isIndexing else { return }
        
        await MainActor.run {
            isIndexing = true
        }

        let url = URL(string: "https://api.github.com/repos/jaakkopasanen/AutoEq/git/trees/master?recursive=1")!
        var request = URLRequest(url: url)
        request.setValue("KrishaApp/1.0.0 (macOS; Equalizer)", forHTTPHeaderField: "User-Agent")

        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
                let code = (response as? HTTPURLResponse)?.statusCode ?? -1
                print("[AutoEqService] GitHub API error: Status \(code)")
                
                // If rate limited or network failure, load fallbacks if allItems is empty
                if allItems.isEmpty {
                    setupFallbackItems()
                }
                
                await MainActor.run {
                    isIndexing = false
                }
                return
            }

            let gitTree = try JSONDecoder().decode(GitHubTreeResponse.self, from: data)
            
            // Parse entries to find only "results/.../ParametricEQ.txt" files
            var items: [AutoEqItem] = []
            for entry in gitTree.tree {
                if entry.type == "blob" && entry.path.hasSuffix("ParametricEQ.txt") && entry.path.starts(with: "results/") {
                    // Path looks like: "results/oratory1990/harman_over-ear_2018/Sennheiser HD 600/ParametricEQ.txt"
                    let components = entry.path.components(separatedBy: "/")
                    guard components.count >= 4 else { continue }
                    
                    let source = components[1] // E.g. "oratory1990"
                    let cleanName = components[components.count - 2] // The folder name right before the file
                    
                    items.append(AutoEqItem(
                        path: entry.path,
                        name: cleanName,
                        source: source
                    ))
                }
            }

            // Save to disk
            try? FileManager.default.createDirectory(at: cacheURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            let encodedData = try JSONEncoder().encode(items)
            try encodedData.write(to: cacheURL, options: .atomic)

            await MainActor.run {
                self.allItems = items
                self.indexLoaded = true
                self.isIndexing = false
                print("[AutoEqService] Successfully indexed \(items.count) headphones.")
            }
        } catch {
            print("[AutoEqService] Sync failed: \(error)")
            if allItems.isEmpty {
                setupFallbackItems()
            }
            await MainActor.run {
                self.isIndexing = false
            }
        }
    }

    /// Provide a few highly popular headphones as fallback if network/GitHub API is offline or rate-limited
    private func setupFallbackItems() {
        let fallbacks = [
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Sennheiser HD 600/ParametricEQ.txt", name: "Sennheiser HD 600", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Sennheiser HD 650/ParametricEQ.txt", name: "Sennheiser HD 650", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Sennheiser HD 800 S/ParametricEQ.txt", name: "Sennheiser HD 800 S", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Beyerdynamic DT 770 80 Ohm/ParametricEQ.txt", name: "Beyerdynamic DT 770 80 Ohm", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Beyerdynamic DT 990 Pro/ParametricEQ.txt", name: "Beyerdynamic DT 990 Pro", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Audio-Technica ATH-M50x/ParametricEQ.txt", name: "Audio-Technica ATH-M50x", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/HIFIMAN Sundara/ParametricEQ.txt", name: "HIFIMAN Sundara", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Sony WH-1000XM4/ParametricEQ.txt", name: "Sony WH-1000XM4", source: "oratory1990"),
            AutoEqItem(path: "results/oratory1990/harman_over-ear_2018/Apple AirPods Max/ParametricEQ.txt", name: "Apple AirPods Max", source: "oratory1990")
        ]
        self.allItems = fallbacks
        self.indexLoaded = true
    }

    /// Perform a fast in-memory search across indexed items
    func search(query: String) {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            self.searchResults = []
            return
        }

        let filtered = allItems.filter { item in
            item.name.localizedCaseInsensitiveContains(trimmed)
        }
        
        self.searchResults = Array(filtered.prefix(15)) // Limit results to top 15 for premium performance
    }

    /// Download and parse standard ParametricEQ.txt for a headphone item
    func downloadPreset(for item: AutoEqItem) async -> EQPreset? {
        let pathEscaped = item.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? item.path
        let urlString = "https://raw.githubusercontent.com/jaakkopasanen/AutoEq/master/\(pathEscaped)"
        guard let url = URL(string: urlString) else { return nil }

        print("[AutoEqService] Downloading AutoEq file: \(url)")
        
        do {
            let (data, response) = try await URLSession.shared.data(from: url)
            guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
                return nil
            }

            guard let text = String(data: data, encoding: .utf8) else {
                return nil
            }

            // Use our newly added C++ parser bridge!
            if var preset = PresetManager.shared.parseAutoEqText(text) {
                // Set the preset name cleanly to include headphone name + source
                preset.name = "\(item.name) (\(item.source))"
                return preset
            }
        } catch {
            print("[AutoEqService] Download failed: \(error)")
        }

        return nil
    }
}
