#include "subscribe_stream.h"

#include "byte_buffer.h"
#include "memory_pool.h"
#include "rtcp_packet.h"
#include "rtp_header_extension.h"
#include "rtp_utils.h"
#include "spdlog/spdlog.h"
#include "utils.h"

extern thread_local MemoryPool memory_pool;

SubscribeStream::SubscribeStream(const std::string& room_id, const std::string& stream_id, std::weak_ptr<WebrtcStream::Observer> observer, std::weak_ptr<SubscribeStreamObserver> subscribe_stream_observer)
    : WebrtcStream(room_id, stream_id, observer), subscribe_stream_observer_{subscribe_stream_observer}
    , send_side_twcc_{std::make_unique<SendSideTWCC>(this)} {}

SubscribeStream::~SubscribeStream() {}

void SubscribeStream::SetSdpNegotiator(const SdpNegotiator& publish_sdp) {
  sdp_ = publish_sdp;
}

bool SubscribeStream::SetRemoteDescription(const std::string& offer) {
  return sdp_.SetSubscribeOffer(offer);
}

std::optional<std::string> SubscribeStream::CreateAnswer() {
  return sdp_.CreateSubscribeAnswer();
}

void SubscribeStream::SetLocalDescription() {
  auto media_sections = sdp_.GetMediaSections();
  for (int i = 0; i < media_sections.size(); ++i) {
    SubscribeStreamTrack::Configuration config;
    auto& media_section = media_sections[i];
    if (media_section.at("type") == "audio")
      config.audio = true;
    if (media_section.find("ssrcs") != media_section.end()) {
      auto& ssrcs = media_section.at("ssrcs");
      if (!ssrcs.empty())
        config.ssrc = ssrcs[0].at("id");
    }
    if (media_section.find("rtp") != media_section.end()) {
      auto& rtpmaps = media_section.at("rtp");
      if (!rtpmaps.empty()) {
        config.payload_type = rtpmaps[0].at("payload");
        config.clock_rate = rtpmaps[0].at("rate");
      }
      for (auto& rtpmap : rtpmaps) {
        if (rtpmap.at("codec") == "rtx") {
          config.rtx_enabled = true;
          config.rtx_payload_type = rtpmap.at("payload");
        }
      }
    }
    if (media_section.find("ssrcGroups") != media_section.end()) {
      auto& ssrc_groups = media_section.at("ssrcGroups");
      for (auto& ssrc_group : ssrc_groups) {
        if (ssrc_group.at("semantics") == "FID") {
          auto ssrcs = StringSplit(ssrc_group.at("ssrcs"), " ");
          if (ssrcs.size() == 2 && std::stol(ssrcs[0]) == config.ssrc)
            config.rtx_ssrc = std::stol(ssrcs[1]);
        }
      }
    }
    if (media_section.find("rtcpFb") != media_section.end()) {
      auto& rtcpFbs = media_section.at("rtcpFb");
      for (auto& rtcpFb : rtcpFbs) {
        if (rtcpFb.at("payload") == std::to_string(config.payload_type) && rtcpFb.at("type") == "nack" && rtcpFb.find("subtype") == rtcpFb.end())
          config.nack_enabled = true;
      }
    }
    if (media_section.find("ext") != media_section.end()) {
      const auto& extensions = media_section.at("ext");
      for (const auto& extension : extensions)
        config.extension_capability.Register(extension.at("value"), extension.at("uri"));
    }

    auto track = std::make_shared<SubscribeStreamTrack>(config, work_thread_->MessageLoop(), this);
    track->Init();
    tracks_.push_back(track);
    ssrc_track_map_.insert(std::make_pair(config.ssrc, track));

    spdlog::debug(
        "SubscribeStreamTrack ssrc = {}, payload_type = {}"
        ", rtx_enabled = {}, rtx_ssrc = {}, rtx_payload_type = {}, nack_enabled = {}",
        config.ssrc, config.payload_type, config.rtx_enabled, config.rtx_ssrc, config.rtx_payload_type, config.nack_enabled);
  }
}

