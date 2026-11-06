#include "gendb/replication_grpc.h"

#include <grpcpp/grpcpp.h>

#include "replication.grpc.pb.h"

namespace gendb {
namespace {

namespace proto = ::gendb::replication;

absl::Status ClosedStatus() {
  return absl::UnavailableError("The gRPC stream is closed");
}

class ServerConnection final : public Connection {
 public:
  ServerConnection(grpc::ServerContext& context,
                   grpc::ServerReaderWriter<proto::Message, proto::Message>& stream)
      : _context(context), _stream(stream) {}

  absl::Status Send(std::string_view message) override {
    _outgoing.set_payload(message.data(), message.size());
    return _stream.Write(_outgoing) ? absl::OkStatus() : ClosedStatus();
  }
  absl::Status Receive(std::string* message) override {
    if (!_stream.Read(&_incoming)) return ClosedStatus();
    message->swap(*_incoming.mutable_payload());
    return absl::OkStatus();
  }
  void Close() override { _context.TryCancel(); }

 private:
  grpc::ServerContext& _context;
  grpc::ServerReaderWriter<proto::Message, proto::Message>& _stream;
  proto::Message _outgoing;
  proto::Message _incoming;
};

class ClientConnection final : public Connection {
 public:
  explicit ClientConnection(proto::Replication::Stub& stub) : _stream(stub.Session(&_context)) {}
  ~ClientConnection() override {
    _context.TryCancel();
    (void)_stream->Finish();
  }

  absl::Status Send(std::string_view message) override {
    _outgoing.set_payload(message.data(), message.size());
    return _stream->Write(_outgoing) ? absl::OkStatus() : ClosedStatus();
  }
  absl::Status Receive(std::string* message) override {
    if (!_stream->Read(&_incoming)) return ClosedStatus();
    message->swap(*_incoming.mutable_payload());
    return absl::OkStatus();
  }
  void Close() override { _context.TryCancel(); }

 private:
  grpc::ClientContext _context;  // Before the stream, which uses it.
  std::unique_ptr<grpc::ClientReaderWriter<proto::Message, proto::Message>> _stream;
  proto::Message _outgoing;
  proto::Message _incoming;
};

}  // namespace

class GrpcReplicationService::Impl final : public proto::Replication::Service {
 public:
  explicit Impl(ReplicationServer& server) : _server(server) {}

  grpc::Status Session(grpc::ServerContext* context,
                       grpc::ServerReaderWriter<proto::Message, proto::Message>* stream) override {
    ServerConnection connection(*context, *stream);
    const absl::Status status = _server.Serve(connection);
    if (status.ok() || status.code() == absl::StatusCode::kCancelled) return grpc::Status::OK;
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, std::string(status.message()));
  }

 private:
  ReplicationServer& _server;
};

GrpcReplicationService::GrpcReplicationService(ReplicationServer& server)
    : _impl(std::make_unique<Impl>(server)) {}

GrpcReplicationService::~GrpcReplicationService() = default;

grpc::Service* GrpcReplicationService::service() {
  return _impl.get();
}

Connector GrpcConnector(std::shared_ptr<grpc::Channel> channel) {
  auto stub = std::shared_ptr<proto::Replication::Stub>(proto::Replication::NewStub(channel));
  return [stub]() -> absl::StatusOr<std::unique_ptr<Connection>> {
    return std::unique_ptr<Connection>(new ClientConnection(*stub));
  };
}

}  // namespace gendb
