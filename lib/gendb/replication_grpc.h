#pragma once

#include <memory>

#include "gendb/replication.h"

namespace grpc {
class Channel;
class Service;
}  // namespace grpc

// Replication over gRPC. On the primary:
//
//   gendb::ReplicationServer replication(db->engine());
//   gendb::GrpcReplicationService service(replication);
//   grpc::ServerBuilder builder;
//   builder.AddListeningPort(address, grpc::InsecureServerCredentials());
//   builder.RegisterService(service.service());
//   auto server = builder.BuildAndStart();
//
// On the replica:
//
//   auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
//   gendb::Replica replica(db->engine(), gendb::GrpcConnector(channel));
//
// Shut the gRPC server down before destroying these, and `replication` first of all, as it ends the
// streaming calls that grpc::Server::Shutdown would otherwise wait for.
namespace gendb {

class GrpcReplicationService {
 public:
  explicit GrpcReplicationService(ReplicationServer& server);
  ~GrpcReplicationService();
  // Register this with a grpc::ServerBuilder. It can share a server with other services.
  grpc::Service* service();

 private:
  class Impl;
  std::unique_ptr<Impl> _impl;
};

Connector GrpcConnector(std::shared_ptr<grpc::Channel> channel);

}  // namespace gendb
