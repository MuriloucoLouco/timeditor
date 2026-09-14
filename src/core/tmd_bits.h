#pragma once
#include <cstdint>

// Primitive mode/flag bit values shared by tmd_parser.cpp (decode) and
// tmd_writer.cpp (encode) - see TMD.md / tmd.h. Kept in one place so the
// two can never drift apart from each other.
namespace tmd::bits {

// Mode bits.
constexpr uint8_t kModeNoLight = 0x01;
constexpr uint8_t kModeSemiTransparent = 0x02;
constexpr uint8_t kModeTextured = 0x04;
constexpr uint8_t kModeQuad = 0x08;
constexpr uint8_t kModeGouraud = 0x10;
constexpr uint8_t kModeIsPolygon = 0x20; // clear => line primitive, not decoded/encoded here

// Flag bits.
constexpr uint8_t kFlagDoubleSided = 0x02;
constexpr uint8_t kFlagGradation = 0x04;

} // namespace tmd::bits
