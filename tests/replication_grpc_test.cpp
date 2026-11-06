#include "gendb/replication_grpc.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "account.fbs.h"
#include "database.h"
#include "gendb/replication.h"

namespace {

using gendb::tests::Account;
using gendb::tests::AccountBuilder;
using gendb::tests::AccountPatchBuilder;
using gendb::tests::Db;

constexpr auto kPatience = std::chrono::seconds(30);

void Put(Db& db, uint64_t id) {
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).set_age(30).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
}

TEST(ReplicationGrpcTest, SeedsFollowsAndReconnectsOverARealServer) {
  Db primary;
  for (uint64_t id = 1; id <= 1000; ++id) Put(primary, id);

  auto replication = std::make_unique<gendb::ReplicationServer>(primary.engine());
  gendb::GrpcReplicationService service(*replication);
  grpc::ServerBuilder builder;
  int port = 0;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(service.service());
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);

  Db replica_db;
  auto channel =
      grpc::CreateChannel("127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials());
  gendb::Replica replica(replica_db.engine(), gendb::GrpcConnector(channel));
  ASSERT_TRUE(replica.WaitForLsn(1000, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);

  {
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(7, AccountPatchBuilder().set_age(77).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(1001, kPatience));
  Account account;
  {
    auto snapshot = replica_db.CreateSnapshot();
    ASSERT_TRUE(snapshot.GetAccount(7, account).ok());
    EXPECT_EQ(account.age(), 77);
  }
  ASSERT_EQ(replication->Replicas().size(), 1u);

  replica.Stop();
  replication->Shutdown();
  server->Shutdown();
}

}  // namespace