void SubscribeStream::ReceivePublishStreamRtpPacket(std::shared_ptr<RtpPacket> rtp_packet) {
  auto self(shared_from_this());
  work_thread_->PostAsync([rtp_packet, self, this] {
    if (!connection_established_)
      return;
    if (!data_received_) {
      // Only video can be keyframe.
      if (!rtp_packet->IsKeyFrame()) {
        auto shared = subscribe_stream_observer_.lock();
        if (shared)
          shared->OnSubscribeStreamFrameRequested();
        return;
      } else {
        data_received_ = true;
      }
    }
    std::unique_ptr<RtpPacket> clone_packet = std::make_unique<RtpPacket>(*rtp_packet);
    if (ssrc_track_map_.find(clone_packet->Ssrc()) != ssrc_track_map_.end()) {
      clone_packet->UpdateExtensionCapability(ssrc_track_map_.at(clone_packet->Ssrc())->Config().extension_capability);
      auto rid = clone_packet->GetExtensionValue<RtpStreamIdExtension>();
      if (!rid) {
        rid = clone_packet->GetExtensionValue<RepairedRtpStreamIdExtension>();
      }

      if (rid) {
        if (current_layer_rid_ != target_layer_rid_) {
          if (*rid == target_layer_rid_ && clone_packet->IsKeyFrame()) {
            std::optional<SenderReportPacket> reference_layer_sr;
            std::optional<SenderReportPacket> target_layer_sr;
            auto shared = subscribe_stream_observer_.lock();
            if (shared) {
              shared->OnSubscribeStreamLastSrRequested(reference_layer_rid_, reference_layer_sr);
              shared->OnSubscribeStreamLastSrRequested(target_layer_rid_, target_layer_sr);
            }
            if (reference_layer_sr && target_layer_sr) {
              int64_t reference_layer_ntp_millis = NtpTime(reference_layer_sr->NtpSeconds(), reference_layer_sr->SNtpFractions()).ToMillis();
              uint32_t  reference_layer_rtp_timestamp = reference_layer_sr->RtpTimestamp();
              int64_t target_layer_ntp_millis = NtpTime(target_layer_sr->NtpSeconds(), target_layer_sr->SNtpFractions()).ToMillis();
              uint32_t  target_layer_rtp_timestamp = target_layer_sr->RtpTimestamp();
              int64_t diff_ntp_millis;
              if (target_layer_ntp_millis >= reference_layer_ntp_millis)
                diff_ntp_millis = target_layer_ntp_millis - reference_layer_ntp_millis;
              else
                diff_ntp_millis = -1 * (reference_layer_ntp_millis - target_layer_ntp_millis);
              int64_t diff_rtp_timestamp_  = diff_ntp_millis * ssrc_track_map_.at(clone_packet->Ssrc())->Config().clock_rate / kNumMillisecsPerSec;
              uint32_t reference_ntp_point_target_rtp_timestamp_ = target_layer_rtp_timestamp - diff_rtp_timestamp_;
              timestamp_offset_ = reference_ntp_point_target_rtp_timestamp_ - reference_layer_rtp_timestamp;
              current_layer_rid_ = target_layer_rid_;
              if (ssrc_track_map_.find(clone_packet->Ssrc()) != ssrc_track_map_.end())
                ssrc_track_map_.at(clone_packet->Ssrc())->SyncSequenceNumber(clone_packet->SequenceNumber());
            } else {
              auto shared = subscribe_stream_observer_.lock();
              if (shared)
                shared->OnSubscribeStreamFrameRequested(target_layer_rid_);
            }
          }
        }
        if (*rid != current_layer_rid_)
          return;
        clone_packet->SetTimestamp(clone_packet->Timestamp() - timestamp_offset_);
      }
      ssrc_track_map_.at(clone_packet->Ssrc())->SendRtpPacket(std::move(clone_packet));
    } else {
      spdlog::warn("SubscribeStream: Unrecognized RTP packet. ssrc = {}.", rtp_packet->Ssrc());
      return;
    }
  });
}

