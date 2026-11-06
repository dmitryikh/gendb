#pragma once

// How the players use GenDB: their two databases, the replication between them, and the event that
// says a turn has come. It knows nothing of the rules, which are in main.cpp.
//
// A replica takes no commits, so each player has a database of its own, which it writes and serves
// to the other, and a replica of the other's:
//
//   player x:  own database  ---- replication ---->  replica       player o
//              replica       <---- replication ----  own database
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "database.h"
#include "gendb/replication.h"
#include "gendb/status.h"
#include "tcp_transport.h"
#include "tictactoe.gendb.h"

namespace tictactoe {

using Board = std::array<Mark, 9>;

struct GameState {
  Board board{};
  uint32_t ply = 0;  // Moves made so far.
  Mark turn = Mark::kX;
  Result result = Result::kInProgress;
};

struct PlayedMove {
  uint32_t ply;
  Mark player;
  uint8_t cell;
};

struct StoreOptions {
  Mark me = Mark::kEmpty;
  uint16_t listen_port = 0;
  std::string peer_host = "127.0.0.1";
  uint16_t peer_port = 0;
  std::string directory;  // Memory only if empty.
};

class GameStore {
 public:
  static absl::StatusOr<std::unique_ptr<GameStore>> Open(const StoreOptions& options) {
    auto own = OpenDb(options, "own", /*replica=*/false);
    if (!own.ok()) return own.status();
    auto their = OpenDb(options, "their", /*replica=*/true);
    if (!their.ok()) return their.status();
    uint16_t port = options.listen_port;
    auto socket = gendb::examples::TcpListen(port);
    if (!socket.ok()) return socket.status();
    return std::unique_ptr<GameStore>(
        new GameStore(options, std::move(*own), std::move(*their), *socket));
  }

  ~GameStore() { StopReplication(); }

  // A game's latest state is in whichever database has more moves of it, as each player only writes
  // its own. A game nobody has started reads as a new one.
  GameState Read(uint64_t game_id) const {
    GameState state;
    for (const Db* db : {_own.get(), _their.get()}) {
      const auto snapshot = db->CreateSnapshot();
      Game game;
      if (!snapshot.GetGame(game_id, game).ok() || game.ply() <= state.ply) continue;
      state.ply = game.ply();
      state.turn = game.turn();
      state.result = game.result();
      for (size_t cell = 0; cell < state.board.size(); ++cell) {
        state.board[cell] = game.board()[cell];
      }
    }
    return state;
  }

  // Commits a move to our own database, from where replication takes it to the other player. One
  // transaction, so that the other player sees the move and the new field together.
  absl::Status WriteMove(uint64_t game_id, const GameState& after, uint8_t cell) {
    auto writer = _own->CreateWriter();
    RETURN_IF_ERROR(writer.PutMove(MoveBuilder()
                                       .set_game_id(game_id)
                                       .set_ply(after.ply)
                                       .set_player(_me)
                                       .set_cell(cell)
                                       .Build()));
    RETURN_IF_ERROR(writer.PutGame(GameBuilder()
                                       .set_game_id(game_id)
                                       .set_board(std::span<const Mark>(after.board))
                                       .set_ply(after.ply)
                                       .set_turn(after.turn)
                                       .set_result(after.result)
                                       .Build()));
    return writer.Commit();
  }

  // All moves of a game by both players, in the order they were made.
  std::vector<PlayedMove> Moves(uint64_t game_id) const {
    std::vector<PlayedMove> moves;
    for (const Db* db : {_own.get(), _their.get()}) {
      const auto snapshot = db->CreateSnapshot();
      for (auto it = snapshot.GetMoveByGameEqual(game_id); it.Valid(); it.Next()) {
        const Move move = it.Value();
        moves.push_back({move.ply(), move.player(), move.cell()});
      }
    }
    std::ranges::sort(moves, {}, &PlayedMove::ply);
    return moves;
  }

  // Events say that something may have changed, for a thread that then looks for itself with Read.
  // Taking the count before looking makes a change between looking and waiting impossible to miss.
  uint64_t events() const {
    const std::lock_guard<std::mutex> lock(_events_mutex);
    return _events;
  }
  // False if nothing came within `timeout`.
  bool WaitForEvent(uint64_t seen, std::chrono::seconds timeout) const {
    std::unique_lock<std::mutex> lock(_events_mutex);
    return _events_changed.wait_for(lock, timeout, [&] { return _events != seen; });
  }

