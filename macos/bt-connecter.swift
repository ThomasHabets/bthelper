import Foundation
import IOBluetooth

let programName = CommandLine.arguments.first.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "bt-connecter"

func writeError(_ text: String) {
    FileHandle.standardError.write(Data(text.utf8))
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
        guard part.count == 2, part.allSatisfy({ $0.isHexDigit }) else { return nil }
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
        source.setEventHandler { [weak self] in
            guard let self = self else { return }
            var buffer = [UInt8](repeating: 0, count: chunk)
            let count = Darwin.read(STDIN_FILENO, &buffer, chunk)
            if count > 0 {
                let payload = Data(buffer[0..<count])
                DispatchQueue.main.async { self.send(payload) }
            } else {
                DispatchQueue.main.async { self.finish(0) }
            }
        }
        source.resume()
        stdinSource = source
    }

    private func send(_ payload: Data) {
        guard let live = channel else { return }
        var mutable = payload
        let status = mutable.withUnsafeMutableBytes { raw in
            live.writeSync(raw.baseAddress, length: UInt16(payload.count))
        }
        if status != kIOReturnSuccess { finish(1) }
    }

    private func finish(_ code: Int32) -> Never {
        channel?.close()
        exit(code)
    }

    @objc func rfcommChannelData(_ rfcommChannel: IOBluetoothRFCOMMChannel!, data dataPointer: UnsafeMutableRawPointer!, length dataLength: Int) {
        guard dataLength > 0 else { return }
        let base = dataPointer.assumingMemoryBound(to: UInt8.self)
        var offset = 0
        while offset < dataLength {
            let written = Darwin.write(STDOUT_FILENO, base + offset, dataLength - offset)
            if written <= 0 { finish(1) }
            offset += written
        }
    }

    @objc func rfcommChannelClosed(_ rfcommChannel: IOBluetoothRFCOMMChannel!) {
        finish(0)
    }
}

var positional: [String] = []
for argument in CommandLine.arguments.dropFirst() {
    if argument == "-h" {
        usage(EXIT_SUCCESS)
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

Bridge().run(address: address, channelID: channelID)