void SubscribeStream::SetSimulcastLayer(uint32_t rid) {
  auto self(shared_from_this());
  work_thread_->PostAsync([self, this, rid] {
    ChangeSimulcastLayer(rid);
  });
}

void SubscribeStream::Stop() {
  auto self(shared_from_this());
  work_thread_->PostAsync([self, this] {
    if (stoped_)
      return;
    WebrtcStream::Stop();
    for (auto track : tracks_)
      track->Deinit();
  });
}

void SubscribeStream::OnRtpPacketReceive(uint8_t* data, size_t length) {
  spdlog::warn("SubscribeStream receive rtp packet.");
}

void SubscribeStream::OnRtcpPacketReceive(uint8_t* data, size_t length) {
  RtcpCompound rtcp_compound;
  if (!rtcp_compound.Parse(data, length)) {
    spdlog::warn("Failed to parse compound rtcp.");
    return;
  }
  auto rtcp_packets = rtcp_compound.GetRtcpPackets();
  for (auto p : rtcp_packets) {
    if (p->Type() == kRtcpTypeRtpfb) {
      if (p->Format() == FeedbackRtpMessageType::kNack) {
        NackPacket* nack_packet = dynamic_cast<NackPacket*>(p);
        ssrc_track_map_[nack_packet->MediaSsrc()]->ReceiveNack(nack_packet);
      } else if (p->Format() == FeedbackRtpMessageType::kTwcc) {
        TransportFeedback* fb = dynamic_cast<TransportFeedback*>(p);
        ReceiveTransportFeedback(*fb);
      } else {
        spdlog::debug("fb format = {}", p->Format());
      }
    } else if (p->Type() == kRtcpTypeRr) {
      ReceiverReportPacket* rr = dynamic_cast<ReceiverReportPacket*>(p);
      auto report_blocks = rr->GetReportBlocks();
      for (auto block : report_blocks) {
        // TODO: When RTX is enabled, the RR packet of RTX is ignored. Fix!!.
        auto track_iter = ssrc_track_map_.find(block.MediaSsrc());
        if (track_iter != ssrc_track_map_.end())
          track_iter->second->ReceiveReceiverReport(block);
      }
    } else if (p->Type() == kRtcpTypePsfb) {
      if (p->Format() == FeedbackPsMessageType::kPli) {
        spdlog::debug("Subscribe stream receive pli.");
        auto shared = subscribe_stream_observer_.lock();
        if (shared)
          shared->OnSubscribeStreamFrameRequested(current_layer_rid_);
      } else if (p->Format() == FeedbackPsMessageType::kFir) {
        spdlog::debug("Subscribe stream receive fir.");
      }
    } else if (p->Type() == kRtcpTypeSr) {
      spdlog::warn("Subscribe stream receive SR.");
    }
  }
}

void SubscribeStream::OnSubscribeStreamTrackResendRtpPacket(std::unique_ptr<RtpPacket> rtp_packet) {
  if (!connection_established_)
    return;
  uint16_t twsn = (++transport_seq_) & 0xFFFF;
  rtp_packet->SetExtensionValue<TransportSequenceNumberExtension>(twsn);
  InjectSendSideTWCC(rtp_packet.get(), twsn);
  SendRtp(rtp_packet->Data(), rtp_packet->Size());
}

