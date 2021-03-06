#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "publish_stream.h"
#include "random.h"
#include "sdptransform/json.hpp"
#include "subscribe_stream.h"
#include "webrtc_stream.h"
#include "yl_error.h"

/**
 * @brief This class represents a conference room.
 *
 */
class Room : public WebrtcStream::Observer, public std::enable_shared_from_this<Room> {
 public:
  explicit Room(const std::string& id);
  ~Room();

  /**
   * @brief Get room ID.
   *
   * @return const std::string&
   */
  const std::string& Id() const;

  /**
   * @brief Join meeting.
   *
   * @param participant_id ID of the participant to join the room.
   * @return YlError
   */
  YlError Join(const std::string& participant_id);

  /**
   * @brief Leave meeting.
   *
   * @param participant_id ID of the participant who left the meeting.
   */
  void Leave(const std::string& participant_id);

  /**
   * @brief Kick out a participant.
   *
   * @param participant_id ID of the participant to kick out.
   */
  void KickoutParticipant(const std::string& participant_id);
  std::shared_ptr<PublishStream> ParticipantPublish(const std::string& participant_id, const std::string& offer);
  std::shared_ptr<SubscribeStream> ParticipantSubscribe(const std::string& src_participant_id,
                                                        const std::string& dst_participant_id,
                                                        const std::string& stream_id,
                                                        const std::string& sdp);

  /**
   * @brief Get the Room Info.
   */
  nlohmann::json GetRoomInfo();

  /**
   * @brief Get the publishing stream according to the ID.
   *
   * @param id ID of the publishing stream.
   * @return std::shared_ptr<PublishStream>
   */
  std::shared_ptr<WebrtcStream> GetStreamById(const std::string& id);

  /**
   * @brief Dissolution meeting.
   *
   */
  void Destroy();

 private:
  void OnWebrtcStreamConnected(const std::string& stream_id) override;
  void OnWebrtcStreamShutdown(const std::string& stream_id) override;
  bool StreamIdExist(const std::string& stream_id);
  Room(const Room&) = delete;
  Room& operator=(const Room&) = delete;
  const static uint32_t kParticipantIdLength = 64;
  std::string id_;
  std::unordered_set<std::string> participant_id_set_;
  std::unordered_map<std::string, std::unordered_set<std::shared_ptr<PublishStream>>> participant_publishs_map_;
  std::unordered_map<std::shared_ptr<PublishStream>, std::unordered_set<std::shared_ptr<SubscribeStream>>> publish_subscribes_map_;
  std::unordered_map<std::string, std::unordered_set<std::shared_ptr<SubscribeStream>>> participant_subscribes_map_;
  Random random_;
};