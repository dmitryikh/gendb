// Two programs in one binary: a primary that commits accounts, and a replica in another process
// that fetches a checkpoint, follows the log and subscribes to the changes.
//
//   replication_demo primary <grpc|tcp> <port> <db directory> <commits>
//   replication_demo replica <grpc|tcp> <port> <db directory> <lsn to wait for>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "account.gendb.h"
#include "database.h"
#include "gendb/replication.h"
#include "tcp_transport.h"
#ifdef GENDB_DEMO_WITH_GRPC
#include <grpcpp/grpcpp.h>

#include "gendb/replication_grpc.h"
#endif

using gendb::tests::AccountBuilder;
using gendb::tests::AccountChange;
using gendb::tests::AccountPatchBuilder;
using gendb::tests::Db;

namespace {

// Commits a stream of accounts, leaving time for replicas to attach partway through.
void Commit(Db& db, uint64_t commits) {
  for (uint64_t id = 1; id <= commits; ++id) {
    {
      auto writer = db.CreateWriter();
      if (id % 2 == 1) {
        (void)writer.PutAccount(
            AccountBuilder().set_account_id(id).set_name("Ann").set_age(30).Build());
      } else {
        (void)writer.UpdateAccount(id - 1, AccountPatchBuilder().set_age(31).Build());
      }
      if (!writer.Commit().ok()) std::exit(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::printf("primary: committed up to lsn %llu\n",
              static_cast<unsigned long long>(db.engine().Stats().lsn));
}

// Gives a replica two seconds to catch up, as the demo then ends.
void WaitForReplica(gendb::ReplicationServer& replication, const gendb::Engine& engine) {
  for (int i = 0; i < 200; ++i) {
    const auto replicas = replication.Replicas();
    if (!replicas.empty() && replicas.front().acked_lsn >= engine.Stats().lsn) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

int RunPrimary(const std::string& transport, uint16_t port, const std::string& path,
               uint64_t commits) {
  auto db = Db::Open({.path = path, .sync_commits = false});
  if (!db.ok()) {
    std::fprintf(stderr, "%s\n", db.status().ToString().c_str());
    return 1;
  }
  gendb::ReplicationServer replication((*db)->engine());

  if (transport == "tcp") {
    auto listener = gendb::examples::TcpListen(port);
    if (!listener.ok()) return 1;
    std::thread acceptor([&replication, fd = *listener] {
      for (;;) {
        const int client = accept(fd, nullptr, nullptr);
        if (client < 0) return;
        std::thread([&replication, client] {
          gendb::examples::TcpConnection connection(client);
          (void)replication.Serve(connection);
        }).detach();
      }
    });
    acceptor.detach();
    Commit(**db, commits);
    WaitForReplica(replication, (*db)->engine());
    replication.Shutdown();
    return 0;
  }
#ifdef GENDB_DEMO_WITH_GRPC
  gendb::GrpcReplicationService service(replication);
  grpc::ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:" + std::to_string(port), grpc::InsecureServerCredentials());
  builder.RegisterService(service.service());
  auto server = builder.BuildAndStart();
  Commit(**db, commits);
  WaitForReplica(replication, (*db)->engine());
  replication.Shutdown();
  server->Shutdown();
  return 0;
#else
  std::fprintf(stderr, "Built without gRPC\n");
  return 1;
#endif
}

gendb::Connector MakeConnector(const std::string& transport, uint16_t port) {
  if (transport == "tcp") return gendb::examples::TcpConnector("127.0.0.1", port);
#ifdef GENDB_DEMO_WITH_GRPC
  return gendb::GrpcConnector(
      grpc::CreateChannel("127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials()));
#else
  std::fprintf(stderr, "Built without gRPC\n");
  std::exit(1);
#endif
}

int RunReplica(const std::string& transport, uint16_t port, const std::string& path, uint64_t lsn) {
  auto db = Db::Open({.path = path, .sync_commits = false, .replica = true});
  if (!db.ok()) {
    std::fprintf(stderr, "%s\n", db.status().ToString().c_str());
    return 1;
  }
  uint64_t inserts = 0, age_updates = 0;
  auto subscription = (*db)->Subscribe({.on_account = [&](const AccountChange& change) {
    if (change.kind() == gendb::ChangeKind::kInsert) ++inserts;
    if (change.kind() == gendb::ChangeKind::kUpdate && change.age_changed()) ++age_updates;
  }});

  gendb::Replica replica((*db)->engine(), MakeConnector(transport, port));
  if (!replica.WaitForLsn(lsn, std::chrono::seconds(60))) {
    std::fprintf(stderr, "replica: timed out: %s\n",
                 replica.Status().last_error.ToString().c_str());
    return 1;
  }
  subscription.WaitUntilDelivered();

  size_t rows = 0;
  size_t aged = 0;
  auto snapshot = (*db)->CreateSnapshot();
  for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) ++rows;
  for (auto it = snapshot.GetAccountByAgeEqual(31); it.Valid(); it.Next()) ++aged;
  const gendb::ReplicaStatus status = replica.Status();
  std::printf(
      "replica: lsn %llu, %zu accounts, %zu aged 31, %llu checkpoint(s), subscriber saw %llu "
      "inserts and %llu age updates\n",
      static_cast<unsigned long long>(status.applied_lsn), rows, aged,
      static_cast<unsigned long long>(status.checkpoints_installed),
      static_cast<unsigned long long>(inserts), static_cast<unsigned long long>(age_updates));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s primary|replica grpc|tcp <port> <directory> <number>\n",
                 argv[0]);
    return 2;
  }
  const std::string role = argv[1];
  const auto port = static_cast<uint16_t>(std::atoi(argv[3]));
  const uint64_t number = std::strtoull(argv[5], nullptr, 10);
  return role == "primary" ? RunPrimary(argv[2], port, argv[4], number)
                           : RunReplica(argv[2], port, argv[4], number);
}
