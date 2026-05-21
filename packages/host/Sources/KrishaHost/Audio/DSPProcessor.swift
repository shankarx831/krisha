import Foundation
import CKrishaDSP

class DSPProcessor {
    private var engine: OpaquePointer?

    init(sampleRate: UInt32) {
        engine = krisha_dsp_create(sampleRate)
    }

    deinit {
        if let engine = engine {
            krisha_dsp_destroy(engine)
        }
    }

    func applyPreset(_ preset: krisha_preset_t) -> Bool {
        guard let engine = engine else { return false }

        var mutablePreset = preset
        if krisha_dsp_apply_preset(engine, &mutablePreset) == KRISHA_OK {
            return true
        }
        return false
    }

    func processInterleaved(
        _ input: [Float],
        output: inout [Float],
        frameCount: UInt32
    ) {
        guard let engine = engine else { return }
        krisha_dsp_process_interleaved(engine, input, &output, frameCount)
    }

    func setSampleRate(_ sampleRate: UInt32) -> Bool {
        guard let engine = engine else { return false }
        return krisha_dsp_set_sample_rate(engine, sampleRate) == KRISHA_OK
    }

    func createFlatPreset() -> krisha_preset_t {
        var preset = krisha_preset_t()
        krisha_dsp_preset_init_flat(&preset)

        return preset
    }
}
