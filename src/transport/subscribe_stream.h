#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>

#include "rtp_packet.h"
#include "rtcp_packet.h"
#include "subscribe_stream_track.h"
#include "webrtc_stream.h"
#include "transport_feedback.h"
#include "send_side_twcc.h"

class SubscribeStreamObserver {
 public:
  virtual void OnSubscribeStreamFrameRequested(uint32_t rid = 0) = 0;
  virtual void OnSubscribeStreamLastSrRequested(uint32_t rid, std::optional<SenderReportPacket>& sr) = 0;
  virtual void OnSubscribeStreamQueryRID(uint32_t want, uint32_t& result) = 0;
};

class SubscribeStream : public WebrtcStream, public SubscribeStreamTrack::Observer,  public SendSideTWCCObserver {
 public:
  SubscribeStream(const std::string& room_id, const std::string& stream_id, std::weak_ptr<WebrtcStream::Observer> observer, std::weak_ptr<SubscribeStreamObserver> subscribe_stream_observer);
  ~SubscribeStream();
  void SetSdpNegotiator(const SdpNegotiator& publish_sdp);
  bool SetRemoteDescription(const std::string& offer) override;
  std::optional<std::string> CreateAnswer() override;
  void SetLocalDescription() override;
  void ReceivePublishStreamRtpPacket(std::shared_ptr<RtpPacket> rtp_packet);
  void SetSimulcastLayer(uint32_t rid);
  void Stop() override;

 private:
  static constexpr int64_t kNumMillisecsPerSec = 1000;
  static constexpr uint32_t kMaxSimulcastLayers = 3;
  static constexpr uint32_t kInvalidLayerIndex = 0;
  void OnRtpPacketReceive(uint8_t* data, size_t length) override;
  void OnRtcpPacketReceive(uint8_t* data, size_t length) override;
  void OnSubscribeStreamTrackResendRtpPacket(std::unique_ptr<RtpPacket> rtp_packet) override;
  void OnSubscribeStreamTrackSendRtxPacket(std::unique_ptr<RtpPacket> rtp_packet,
                                           uint8_t payload_type,
                                           uint32_t ssrc,
                                           uint16_t sequence_number) override;
  void OnSubscribeStreamTrackSendRtcpPacket(RtcpPacket& rtcp_packet) override;
  void OnSubscribeStreamTrackSendRtpPacket(RtpPacket* packet) override;
  void ReceiveTransportFeedback(const TransportFeedback& feedback);
  void InjectSendSideTWCC(RtpPacket* packet, uint16_t twsn);
  void OnBandwidthOveruse() override;
  void OnBandwidthRecover() override;
  bool ChangeSimulcastLayer(uint32_t rid);

  std::vector<std::shared_ptr<SubscribeStreamTrack>> tracks_;
  std::unordered_map<uint32_t, std::shared_ptr<SubscribeStreamTrack>> ssrc_track_map_;
  std::weak_ptr<SubscribeStreamObserver> subscribe_stream_observer_;
  uint64_t transport_seq_{0};
  bool data_received_{false};
  uint32_t current_layer_rid_{1};
  uint32_t target_layer_rid_{1};
  uint32_t reference_layer_rid_{1};
  uint32_t timestamp_offset_{0};
  std::unique_ptr<SendSideTWCC> send_side_twcc_;
};