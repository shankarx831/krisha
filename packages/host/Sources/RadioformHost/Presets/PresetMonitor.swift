import Foundation

class PresetMonitor {
    private let loader: PresetLoader
    private let processor: DSPProcessor
    private let queue = DispatchQueue(label: "com.radioform.preset-monitor")
    private var dirSource: DispatchSourceFileSystemObject?
    private var dirDescriptor: Int32 = -1
    private var lastModification: Date?

    init(loader: PresetLoader, processor: DSPProcessor) {
        self.loader = loader
        self.processor = processor
    }

    func startMonitoring() {
        guard dirSource == nil else { return }

        let presetFilePath = RadioformConfig.presetFilePath
        let presetDir = URL(fileURLWithPath: presetFilePath).deletingLastPathComponent().path

        dirDescriptor = open(presetDir, O_EVTONLY)
        guard dirDescriptor >= 0 else {
            print("Failed to open preset directory for monitoring: \(presetDir)")
            return
        }

        let source = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: dirDescriptor,
            eventMask: .write,
            queue: queue
        )

        source.setEventHandler { [weak self] in
            self?.checkForChanges()
        }

        source.setCancelHandler { [weak self] in
            if let fd = self?.dirDescriptor, fd >= 0 {
                close(fd)
                self?.dirDescriptor = -1
            }
        }

        source.resume()
        self.dirSource = source

        // Check once initially
        queue.async { [weak self] in
            self?.checkForChanges()
        }
    }

    func stopMonitoring() {
        dirSource?.cancel()
        dirSource = nil
    }

    private func checkForChanges() {
        if let attributes = try? FileManager.default.attributesOfItem(
            atPath: RadioformConfig.presetFilePath
        ),
           let modDate = attributes[.modificationDate] as? Date {

            if lastModification == nil || modDate > lastModification! {
                lastModification = modDate
                loadAndApplyPreset()
            }
        }
    }

    private func loadAndApplyPreset() {
        do {
            let preset = try loader.load(from: RadioformConfig.presetFilePath)

            if processor.applyPreset(preset) {
                let name = String(
                    cString: withUnsafeBytes(of: preset.name) {
                        $0.baseAddress!.assumingMemoryBound(to: CChar.self)
                    }
                )
                print("Applied preset: \(name)")
                print("    preamp_db=\(preset.preamp_db) preamp_left_db=\(preset.preamp_left_db) preamp_right_db=\(preset.preamp_right_db)")
                print("    limiter_enabled=\(preset.limiter_enabled) limiter_threshold_db=\(preset.limiter_threshold_db)")
                print("    bands=\(preset.num_bands)")
            } else {
                print("Failed to apply preset")
            }
        } catch {
            print("Failed to load preset: \(error)")
        }
    }
}
