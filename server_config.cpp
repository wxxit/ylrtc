#include "server_config.h"

#include <exception>

#include "spdlog/spdlog.h"
#include "toml.hpp"

ServerConfig& ServerConfig::GetInstance() {
  static ServerConfig server_config;
  return server_config;
}

bool ServerConfig::Load(std::string_view json_file_name) {
  try {
    const auto config_file = toml::parse(json_file_name.data());

    const auto& webrtc = toml::find(config_file, "webrtc");
    ip_ = toml::find<std::string>(webrtc, "ip");
    announced_ip_ = toml::find<std::string>(webrtc, "announcedIp");
    webrtc_port_ = toml::find<uint16_t>(webrtc, "port");
    webrtc_worker_thread_count_ = toml::find<uint32_t>(webrtc, "workerThreadCount");

    const auto& signaling_server = toml::find(config_file, "signaling_server");
    signaling_server_port_ = toml::find<uint16_t>(signaling_server, "port");
    ssl_cert_file_ = toml::find<std::string>(signaling_server, "certFile");
    ssl_key_file_ = toml::find<std::string>(signaling_server, "keyFile");

    const auto& memory_pool = toml::find(config_file, "memory_pool");
    memory_pool_enabled_ = toml::find<bool>(memory_pool, "enabled");
    memory_pool_max_list_length_ = toml::find<size_t>(memory_pool, "maxListLength");
  } catch (const std::exception& e) {
    spdlog::error("Parse config file failed. error: {}", e.what());
    return false;
  }

  return true;
}

std::string_view ServerConfig::GetIp() const {
  return ip_;
}

std::string_view ServerConfig::GetAnnouncedIp() const {
  return announced_ip_;
}

uint16_t ServerConfig::GetSignalingServerPort() const {
  return signaling_server_port_;
}

uint16_t ServerConfig::GetWebRtcPort() const {
  return webrtc_port_;
}

uint32_t ServerConfig::GetWebrtcWorkerThreadCount() const {
  return webrtc_worker_thread_count_;
}

std::string_view ServerConfig::GetCertFile() const {
  return ssl_cert_file_;
}

std::string_view ServerConfig::GetKeyFile() const {
  return ssl_key_file_;
}

bool ServerConfig::MemoryPoolEnabled() const {
  return memory_pool_enabled_;
}

size_t ServerConfig::MemoryPoolMaxListLength() const {
  return memory_pool_max_list_length_;
}