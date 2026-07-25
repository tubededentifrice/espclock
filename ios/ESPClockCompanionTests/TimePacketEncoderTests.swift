import Foundation
import XCTest
@testable import ESPClockCompanion

final class TimePacketEncoderTests: XCTestCase {
    func testEncodesFirmwareBinaryProtocolLittleEndian() throws {
        let epoch: Int64 = 1_735_689_600
        let packet = try TimePacketEncoder.encode(
            epoch: epoch,
            utcOffsetMinutes: 330
        )

        XCTAssertEqual(packet.count, 12)
        XCTAssertEqual(packet[0], 1)
        XCTAssertEqual(Array(packet[1...8]), epoch.littleEndianBytes)
        XCTAssertEqual(Array(packet[9...10]), Int16(330).littleEndianBytes)
        XCTAssertEqual(packet[11], 0)
    }

    func testEncodesNegativeOffsetAsSignedLittleEndian() throws {
        let packet = try TimePacketEncoder.encode(
            epoch: 1_735_689_600,
            utcOffsetMinutes: -210
        )

        XCTAssertEqual(Array(packet[9...10]), Int16(-210).littleEndianBytes)
    }

    func testRejectsEpochBeforeFirmwareRange() {
        XCTAssertThrowsError(
            try TimePacketEncoder.encode(
                epoch: 1_704_067_199,
                utcOffsetMinutes: 0
            )
        )
    }

    func testRejectsOffsetOutsideFourteenHours() {
        XCTAssertThrowsError(
            try TimePacketEncoder.encode(
                epoch: 1_735_689_600,
                utcOffsetMinutes: 841
            )
        )
        XCTAssertThrowsError(
            try TimePacketEncoder.encode(
                epoch: 1_735_689_600,
                utcOffsetMinutes: -841
            )
        )
    }
}

private extension FixedWidthInteger {
    var littleEndianBytes: [UInt8] {
        withUnsafeBytes(of: littleEndian) { Array($0) }
    }
}
