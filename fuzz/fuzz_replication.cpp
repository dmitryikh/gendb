// Both ends of a session, against a peer that sends anything at all.
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "gendb/engine.h"
#include "gendb/replication.h"

using namespace gendb;
using internal::FrameType;

namespace {

// As in replication.cpp, which keeps them to itself.
constexpr char kHello = 1, kWelcome = 2, kCheckpointData = 4, kLogData = 5, kHeartbeat = 6;

// Plays back messages, then ends. What is sent to it goes nowhere.
class ScriptedConnection final : public Connection {
 public:
  explicit ScriptedConnection(std::vector<std::string> messages)
      : _messages(messages.begin(), messages.end()) {}
  absl::Status Send(std::string_view) override { return absl::OkStatus(); }
  absl::Status Receive(std::string* message) override {
    std::lock_guard lock(_mutex);
    if (_closed || _messages.empty()) return absl::UnavailableError("End of the script");
    *message = std::move(_messages.front());
    _messages.pop_front();
    return absl::OkStatus();
  }
  void Close() override {
    std::lock_guard lock(_mutex);
    _closed = true;
  }

 private:
  std::mutex _mutex;
  std::deque<std::string> _messages;
  bool _closed = false;
};

std::string Welcome(bool checkpoint) {
  std::string message(1, kWelcome);
  internal::PutFixed32(message, kReplicationProtocolVersion);
  internal::PutHistory(message, internal::HistoryId{1, 2});
  internal::PutFixed64(message, 0);
  message.push_back(checkpoint ? 1 : 0);
  internal::PutVarint(message, 0);
  return message;
}

std::string Data(char type, std::string_view bytes) {
  std::string message(1, type);
  internal::PutFixed64(message, 0);
  if (type != kCheckpointData) {
    internal::PutFixed64(message, 0);
    internal::PutFixed64(message, 0);
  }
  message.append(bytes);
  return message;
}

void FuzzReplica(fuzz::Pieces& input) {
  std::vector<std::string> messages;
  const uint8_t shape = input.Byte();
  if ((shape & 1) != 0) {
    messages.push_back(std::string(input.Next()));  // Maybe no Welcome at all.
  } else {
    messages.push_back(Welcome((shape & 2) != 0));
  }
  for (uint64_t lsn = 1; !input.empty(); ++lsn) {
    const uint8_t choice = input.Byte();
    switch (choice % 4) {
      case 0:
        messages.push_back(std::string(input.Next()));
        break;
      case 1:
        messages.push_back(
            Data(kLogData, fuzz::MakeFrame(FrameType::kCommit, fuzz::CommitBody(lsn, input))));
        break;
      case 2:
        messages.push_back(Data(
            kCheckpointData, fuzz::MakeFrame(static_cast<FrameType>(input.Byte()), input.Next())));
        break;
      default:
        messages.push_back(Data(kHeartbeat, input.Next()));
        break;
    }
  }

  Engine engine(fuzz::MakeSchema());
  // Only once: this is about one session's messages.
  auto used = std::make_shared<std::atomic<bool>>(false);
  Replica replica(engine,
                  [&messages, used]() -> absl::StatusOr<std::unique_ptr<Connection>> {
                    if (used->exchange(true)) return absl::UnavailableError("One session only");
                    return std::unique_ptr<Connection>(new ScriptedConnection(messages));
                  },
                  {.min_backoff = std::chrono::milliseconds(1)});
  while (!used->load() || replica.Status().connections < 2) {
    std::this_thread::yield();
  }
  replica.Stop();
}

void FuzzServer(fuzz::Pieces& input) {
  Engine engine(fuzz::MakeSchema());
  {
    Transaction txn(engine);
    const std::array<uint8_t, 1> key{1};
    (void)txn.Put(0, key, key);
    (void)txn.Commit();
  }
  ReplicationServer server(engine, {.replica_timeout = std::chrono::milliseconds(1)});
  std::vector<std::string> messages;
  const uint8_t shape = input.Byte();
  if ((shape & 1) == 0) {
    std::string hello(1, kHello);
    internal::PutFixed32(hello, input.Byte());
    internal::PutFixed32(hello, input.Byte());
    internal::PutHistory(hello, internal::HistoryId{1, 2});
    internal::PutFixed64(hello, input.Byte());
    hello.push_back(static_cast<char>(input.Byte() % 2));
    messages.push_back(std::move(hello));
  }
  while (!input.empty()) messages.push_back(std::string(input.Next()));
  ScriptedConnection connection(std::move(messages));
  (void)server.Serve(connection);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzz::Pieces input(data, size);
  if (input.Byte() % 2 == 0) {
    FuzzReplica(input);
  } else {
    FuzzServer(input);
  }
  return 0;
}
