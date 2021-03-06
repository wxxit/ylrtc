#pragma once

#include <string>
#include <optional>

#include "notification.h"
#include "sdptransform/json.hpp"

struct SessionInfo;
/**
 * @brief Handles the signaling of the participants.
 *
 */
class SignalingHandler {
 public:
  SignalingHandler(SessionInfo& session_info);

  /**
   * @brief Handle signaling.
   *
   * @param signaling Participants' requests.
   * @return std::string The response returned.
   */
  std::string HandleSignaling(const std::string& signaling);

  /**
   * @brief Hijack unwanted notifications.
   *
   * @param notification
   * @return true Notifications that should be discarded.
   * @return false Notifications that should not be discarded.
   */
  bool HijackNotification(const Notification& notification);
  std::optional<int64_t> LastReceiveKeepAlive() const;

 private:
  SessionInfo& session_info_;
  nlohmann::json room_info_when_joining_;
  std::optional<int64_t> last_receive_keep_alive_;
};