#include "subscribe_stream_track.h"

#include "spdlog/spdlog.h"
#include "utils.h"
#include "sequence_number_util.h"

SubscribeStreamTrack::SubscribeStreamTrack(const Configuration& configuration, boost::asio::io_context& io_context, Observer* observer)
    : configuration_{configuration}, io_context_{io_context}, observer_{observer} {
  report_interval_ = configuration_.audio ? kDefaultAudioReportIntervalMillis : kDefaultVideoReportIntervalMillis;
}

void SubscribeStreamTrack::Init() {
  rtcp_timer_.reset(new Timer(io_context_, shared_from_this()));
  rtcp_timer_->AsyncWait(report_interval_);
}

void SubscribeStreamTrack::Deinit() {
  rtcp_timer_->Stop();
}

SubscribeStreamTrack::Configuration& SubscribeStreamTrack::Config() {
  return configuration_;
}

void SubscribeStreamTrack::SendRtpPacket(std::unique_ptr<RtpPacket> rtp_packet) {
  if (!rtp_packet)
    return;
  packets_sent_++;
  media_bytes_sent_ += rtp_packet->Size();
  last_rtp_timestamp_ = rtp_packet->Timestamp();
  last_send_timestamp_ = TimeMillis();
  rate_statistics_.AddData(rtp_packet->Size(), last_send_timestamp_);
  uint16_t seq = base_rtp_seq_ + rtp_packet->SequenceNumber();
  if (first_packet_) {
    max_rtp_seq_ = seq;
    first_packet_ = false;
  } else {
    if (SeqNumGT(seq, max_rtp_seq_))
      max_rtp_seq_ = seq;
  }
  rtp_packet->SetSequenceNumber(seq);
  if (observer_)
    observer_->OnSubscribeStreamTrackSendRtpPacket(rtp_packet.get());
  if (configuration_.nack_enabled)
    send_packet_recorder_.Record(std::move(rtp_packet));
}

void SubscribeStreamTrack::ReceiveNack(NackPacket* nack_packet) {
  if (!configuration_.nack_enabled) {
    spdlog::warn("NACK packet are received in NACK disabled stream track.");
    return;
  }

  if (!nack_packet)
    return;
  auto& lost_packets = nack_packet->GetLostPacketSequenceNumbers();
  for (auto& seq_num : lost_packets) {
    auto packet = send_packet_recorder_.Query(seq_num);
    if (!packet)
      continue;
    if (!configuration_.rtx_enabled)
      observer_->OnSubscribeStreamTrackResendRtpPacket(std::move(packet));
    else
      observer_->OnSubscribeStreamTrackSendRtxPacket(std::move(packet), configuration_.rtx_payload_type, configuration_.rtx_ssrc,
                                                     rtx_sequence_number_++);
  }
}

void SubscribeStreamTrack::ReceiveReceiverReport(const ReportBlock& report_block) {
  uint64_t now = TimeMillis();
  NtpTime ntp = NtpTime::CreateFromMillis(now);
  uint32_t compact_ntp = ntp.ToCompactNtp();
  if (report_block.LastSr() != 0) {
    uint32_t rtp_compact_ntp = compact_ntp - report_block.DelayLastSr() - report_block.LastSr();
    rtt_millis_ = NtpTime::CreateFromCompactNtp(rtp_compact_ntp).ToMillis();
    send_packet_recorder_.SetRtt(rtt_millis_);
  }
}

void SubscribeStreamTrack::SyncSequenceNumber(uint16_t seq) {
  base_rtp_seq_ = max_rtp_seq_ - seq + 1;
}

std::optional<SenderReportPacket> SubscribeStreamTrack::BuildSr() {
  if (last_send_timestamp_ == -1)
    return std::nullopt;
  int now_millis = TimeMillis();
  auto ntp = NtpTime::CreateFromMillis(now_millis);
  SenderReportPacket sr;
  auto diff_in_millis = now_millis - last_send_timestamp_;
  auto diff_in_clockrate = diff_in_millis * configuration_.clock_rate / 1000;
  sr.SetSenderSsrc(configuration_.ssrc);
  sr.SetNtpSeconds(ntp.Seconds());
  sr.SetNtpFractions(ntp.Fractions());
  sr.SetRtpTimestamp(last_rtp_timestamp_ + diff_in_clockrate);
  sr.SetSendPacketCount(packets_sent_);
  sr.SendOctets(media_bytes_sent_);
  return sr;
}

void SubscribeStreamTrack::OnTimerTimeout() {
  auto sr_packet = BuildSr();
  if (sr_packet && observer_)
    observer_->OnSubscribeStreamTrackSendRtcpPacket(*sr_packet);

  // generate next time to send an RTCP report
  int64_t min_interval = report_interval_;

  if (!configuration_.audio) {
    // Calculate bandwidth for video; 360 / send bandwidth in kbit/s.
    auto rate = rate_statistics_.Rate(TimeMillis());
    if (rate) {
      int64_t send_bitrate_kbit = *rate / 1000;
      if (send_bitrate_kbit != 0) {
        const int64_t millisecs_per_sec = 1000;
        min_interval = std::min(360 * millisecs_per_sec / send_bitrate_kbit, report_interval_);
      }
    }
  }

  // The interval between RTCP packets is varied randomly over the
  // range [1/2,3/2] times the calculated interval.
  int64_t time_to_next = random_.RandomNumber(min_interval * 1 / 2, min_interval * 3 / 2);
  rtcp_timer_->AsyncWait(time_to_next);
}