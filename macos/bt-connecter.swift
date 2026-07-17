import Foundation
import IOBluetooth

let programName = CommandLine.arguments.first.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "bt-connecter"

func writeError(_ text: String) {
    let bytes = Array(text.utf8)
    _ = bytes.withUnsafeBytes { Darwin.write(STDERR_FILENO, $0.baseAddress, $0.count) }
}

func usage(_ code: Int32) -> Never {
    writeError(
        "Usage: \(programName) [-h] <bluetooth destination> <channel>\n" +
        "  <bluetooth destination>  device address, e.g. AA:BB:CC:DD:EE:FF\n" +
        "  <channel>                RFCOMM channel, 1-30\n")
    exit(code)
}

func fail(_ message: String) -> Never {
    writeError("\(programName): \(message)\n")
    exit(EXIT_FAILURE)
}

func normalizeAddress(_ raw: String) -> String? {
    let parts = raw.split(whereSeparator: { $0 == ":" || $0 == "-" }).map(String.init)
    guard parts.count == 6 else { return nil }
    for part in parts {
        guard part.count == 2, part.allSatisfy({ $0.isHexDigit && $0.isASCII }) else { return nil }
    }
    return parts.map { $0.uppercased() }.joined(separator: "-")
}

func parseChannel(_ raw: String) -> Int? {
    guard let value = Int(raw), (1...30).contains(value) else { return nil }
    return value
}

final class Bridge: NSObject, IOBluetoothRFCOMMChannelDelegate {
    private var channel: IOBluetoothRFCOMMChannel?
    private var stdinSource: DispatchSourceRead?
    private var exitCode: Int32?
    private let stdinQueue = DispatchQueue(label: "bt-connecter.stdin")

    func run(address: String, channelID: Int) -> Never {
        guard let device = IOBluetoothDevice(addressString: address) else {
            fail("cannot resolve device \(address)")
        }
        var opened: IOBluetoothRFCOMMChannel?
        let status = device.openRFCOMMChannelSync(&opened, withChannelID: BluetoothRFCOMMChannelID(channelID), delegate: self)
        guard status == kIOReturnSuccess, let live = opened else {
            fail("cannot open RFCOMM channel \(channelID) on \(address) (status 0x\(String(UInt32(bitPattern: status), radix: 16)))")
        }
        channel = live
        let mtu = Int(live.getMTU())
        startStdin(chunk: mtu > 0 ? mtu : 4096)
        CFRunLoopRun()
        exit(EXIT_SUCCESS)
    }

    private func startStdin(chunk: Int) {
        let source = DispatchSource.makeReadSource(fileDescriptor: STDIN_FILENO, queue: stdinQueue)
        var buffer = [UInt8](repeating: 0, count: chunk)
        source.setEventHandler { [weak self] in
            guard let self = self else { return }
            var count = 0
            repeat {
                count = Darwin.read(STDIN_FILENO, &buffer, chunk)
            } while count < 0 && errno == EINTR
            if count > 0 {
                let payload = Data(buffer[0..<count])
                source.suspend()
                DispatchQueue.main.async {
                    self.send(payload)
                    source.resume()
                }
            } else {
                let code: Int32 = count == 0 ? 0 : 1
                source.cancel()
                DispatchQueue.main.async { self.finish(code) }
            }
        }
        stdinSource = source
        source.resume()
    }

    private func send(_ payload: Data) {
        guard let live = channel else { return }
        let status = payload.withUnsafeBytes { raw in
            live.writeSync(UnsafeMutableRawPointer(mutating: raw.baseAddress), length: UInt16(raw.count))
        }
        if status != kIOReturnSuccess { finish(1) }
    }

    private func finish(_ code: Int32) -> Never {
        if let started = exitCode { exit(started) }
        exitCode = code
        channel?.close()
        exit(code)
    }

    @objc func rfcommChannelData(_ rfcommChannel: IOBluetoothRFCOMMChannel!, data dataPointer: UnsafeMutableRawPointer!, length dataLength: Int) {
        guard dataLength > 0 else { return }
        let base = dataPointer.assumingMemoryBound(to: UInt8.self)
        var offset = 0
        while offset < dataLength {
            let written = Darwin.write(STDOUT_FILENO, base + offset, dataLength - offset)
            if written <= 0 {
                if written < 0 && errno == EINTR { continue }
                finish(1)
            }
            offset += written
        }
    }

    @objc func rfcommChannelClosed(_ rfcommChannel: IOBluetoothRFCOMMChannel!) {
        finish(0)
    }
}

signal(SIGPIPE, SIG_IGN)

var positional: [String] = []
for argument in CommandLine.arguments.dropFirst() {
    if argument == "-h" {
        usage(EXIT_SUCCESS)
    } else if argument == "-t" {
        writeError("\(programName): terminal mode (-t) is not implemented\n")
        usage(EXIT_FAILURE)
    } else if argument.hasPrefix("-") && argument != "-" {
        writeError("\(programName): unknown option: \(argument)\n")
        usage(EXIT_FAILURE)
    } else {
        positional.append(argument)
    }
}

guard positional.count == 2 else {
    writeError("\(programName): need exactly two arguments: destination and channel\n")
    usage(EXIT_FAILURE)
}
guard let address = normalizeAddress(positional[0]) else {
    fail("invalid bluetooth address: \(positional[0])")
}
guard let channelID = parseChannel(positional[1]) else {
    fail("invalid RFCOMM channel: \(positional[1])")
}

let bridge = Bridge()
bridge.run(address: address, channelID: channelID)
