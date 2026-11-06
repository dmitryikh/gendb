#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gendb/replication.h"

namespace gendb::testing {

// The simplest possible transport: two message queues in one process.
class Pipe {
 public:
  void Push(std::string message) {
    {
      std::lock_guard lock(_mutex);
      _messages.push_back(std::move(message));
    }
    _cv.notify_all();
  }
  absl::Status Pop(std::string* message) {
    std::unique_lock lock(_mutex);
    _cv.wait(lock, [this] { return _reader_gone || _writer_gone || !_messages.empty(); });
    if (_reader_gone || _messages.empty()) return absl::UnavailableError("closed");
    *message = std::move(_messages.front());
    _messages.pop_front();
    return absl::OkStatus();
  }
  // What was sent before the writer left can still be read, as on a socket.
  void WriterLeaves() {
    {
      std::lock_guard lock(_mutex);
      _writer_gone = true;
    }
    _cv.notify_all();
  }
  void ReaderLeaves() {
    {
      std::lock_guard lock(_mutex);
      _reader_gone = true;
    }
    _cv.notify_all();
  }

 private:
  std::mutex _mutex;
  std::condition_variable _cv;
  std::deque<std::string> _messages;
  bool _writer_gone = false;
  bool _reader_gone = false;
};

class PipeConnection final : public gendb::Connection {
 public:
  PipeConnection(std::shared_ptr<Pipe> in, std::shared_ptr<Pipe> out)
      : _in(std::move(in)), _out(std::move(out)) {}
  ~PipeConnection() override { Close(); }

  absl::Status Send(std::string_view message) override {
    _out->Push(std::string(message));
    return absl::OkStatus();
  }
  absl::Status Receive(std::string* message) override { return _in->Pop(message); }
  void Close() override {
    _in->ReaderLeaves();
    _out->WriterLeaves();
  }

 private:
  std::shared_ptr<Pipe> _in, _out;
};

// A primary's end: each connection gets a thread calling Serve, like a server's accept loop.
class LoopbackServer {
 public:
  explicit LoopbackServer(gendb::Engine& engine, gendb::ReplicationServerOptions options = {})
      : _server(engine, options) {}
  ~LoopbackServer() {
    _server.Shutdown();
    for (auto& thread : _threads) thread.join();
  }

  gendb::Connector connector() {
    return [this]() -> absl::StatusOr<std::unique_ptr<gendb::Connection>> {
      std::lock_guard lock(_mutex);
      if (_refuse) return absl::UnavailableError("refused");
      auto to_server = std::make_shared<Pipe>();
      auto to_client = std::make_shared<Pipe>();
      auto server_side = std::make_shared<PipeConnection>(to_server, to_client);
      _open.push_back(server_side);
      _threads.emplace_back([this, server_side] { (void)_server.Serve(*server_side); });
      return std::unique_ptr<gendb::Connection>(new PipeConnection(to_client, to_server));
    };
  }
  // Breaks every connection, as a network failure would.
  void DropConnections() {
    std::lock_guard lock(_mutex);
    for (auto& connection : _open) connection->Close();
    _open.clear();
  }
  void Refuse(bool refuse) {
    std::lock_guard lock(_mutex);
    _refuse = refuse;
  }
  gendb::ReplicationServer& server() { return _server; }

 private:
  gendb::ReplicationServer _server;
  std::mutex _mutex;
  std::vector<std::shared_ptr<PipeConnection>> _open;
  std::vector<std::thread> _threads;
  bool _refuse = false;
};

}  // namespace gendb::testing
