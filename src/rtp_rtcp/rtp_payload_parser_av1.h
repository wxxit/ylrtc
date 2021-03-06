#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "rtp_payload_parser.h"

class RtpPayloadParserAv1 {
 public:
  static std::optional<PayloadInfo> Parse(uint8_t* data, size_t size);
};