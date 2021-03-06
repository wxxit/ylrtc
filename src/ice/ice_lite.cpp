#include "ice_lite.h"

#include "spdlog/spdlog.h"
#include "stun_message.h"
#include "stun_common.h"
#include "utils.h"

IceLite::IceLite(const std::string& room_id, const std::string& stream_id, const std::string& remote_ufrag, Observer* observer)
    : local_ufrag_{MakeUfrag(room_id, stream_id)},
      local_password_{random_.RandomString(kDefaultPasswordLength)},
      remote_ufrag_{remote_ufrag},
      observer_{observer} {}

void IceLite::ProcessStunMessage(uint8_t* data, size_t len, udp::endpoint* remote_ep) {
  StunMessage msg(local_ufrag_, local_password_, remote_ufrag_);
  if (!msg.Parse(data, len)) {
    spdlog::error("Stun message parsing error from [ip: {}, port {}]", remote_ep->address().to_string(), remote_ep->port());
    DCHECK(observer_);
    observer_->OnIceConnectionError();
    return;
  }

  if (msg.HasUseCandidate()) {
    old_selected_candidate_ = selected_candidate_;
    selected_candidate_ = *remote_ep;
    if (old_selected_candidate_ != selected_candidate_) {
      spdlog::debug("Ice connect completed. remote: [ip: {}, port {}]", remote_ep->address().to_string(), remote_ep->port());
      DCHECK(observer_);
      observer_->OnIceConnectionCompleted();
    }
  }

  msg.SetXorMappedAddress(remote_ep);
  if (!msg.CreateResponse())
    return;
  DCHECK(observer_);
  observer_->OnStunMessageSend(msg.Data(), msg.Size(), remote_ep);
}

const std::string& IceLite::LocalUfrag() const {
  return local_ufrag_;
}

const std::string& IceLite::LocalPassword() const {
  return local_password_;
}

const udp::endpoint* IceLite::SelectedCandidate() const {
  return &selected_candidate_;
}

void IceLite::LocalPassword(const std::string& pwd) {
  local_password_ = pwd;
}