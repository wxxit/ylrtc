#include "send_side_twcc.h"

#include "utils.h"
#include "spdlog/spdlog.h"

SendSideTWCC::SendSideTWCC(SendSideTWCCObserver* observer) : inter_arrival_delta_{std::make_unique<InterArrivalDelta>()}
  , trendline_estimator_{std::make_unique<TrendlineEstimator>()}
  , previous_bandwidth_usage_{BandwidthUsage::kBwNormal}
  , observer_{observer} {
}

void SendSideTWCC::SendPacket(const PacketStatus& packet) {
  if (!packet.IsSent())
    return;
  while (!history_.empty() && packet.sent_time_millis - history_.begin()->second.sent_time_millis > kSendTimeWindowMillis)
    history_.erase(history_.begin());
  history_.insert(std::make_pair(packet.transport_wide_sequence_number, packet));
}

void SendSideTWCC::ReceiveTransportFeedback(const TransportFeedback& feedback) {
  std::vector<PacketStatus> packet_status_vector;
  if (feedback.GetPacketStatusCount() == 0)
    return;
  int64_t base_time_micros = feedback.GetBaseTimeUs();
  int64_t time_offset_micros = 0;
  for (const auto& packet : feedback.GetAllPackets()) {
    int64_t seq_num = seq_num_unwrapper_.Unwrap(packet.sequence_number());
    auto it = history_.find(seq_num);
    if (it == history_.end())
      continue;
    if (!it->second.IsSent())
      continue;
    PacketStatus packet_status = it->second;
    if (packet.received()) {
      time_offset_micros += packet.delta_us();
      packet_status.receive_time_millis = (base_time_micros + time_offset_micros) / 1000;
      history_.erase(it);
      packet_status_vector.push_back(packet_status);
    }
  }
  previous_bandwidth_usage_ = trendline_estimator_->State();
  if (!packet_status_vector.empty()) {
    int64_t now_millis = TimeMillis();
    int64_t send_time_delta_millis = -1;
    int64_t arrival_time_delta_millis = -1;
    int packet_size_delta = -1;
    for (auto& packet_status : packet_status_vector) {
      bool calculated_deltas = inter_arrival_delta_->ComputeDeltas(packet_status.sent_time_millis, packet_status.receive_time_millis
        , now_millis, packet_status.size, &send_time_delta_millis, &arrival_time_delta_millis, &packet_size_delta);
      trendline_estimator_->Update(arrival_time_delta_millis, send_time_delta_millis, packet_status.sent_time_millis
        , packet_status.receive_time_millis, packet_status.size, calculated_deltas);
      auto current_bandwidth_usage_ = trendline_estimator_->State();
      if (previous_bandwidth_usage_ == BandwidthUsage::kBwNormal || previous_bandwidth_usage_ == BandwidthUsage::kBwUnderusing) {
        if (current_bandwidth_usage_ == BandwidthUsage::kBwOverusing) {
          if (observer_)
            observer_->OnBandwidthOveruse();
        }
      } else {
        if (current_bandwidth_usage_ == BandwidthUsage::kBwNormal || current_bandwidth_usage_ == BandwidthUsage::kBwUnderusing) {
          if (observer_)
            observer_->OnBandwidthRecover();
        }
      }
    }
  }
}

BandwidthUsage SendSideTWCC::State() {
  return trendline_estimator_->State();
}