void SubscribeStream::OnSubscribeStreamTrackSendRtxPacket(std::unique_ptr<RtpPacket> rtp_packet,
                                                          uint8_t payload_type,
                                                          uint32_t ssrc,
                                                          uint16_t sequence_number) {
  // https://tools.ietf.org/html/rfc4588#section-8.3
  // The format of a retransmission packet is shown below:

  //  0                   1                   2                   3
  //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |                         RTP Header                            |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |            OSN                |                               |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
  // |                  Original RTP Packet Payload                  |
  // |                                                               |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  work_thread_->AssertInThisThread();
  if (!connection_established_)
    return;
  uint16_t twsn = (++transport_seq_) & 0xFFFF;
  rtp_packet->SetExtensionValue<TransportSequenceNumberExtension>(twsn);
  InjectSendSideTWCC(rtp_packet.get(), twsn);
  int protect_rtp_need_len = send_srtp_session_->GetProtectRtpNeedLength(rtp_packet->Size() + kRtxHeaderSize);
  UdpSocket::UdpMessage msg;
  msg.buffer = memory_pool.AllocMemory(protect_rtp_need_len);
  msg.endpoint = selected_endpoint_;
  memcpy(msg.buffer.get(), rtp_packet->Data(), rtp_packet->HeaderSize());
  memcpy(msg.buffer.get() + rtp_packet->HeaderSize() + kRtxHeaderSize, rtp_packet->Payload(), rtp_packet->PayloadSize());
  StoreUInt16BE(msg.buffer.get() + rtp_packet->HeaderSize(), rtp_packet->SequenceNumber());

  SetRtpSsrc(msg.buffer.get(), rtp_packet->Size() + kRtxHeaderSize, ssrc);
  SetPayloadType(msg.buffer.get(), rtp_packet->Size() + kRtxHeaderSize, payload_type);
  SetSequenceNumber(msg.buffer.get(), rtp_packet->Size() + kRtxHeaderSize, sequence_number);
  int length = 0;
  if (!send_srtp_session_->ProtectRtp(msg.buffer.get(), rtp_packet->Size() + kRtxHeaderSize, protect_rtp_need_len, &length)) {
    spdlog::error("Failed to encrypt RTP packat.");
    return;
  }
  msg.length = length;
  if (udp_socket_)
    udp_socket_->SendData(msg);
  else
    spdlog::error("Send data before socket is connected.");
}

void SubscribeStream::OnSubscribeStreamTrackSendRtcpPacket(RtcpPacket& rtcp_packet) {
  SendRtcp(rtcp_packet);
}

void SubscribeStream::OnSubscribeStreamTrackSendRtpPacket(RtpPacket* packet) {
  if (!packet)
    return;
  uint16_t twsn = (++transport_seq_) & 0xFFFF;
  packet->SetExtensionValue<TransportSequenceNumberExtension>(twsn);
  InjectSendSideTWCC(packet, twsn);
  SendRtp(packet->Data(), packet->Size());
}

void SubscribeStream::ReceiveTransportFeedback(const TransportFeedback& feedback) {
  send_side_twcc_->ReceiveTransportFeedback(feedback);
}

void SubscribeStream::InjectSendSideTWCC(RtpPacket* packet, uint16_t twsn) {
  if (!packet)
    return;
  PacketStatus packet_status;
  packet_status.sent_time_millis = TimeMillis();
  packet_status.transport_wide_sequence_number = twsn;
  packet_status.size = packet->Size();
  send_side_twcc_->SendPacket(packet_status);
}

void SubscribeStream::OnBandwidthOveruse() {
}

void SubscribeStream::OnBandwidthRecover() {
}

bool SubscribeStream::ChangeSimulcastLayer(uint32_t rid) {
  if (rid > kMaxSimulcastLayers || rid == kInvalidLayerIndex)
    return false;
  if (rid == current_layer_rid_)
    return true;
  uint32_t selected_rid = 0;
  auto shared = subscribe_stream_observer_.lock();
  if (shared)
    shared->OnSubscribeStreamQueryRID(rid, selected_rid);
  if (selected_rid == 0)
    return false;
  target_layer_rid_ = selected_rid;
  if (shared)
    shared->OnSubscribeStreamFrameRequested(target_layer_rid_);
  return true;
}