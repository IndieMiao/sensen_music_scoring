import AVFoundation

final class AudioRecorder: ObservableObject {
    struct Capture {
        let pcm: [Float]
        let sampleRate: Int
    }

    private let engine = AVAudioEngine()
    private let lock = NSLock()
    private var chunks: [[Float]] = []
    private var sampleRate: Int = 44100
    private var running = false

    func start(onError: @escaping (String) -> Void) async -> Bool {
        let granted = await withCheckedContinuation { cont in
            AVAudioSession.sharedInstance().requestRecordPermission { cont.resume(returning: $0) }
        }
        if !granted {
            onError("Microphone permission denied.")
            return false
        }

        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.record, mode: .measurement, options: [])
            try session.setActive(true, options: [])
        } catch {
            onError("Audio session failed: \(error.localizedDescription)")
            return false
        }

        lock.lock(); chunks.removeAll(); lock.unlock()

        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        sampleRate = Int(format.sampleRate)

        input.installTap(onBus: 0, bufferSize: 4096, format: format) { [weak self] buffer, _ in
            guard let self, let channel = buffer.floatChannelData?[0] else { return }
            let count = Int(buffer.frameLength)
            let chunk = Array(UnsafeBufferPointer(start: channel, count: count))
            self.lock.lock()
            self.chunks.append(chunk)
            self.lock.unlock()
        }

        do {
            try engine.start()
        } catch {
            input.removeTap(onBus: 0)
            onError("Engine start failed: \(error.localizedDescription)")
            return false
        }

        running = true
        return true
    }

    func stop() -> Capture {
        guard running else {
            return Capture(pcm: [], sampleRate: sampleRate)
        }
        running = false

        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)

        lock.lock()
        let allChunks = chunks
        chunks.removeAll()
        lock.unlock()

        var total = 0
        for c in allChunks { total += c.count }
        var flat = [Float]()
        flat.reserveCapacity(total)
        for c in allChunks { flat.append(contentsOf: c) }
        return Capture(pcm: flat, sampleRate: sampleRate)
    }
}
