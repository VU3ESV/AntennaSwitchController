import Foundation

/// Supported amateur bands. The index order is identical to the firmware's
/// `Band` enum and the `/config` `bands[]` array — DO NOT reorder, or saved
/// relay assignments would remap to the wrong band.
enum Band: Int, CaseIterable, Identifiable {
    case b160, b80, b60, b40, b30, b20, b17, b15, b12, b10, b6

    var id: Int { rawValue }

    var label: String {
        ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m"][rawValue]
    }
}

/// Number of relays on the controller board (fixed at 8).
let kRelayCount = 8

/// Firmware GPIO map (relay index 0..7 → ESP8266 GPIO), shown in the relay
/// picker so the operator can match physical wiring.
let kRelayGPIO = [16, 14, 12, 13, 15, 0, 4, 5]
