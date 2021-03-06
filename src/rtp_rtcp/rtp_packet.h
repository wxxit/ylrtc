#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include "rtp_header_extension.h"
#include "rtp_payload_parser.h"

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |  header eXtension profile id  |       length in 32bits        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                           Payload                             |
// |             ....              :  padding...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

class RtpPacket {
 public:
  struct ExtensionInfo {
    explicit ExtensionInfo(uint8_t id) : ExtensionInfo(id, 0, 0) {}
    ExtensionInfo(uint8_t id, uint8_t length, uint16_t offset) : id(id), length(length), offset(offset) {}
    uint8_t id;
    uint8_t length;
    uint16_t offset;
    uint8_t header_length;
  };

  RtpPacket();
  RtpPacket(const RtpPacket& other);
  RtpPacket& operator=(const RtpPacket&) = delete;
  ~RtpPacket();
  bool Create(uint8_t* buffer, size_t size);

  void SetMarker(bool marker_bit);
  void SetPayloadType(uint8_t payload_type);
  void SetSequenceNumber(uint16_t seq_no);
  void SetTimestamp(uint32_t timestamp);
  void SetSsrc(uint32_t ssrc);

  bool Marker() const;
  uint8_t PayloadType() const;
  uint16_t SequenceNumber() const;
  uint32_t Timestamp() const;
  uint32_t Ssrc() const;
  size_t Size() const;
  uint8_t* Data() const;
  size_t PayloadSize() const;
  uint8_t* Payload() const;
  size_t HeaderSize() const;
  bool IsKeyFrame() const;
  void SetExtensionCapability(const RtpHeaderExtensionCapability& extension_capability);
  template <typename Extension>
  std::optional<typename Extension::value_type> GetExtensionValue() {
    auto id = extension_capability_.GetTypeId(Extension::kType);
    if (!id)
      return std::nullopt;
    auto result = std::find_if(extension_entries_.cbegin(), extension_entries_.cend(), [id, this](auto& extension) { return extension.id == *id; });
    if (result == extension_entries_.end())
      return std::nullopt;
    return Extension::Parse(data_.get() + result->offset, result->length);
  }

  template <typename Extension>
  void SetExtensionValue(typename Extension::value_type value) {
    auto id = extension_capability_.GetTypeId(Extension::kType);
    if (!id)
      return;
    auto result = std::find_if(extension_entries_.cbegin(), extension_entries_.cend(), [id, this](auto& extension) { return extension.id == *id; });
    if (result == extension_entries_.cend())
      return;
    Extension::Serialize(data_.get() + result->offset, result->length, value);
  }

  void UpdateExtensionCapability(RtpHeaderExtensionCapability new_capability);
  bool RtxRepair(uint16_t sequence_number, uint8_t payload_type, uint32_t ssrc);
  bool ParsePayload(std::string_view codec);

 private:
  ExtensionInfo& FindOrCreateExtensionInfo(int id);
  bool Parse(const uint8_t* buffer, size_t size);

  bool marker_;
  uint8_t payload_type_;
  uint8_t padding_size_;
  uint16_t sequence_number_;
  uint32_t timestamp_;
  uint32_t ssrc_;
  size_t payload_offset_;  // Match header size with csrcs and extensions.
  size_t payload_size_;
  std::vector<ExtensionInfo> extension_entries_;
  size_t extensions_size_ = 0;  // Unaligned.
  std::unique_ptr<uint8_t[]> data_;
  PayloadInfo payload_info_;
  RtpHeaderExtensionCapability extension_capability_;
};