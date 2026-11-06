// Two processes play tic-tac-toe, and all they share is GenDB's replication. This file has the
// rules, the moves and the program. game_store.h has how they use the database, and where the
// events come from. The README in this directory shows how to build and run it.
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "game_store.h"
#include "gendb/status.h"

namespace {

using tictactoe::Board;
using tictactoe::GameState;
using tictactoe::GameStore;
using tictactoe::Mark;
using tictactoe::PlayedMove;
using tictactoe::Result;
using tictactoe::StoreOptions;

struct Options {
  StoreOptions store;
  uint64_t games = 1000;
  uint64_t seed = 1;
  std::chrono::seconds patience{30};
};

char Name(Mark mark) {
  return mark == Mark::kX ? 'x' : 'o';
}

Mark Opponent(Mark mark) {
  return mark == Mark::kX ? Mark::kO : Mark::kX;
}

Result Judge(const Board& board, uint32_t ply) {
  static constexpr int kLines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                       {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
  for (const auto& line : kLines) {
    const Mark mark = board[line[0]];
    if (mark != Mark::kEmpty && mark == board[line[1]] && mark == board[line[2]]) {
      return mark == Mark::kX ? Result::kXWins : Result::kOWins;
    }
  }
  return ply == board.size() ? Result::kDraw : Result::kInProgress;
}

// Takes any empty cell, at random.
absl::Status MakeMove(GameStore& store, uint64_t game_id, GameState state, Mark me,
                      std::mt19937_64& random) {
  std::vector<uint8_t> empty;
  for (uint8_t cell = 0; cell < state.board.size(); ++cell) {
    if (state.board[cell] == Mark::kEmpty) empty.push_back(cell);
  }
  const uint8_t cell = empty[random() % empty.size()];
  state.board[cell] = me;
  ++state.ply;
  state.result = Judge(state.board, state.ply);
  state.turn = state.result == Result::kInProgress ? Opponent(me) : Mark::kEmpty;
  return store.WriteMove(game_id, state, cell);
}

// Looks at the field again each time an event says so, rather than taking what the event says,
// which also makes a restart continue from where the game is.
absl::Status PlayAll(const Options& options, GameStore& store) {
  const Mark me = options.store.me;
  std::mt19937_64 random(options.seed * 2 + (me == Mark::kX ? 0 : 1));
  for (uint64_t game_id = 1; game_id <= options.games; ++game_id) {
    for (;;) {
      const uint64_t seen = store.events();
      const GameState state = store.Read(game_id);
      if (state.result != Result::kInProgress) break;
      if (state.turn == me) {
        RETURN_IF_ERROR(MakeMove(store, game_id, state, me, random));
      } else if (!store.WaitForEvent(seen, options.patience)) {
        return absl::DeadlineExceededError(
            absl::StrCat("Nothing from the other player in game ", game_id,
                         ". Replication says: ", store.ReplicationStatus().last_error.ToString()));
      }
    }
    if (options.games >= 10 && game_id % (options.games / 10) == 0) {
      std::printf("player %c: %llu games\n", Name(me), static_cast<unsigned long long>(game_id));
      std::fflush(stdout);
    }
  }
  return absl::OkStatus();
}

struct Totals {
  uint64_t x_wins = 0;
  uint64_t o_wins = 0;
  uint64_t draws = 0;
  uint64_t moves = 0;
  uint64_t fingerprint = 14695981039346656037u;  // FNV-1a of every move, to compare the two sides.

  void Mix(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      fingerprint = (fingerprint ^ ((value >> (i * 8)) & 0xff)) * 1099511628211u;
    }
  }
};

// Whether the two databases together hold a legal match, replayed from the moves alone.
absl::StatusOr<Totals> Audit(const GameStore& store, uint64_t games) {
  Totals totals;
  for (uint64_t game_id = 1; game_id <= games; ++game_id) {
    const auto wrong = [game_id](std::string_view what) {
      return absl::DataLossError(absl::StrCat("Game ", game_id, ": ", what));
    };
    Board board{};
    Mark turn = Mark::kX;
    Result result = Result::kInProgress;
    uint32_t ply = 0;
    for (const PlayedMove& move : store.Moves(game_id)) {
      if (result != Result::kInProgress) return wrong("a move after the end");
      if (move.ply != ++ply) return wrong("a move is missing");
      if (move.player != turn) return wrong("a move out of turn");
      if (move.cell >= board.size() || board[move.cell] != Mark::kEmpty) {
        return wrong("a move onto a taken cell");
      }
      board[move.cell] = move.player;
      turn = Opponent(turn);
      result = Judge(board, ply);
      totals.Mix(game_id);
      totals.Mix(ply);
      totals.Mix(move.cell);
    }
    totals.moves += ply;

    const GameState state = store.Read(game_id);
    if (state.ply != ply || state.board != board || state.result != result) {
      return wrong("the field does not match the moves");
    }
    switch (result) {
      case Result::kXWins:
        ++totals.x_wins;
        break;
      case Result::kOWins:
        ++totals.o_wins;
        break;
      case Result::kDraw:
        ++totals.draws;
        break;
      default:
        return wrong("it has not ended");
    }
  }
  return totals;
}

absl::Status Run(const Options& options) {
  auto store = GameStore::Open(options.store);
  if (!store.ok()) return store.status();

  const auto start = std::chrono::steady_clock::now();
  RETURN_IF_ERROR(PlayAll(options, **store));
  const std::chrono::duration<double> played = std::chrono::steady_clock::now() - start;

  const auto replication = (*store)->Finish(options.patience);
  if (!replication.ok()) return replication.status();
  const auto totals = Audit(**store, options.games);
  if (!totals.ok()) return totals.status();

  std::printf("player %c: %.1f s, %.0f moves/s, replica verified up to lsn %llu of %llu\n",
              Name(options.store.me), played.count(),
              static_cast<double>(totals->moves) / played.count(),
              static_cast<unsigned long long>(replication->verified_lsn),
              static_cast<unsigned long long>(replication->applied_lsn));
  // run_match.sh compares this line between the two players.
  std::printf(
      "result games=%llu x_wins=%llu o_wins=%llu draws=%llu moves=%llu fingerprint=%016llx\n",
      static_cast<unsigned long long>(options.games),
      static_cast<unsigned long long>(totals->x_wins),
      static_cast<unsigned long long>(totals->o_wins),
      static_cast<unsigned long long>(totals->draws),
      static_cast<unsigned long long>(totals->moves),
      static_cast<unsigned long long>(totals->fingerprint));
  return absl::OkStatus();
}

bool ParseFlag(std::string_view arg, std::string_view name, std::string_view& value) {
  if (!arg.starts_with(name) || arg.size() <= name.size() || arg[name.size()] != '=') return false;
  value = arg.substr(name.size() + 1);
  return true;
}

template <typename T>
bool ParseNumber(std::string_view text, T& out) {
  uint64_t number = 0;
  if (!absl::SimpleAtoi(text, &number) || number > std::numeric_limits<T>::max()) return false;
  out = static_cast<T>(number);
  return true;
}

bool ParseOptions(int argc, char** argv, Options& options) {
  StoreOptions& store = options.store;
  for (int i = 1; i < argc; ++i) {
    std::string_view value;
    uint64_t patience = 0;
    if (ParseFlag(argv[i], "--player", value)) {
      if (value == "x") store.me = Mark::kX;
      if (value == "o") store.me = Mark::kO;
    } else if (ParseFlag(argv[i], "--n", value)) {
      if (!ParseNumber(value, options.games)) return false;
    } else if (ParseFlag(argv[i], "--listen", value)) {
      if (!ParseNumber(value, store.listen_port)) return false;
    } else if (ParseFlag(argv[i], "--peer", value)) {
      const size_t colon = value.rfind(':');
      if (colon == std::string_view::npos) return false;
      store.peer_host = std::string(value.substr(0, colon));
      if (!ParseNumber(value.substr(colon + 1), store.peer_port)) return false;
    } else if (ParseFlag(argv[i], "--dir", value)) {
      store.directory = std::string(value);
    } else if (ParseFlag(argv[i], "--seed", value)) {
      if (!ParseNumber(value, options.seed)) return false;
    } else if (ParseFlag(argv[i], "--patience", value)) {
      if (!ParseNumber(value, patience)) return false;
      options.patience = std::chrono::seconds(patience);
    } else {
      return false;
    }
  }
  if (store.me == Mark::kEmpty) return false;
  // Two players on one machine need nothing more than which one they are.
  const bool x = store.me == Mark::kX;
  if (store.listen_port == 0) store.listen_port = x ? 7101 : 7102;
  if (store.peer_port == 0) store.peer_port = x ? 7102 : 7101;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  Options options;
  if (!ParseOptions(argc, argv, options)) {
    std::fprintf(
        stderr,
        "usage: %s --player=x|o [--n=1000] [--listen=PORT] [--peer=HOST:PORT] [--dir=PATH] "
        "[--seed=1] [--patience=SECONDS]\n",
        argv[0]);
    return 2;
  }
  if (const absl::Status status = Run(options); !status.ok()) {
    std::fprintf(stderr, "player failed: %s\n", status.ToString().c_str());
    return 1;
  }
  return 0;
}
