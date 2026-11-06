#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "gendb/replication.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS; a write to a closed socket raises SIGPIPE there unless ignored.
#endif

// A replication transport over plain TCP, as an example of how little one takes: a Connection that
// frames messages with a length prefix. There is no encryption or authentication: use it only on a
// trusted network.
namespace gendb::examples {

class TcpConnection final : public Connection {
 public:
  explicit TcpConnection(int fd) : _fd(fd) {
    const int one = 1;
    setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
  }
  ~TcpConnection() override { close(_fd); }

  absl::Status Send(std::string_view message) override {
    const auto size = static_cast<uint32_t>(message.size());
    if (!WriteAll({reinterpret_cast<const char*>(&size), sizeof(size)})) return Closed();
    return WriteAll(message) ? absl::OkStatus() : Closed();
  }
  absl::Status Receive(std::string* message) override {
    uint32_t size = 0;
    if (!ReadAll(reinterpret_cast<char*>(&size), sizeof(size))) return Closed();
    if (size > kMaxReplicationMessageBytes) return absl::DataLossError("Message too large");
    message->resize(size);
    return ReadAll(message->data(), size) ? absl::OkStatus() : Closed();
  }
  // shutdown, unlike close, wakes a thread blocked in read or write.
  void Close() override { shutdown(_fd, SHUT_RDWR); }

 private:
  static absl::Status Closed() { return absl::UnavailableError("The TCP connection is closed"); }

  bool WriteAll(std::string_view data) {
    while (!data.empty()) {
      const ssize_t written = send(_fd, data.data(), data.size(), MSG_NOSIGNAL);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) return false;
      data.remove_prefix(static_cast<size_t>(written));
    }
    return true;
  }
  bool ReadAll(char* out, size_t size) {
    while (size > 0) {
      const ssize_t got = recv(_fd, out, size, 0);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) return false;
      out += got;
      size -= static_cast<size_t>(got);
    }
    return true;
  }

  int _fd;
};

// A connector for Replica.
inline Connector TcpConnector(std::string host, uint16_t port) {
  return [host = std::move(host), port]() -> absl::StatusOr<std::unique_ptr<Connection>> {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0) {
      return absl::UnavailableError(absl::StrCat("Cannot resolve ", host));
    }
    int fd = -1;
    for (const addrinfo* a = found; a != nullptr && fd < 0; a = a->ai_next) {
      fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd >= 0 && connect(fd, a->ai_addr, a->ai_addrlen) != 0) {
        close(fd);
        fd = -1;
      }
    }
    freeaddrinfo(found);
    if (fd < 0) return absl::UnavailableError(absl::StrCat("Cannot connect to ", host));
    return std::unique_ptr<Connection>(new TcpConnection(fd));
  };
}

// Listens on `port`, or on any free port if it is 0. Returns the socket, and sets `port`.
inline absl::StatusOr<int> TcpListen(uint16_t& port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return absl::ErrnoToStatus(errno, "socket");
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  socklen_t size = sizeof(address);
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), size) != 0 || listen(fd, 16) != 0 ||
      getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
    const int error = errno;
    close(fd);
    return absl::ErrnoToStatus(error, "listen");
  }
  port = ntohs(address.sin_port);
  return fd;
}

}  // namespace gendb::examples