  gendb::ReplicaStatus ReplicationStatus() const { return _replica.Status(); }

  // Waits for the other player to have what it needs of ours, ends replication, and gives its final
  // status. Fails if our replica ever differed from the other player's database.
  absl::StatusOr<gendb::ReplicaStatus> Finish(std::chrono::seconds patience) {
    RETURN_IF_ERROR(WaitForOpponent(patience));
    const gendb::ReplicaStatus status = _replica.Status();
    StopReplication();
    if (status.divergences != 0) {
      return absl::DataLossError("The replica differed from the other player's database");
    }
    return status;
  }

 private:
  GameStore(const StoreOptions& options, std::unique_ptr<Db> own, std::unique_ptr<Db> their,
            int socket)
      : _me(options.me),
        _own(std::move(own)),
        _their(std::move(their)),
        // Before the replica exists, so as to see all it brings.
        _subscription(_their->Subscribe(
            {.on_game = [this](const GameChange& change) { OnOtherPlayersGame(change.after()); }})),
        _server(_own->engine()),
        _socket(socket),
        _acceptor([this] { AcceptConnections(); }),
        _replica(_their->engine(),
                 gendb::examples::TcpConnector(options.peer_host, options.peer_port)) {}

  static absl::StatusOr<std::unique_ptr<Db>> OpenDb(const StoreOptions& options,
                                                    std::string_view name, bool replica) {
    if (options.directory.empty()) return std::make_unique<Db>();
    const auto path =
        std::filesystem::path(options.directory) / (options.me == Mark::kX ? "x" : "o") / name;
    std::filesystem::create_directories(path);
    // Not synced by each commit: replication syncs a commit before it leaves, so the other player
    // cannot have seen a move that this one could lose.
    return Db::Open({.path = path.string(), .sync_commits = false, .replica = replica});
  }

  // The event, on the subscription's thread: the other player has made a move that leaves it to us,
  // or has ended the game.
  void OnOtherPlayersGame(const Game& game) {
    if (game.turn() != _me && game.result() == Result::kInProgress) return;
    {
      const std::lock_guard<std::mutex> lock(_events_mutex);
      ++_events;
    }
    _events_changed.notify_one();
  }

  // Each accepted connection is one session of the other player's replica.
  void AcceptConnections() {
    pollfd waiting{.fd = _socket, .events = POLLIN, .revents = 0};
    while (!_stopping) {
      if (poll(&waiting, 1, 50) <= 0) continue;
      const int client = accept(_socket, nullptr, nullptr);
      if (client < 0) continue;
      _sessions.emplace_back([this, client] {
        gendb::examples::TcpConnection connection(client);
        (void)_server.Serve(connection);
      });
    }
  }

  // The other player may still need our last move. It leaves once it has applied everything,
  // perhaps before its acknowledgement of that arrives, so its leaving counts too.
  absl::Status WaitForOpponent(std::chrono::seconds patience) {
    const uint64_t lsn = _own->engine().Stats().lsn;
    const auto deadline = std::chrono::steady_clock::now() + patience;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto replicas = _server.Replicas();
      if (std::ranges::all_of(replicas, [&](const auto& r) { return r.acked_lsn >= lsn; })) {
        return absl::OkStatus();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return absl::DeadlineExceededError("The other player did not take our last move");
  }

  void StopReplication() {
    _replica.Stop();
    if (_stopping.exchange(true)) return;
    _acceptor.join();
    close(_socket);
    _server.Shutdown();
    for (std::thread& session : _sessions) session.join();
  }

  const Mark _me;
  std::unique_ptr<Db> _own;
  std::unique_ptr<Db> _their;

  mutable std::mutex _events_mutex;
  mutable std::condition_variable _events_changed;
  uint64_t _events = 0;
  gendb::Subscription _subscription;

  gendb::ReplicationServer _server;
  const int _socket;
  std::atomic<bool> _stopping{false};
  std::vector<std::thread> _sessions;
  std::thread _acceptor;
  gendb::Replica _replica;
};

}  // namespace tictactoe
