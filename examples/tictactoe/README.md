# Tic-tac-toe over replication

Two processes play each other, and share nothing but GenDB's replication. This is a separate application:
it takes GenDB from a Conan package, as yours would.

## How it works

A [replica](../../docs/replication.md) takes no commits, and replication goes one way. So each player has
two databases: its own, which it writes and serves, and a replica of the other player's.

```text
 player x                                            player o
   own database   ------ replication over TCP ---->    replica
   replica        <----- replication over TCP -----    own database
```

1. **A move is a commit** to the player's own database: the move and the new field, as one transaction.
2. **Replication** carries that commit to the other player, whose readers then see it whole.
3. **A [subscription](../../docs/storage_engine.md#change-subscriptions)** on that replica sees the change.
   If the new `Game` says that it is this player's turn, or that the game has ended, it wakes the player.
4. The player reads the field through snapshots, takes the record with more moves from the two databases,
   and makes its move at random. `x` moves first in each game, and starts the next one.

At the end each player replays every game from the moves in both databases, checking turns, cells and the
field. It prints a line of totals with a fingerprint of all moves, and `run_match.sh` fails unless both
players printed the same line. A player also fails if its replica ever differed from its primary.

## The code

| File | What it shows |
| --- | --- |
| [`game_store.h`](game_store.h) | Working with GenDB: the two databases, the transaction of a move, replication, and the event from the subscription |
| [`main.cpp`](main.cpp) | The application: the rules, choosing moves, checking the match, and starting the program |
| [`schemas`](schemas) | The records, and the collections they are in |

## Build and run

GenDB first, from the top of this repository, into the local Conan cache. Generating code from schemas
needs Python 3 with Jinja2 and PyYAML, here and in the application.

```bash
conan create . --build=missing -s build_type=Release -o "&:with_grpc=False"
```

Then this application, from this directory:

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
./run_match.sh build/tictactoe --n=100000
```

The compiler must support C++23. [`scripts/ci/run.sh example`](../../scripts/ci/run.sh) does all of this in
the [Docker image](../../docs/docker_build.md).

To start the players yourself, in two terminals. Their order does not matter:

```bash
build/tictactoe --player=x --n=100000
```

```bash
build/tictactoe --player=o --n=100000
```

| Flag | Meaning | Default |
| --- | --- | --- |
| `--player` | `x` or `o` | Required |
| `--n` | Games to play before leaving | 1000 |
| `--listen` | The port to serve this player's database on | 7101 for `x`, 7102 for `o` |
| `--peer` | `HOST:PORT` of the other player | `127.0.0.1` and the other's default port |
| `--dir` | Keep the databases on disk here. Both players can share it | Memory only |
| `--seed` | The same seed plays the same match | 1 |
| `--patience` | Seconds to wait for the other player before failing | 30 |

## Results

Two processes on one machine, over loopback, in Docker on a Mac:

| Run | Moves per second | Time |
| --- | ---: | ---: |
| 100,000 games in memory | 14,300 | 53 s |
| 20,000 games on disk | 4,500 | 34 s |

Each move is one commit that crosses to the other process and wakes it, so a move takes about 70 µs in
memory. On disk replication also syncs each commit before sending it, which is most of the difference.

Random play gives a known split, which is a check on the rules:

| | `x` wins | `o` wins | Draws |
| --- | ---: | ---: | ---: |
| 100,000 games here | 58.1% | 29.1% | 12.8% |
| All possible random games | 58.5% | 28.8% | 12.7% |

## Stopping a player

With `--dir`, a player can be killed and started again. It reopens both databases, and its replica continues
from its position. As it looks at the field rather than remembering events, it goes on with the current game.
The other player waits for it, up to `--patience`.

Replication syncs a commit before sending it, so the other player cannot have seen a move that this one lost.

A player in memory cannot come back this way: it starts a new history, empty, and the match cannot go on.
