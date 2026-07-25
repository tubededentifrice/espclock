import Foundation

enum TimePacketEncodingError: LocalizedError {
    case epochOutOfRange
    case offsetOutOfRange

    var errorDescription: String? {
        switch self {
        case .epochOutOfRange:
            return "The phone time is outside the clock's supported 2024–2099 range."
        case .offsetOutOfRange:
            return "The phone time-zone offset is outside UTC−14:00…UTC+14:00."
        }
    }
}

enum TimePacketEncoder {
    static let minimumEpoch: Int64 = 1_704_067_200
    static let maximumEpoch: Int64 = 4_102_444_799

    static func encode(epoch: Int64, utcOffsetMinutes: Int16) throws -> Data {
        guard (minimumEpoch...maximumEpoch).contains(epoch) else {
            throw TimePacketEncodingError.epochOutOfRange
        }
        guard (-840...840).contains(Int(utcOffsetMinutes)) else {
            throw TimePacketEncodingError.offsetOutOfRange
        }

        var bytes = [UInt8](repeating: 0, count: 12)
        bytes[0] = 1

        let rawEpoch = UInt64(bitPattern: epoch)
        for index in 0..<8 {
            bytes[index + 1] = UInt8(truncatingIfNeeded: rawEpoch >> (index * 8))
        }

        let rawOffset = UInt16(bitPattern: utcOffsetMinutes)
        bytes[9] = UInt8(truncatingIfNeeded: rawOffset)
        bytes[10] = UInt8(truncatingIfNeeded: rawOffset >> 8)
        bytes[11] = 0
        return Data(bytes)
    }

    static func encode(date: Date, timeZone: TimeZone = .current) throws -> Data {
        let epoch = Int64(date.timeIntervalSince1970.rounded(.down))
        let offsetSeconds = timeZone.secondsFromGMT(for: date)
        guard offsetSeconds % 60 == 0,
              let offset = Int16(exactly: offsetSeconds / 60) else {
            throw TimePacketEncodingError.offsetOutOfRange
        }
        return try encode(epoch: epoch, utcOffsetMinutes: offset)
    }
}
