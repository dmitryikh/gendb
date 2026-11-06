#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gendb/engine.h"
#include "gendb/engine_internal.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsBytes;
using internal::AsStringView;

namespace {

thread_local bool t_on_subscriber_thread = false;

}  // namespace

namespace internal {

// One subscription's delivery state.
struct Subscriber {
  struct QueuedCommit {
    uint64_t seq;
    ChangePosition position;
    std::vector<Change> changes;  // Views into `arena`.
    std::string arena;
    size_t bytes;
  };
  // What changed while the subscriber was away, delivered before anything in the queue.
  struct CatchUp {
    std::optional<EngineSnapshot> snapshot;  // Exactly at `end`.
    ChangePosition start;
    ChangePosition end;
    std::vector<std::pair<size_t, std::string>> keys;
  };

  Engine* engine;  // Null once the subscription or the engine has ended.
  ChangeCallback callback;
  std::vector<bool> wanted;  // By collection; empty means all.
  Delivery delivery;
  size_t max_queue_bytes;
  std::vector<Change> scratch;  // Writer only.
  std::unique_ptr<CatchUp> catch_up;

  mutable std::mutex mutex;
  mutable std::condition_variable has_work;
  mutable std::condition_variable delivered_cv;
  std::deque<QueuedCommit> queue;
  size_t queue_bytes = 0;
  uint64_t pushed = 0;
  uint64_t delivered = 0;
  bool over_limit = false;
  bool stop = false;
  std::thread thread;

  void Deliver(const CommitEvent& event);
  void Enqueue(const CommitEvent& event, std::span<const Change> changes);
  void Run();
  void DeliverCatchUp();
  void StopThread();
  void SetOverLimit(bool value);  // Holding `mutex`.
};

void Subscriber::Deliver(const CommitEvent& event) {
  std::span<const Change> selected = event.changes;
  if (!wanted.empty()) {
    scratch.clear();
    for (const Change& change : event.changes) {
      if (wanted[change.collection]) scratch.push_back(change);
    }
    if (scratch.empty()) return;
    selected = scratch;
  }
  if (delivery == Delivery::kInline) {
    callback(CommitEvent{event.seq, selected, event.position});
  } else {
    Enqueue(event, selected);
  }
}

void Subscriber::Enqueue(const CommitEvent& event, std::span<const Change> changes) {
  size_t arena_size = 0;
  for (const Change& change : changes) {
    arena_size += change.key.size();
    if (change.before.has_value()) arena_size += change.before->size();
    if (change.after.has_value()) arena_size += change.after->size();
  }
  QueuedCommit item{
      event.seq, event.position, {}, {}, arena_size + changes.size() * sizeof(Change)};
  item.arena.reserve(arena_size);  // Never reallocates below, so the views stay valid.
  item.changes.reserve(changes.size());
  const auto copy = [&item](BytesConstView bytes) {
    const size_t at = item.arena.size();
    item.arena.append(AsStringView(bytes));
    return AsBytes(std::string_view(item.arena).substr(at, bytes.size()));
  };
  for (const Change& change : changes) {
    Change owned{change.collection, copy(change.key), std::nullopt, std::nullopt};
    if (change.before.has_value()) owned.before = copy(*change.before);
    if (change.after.has_value()) owned.after = copy(*change.after);
    item.changes.push_back(owned);
  }
  {
    std::lock_guard lock(mutex);
    queue_bytes += item.bytes;
    queue.push_back(std::move(item));
    ++pushed;
    if (!over_limit && queue_bytes >= max_queue_bytes) SetOverLimit(true);
  }
  has_work.notify_one();
}

void Subscriber::DeliverCatchUp() {
  constexpr size_t kChangesPerEvent = 1024;
  CatchUp& state = *catch_up;
  std::vector<Change> changes;
  for (size_t at = 0; at < state.keys.size();) {
    {
      std::lock_guard lock(mutex);
      if (stop) break;
    }
    changes.clear();
    const size_t end = std::min(at + kChangesPerEvent, state.keys.size());
    for (; at < end; ++at) {
      const auto& [collection, key] = state.keys[at];
      changes.push_back({collection, AsBytes(key), std::nullopt,
                         state.snapshot->Get(collection, AsBytes(key)), /*state_only=*/true});
    }
    // Resuming from partway would miss the first part, so only the last event moves the position.
    const bool last = at == state.keys.size();
    callback(CommitEvent{0, changes, last ? state.end : state.start});
  }
  catch_up.reset();  // Lets go of the snapshot.
}

void Subscriber::Run() {
  t_on_subscriber_thread = true;
  if (catch_up != nullptr) DeliverCatchUp();
  std::unique_lock lock(mutex);
  for (;;) {
    has_work.wait(lock, [this] { return stop || !queue.empty(); });
    if (stop) return;
    const QueuedCommit item = std::move(queue.front());
    queue.pop_front();
    lock.unlock();
    callback(CommitEvent{item.seq, item.changes, item.position});
    lock.lock();
    queue_bytes -= item.bytes;
    ++delivered;
    if (over_limit && queue_bytes <= max_queue_bytes / 2) SetOverLimit(false);
    delivered_cv.notify_all();
  }
}

void Subscriber::SetOverLimit(bool value) {
  over_limit = value;
  if (value) {
    engine->_subscribers_over_limit.fetch_add(1, std::memory_order_acq_rel);
    return;
  }
  {
    std::lock_guard room(engine->_room_mutex);
    engine->_subscribers_over_limit.fetch_sub(1, std::memory_order_acq_rel);
  }
  engine->_room_cv.notify_all();
}

void Subscriber::StopThread() {
  {
    std::lock_guard lock(mutex);
    stop = true;
    if (over_limit) SetOverLimit(false);
    engine = nullptr;
  }
  has_work.notify_all();
  delivered_cv.notify_all();
  if (thread.joinable()) thread.join();
}

}  // namespace internal

void Engine::WaitForSubscriberRoom() {
  if (_subscribers_over_limit.load(std::memory_order_acquire) == 0 || t_on_subscriber_thread) {
    return;
  }
  std::unique_lock lock(_room_mutex);
  _room_cv.wait(lock,
                [this] { return _subscribers_over_limit.load(std::memory_order_acquire) == 0; });
}

std::shared_ptr<internal::Subscriber> Engine::NewSubscriber(ChangeCallback callback,
                                                            const SubscribeOptions& options) {
  auto subscriber = std::make_shared<internal::Subscriber>();
  subscriber->engine = this;
  subscriber->callback = std::move(callback);
  subscriber->delivery = options.delivery;
  subscriber->max_queue_bytes = options.max_queue_bytes;
  if (!options.collections.empty()) {
    subscriber->wanted.resize(_schema.collections.size(), false);
    for (size_t collection : options.collections) subscriber->wanted[collection] = true;
  }
  return subscriber;
}

Subscription Engine::Subscribe(ChangeCallback callback, SubscribeOptions options) {
  auto subscriber = NewSubscriber(std::move(callback), options);
  if (options.delivery == Delivery::kQueued) {
    subscriber->thread = std::thread([raw = subscriber.get()] { raw->Run(); });
  }
  auto writer = AcquireWriterFirst();
  _subscribers.push_back(subscriber);
  return Subscription(std::move(subscriber));
}

absl::StatusOr<Subscription> Engine::SubscribeFrom(const ChangePosition& position,
                                                   ChangeCallback callback,
                                                   SubscribeOptions options) {
  if (options.delivery != Delivery::kQueued) {
    return absl::InvalidArgumentError("Catching up takes Delivery::kQueued");
  }
  auto subscriber = NewSubscriber(std::move(callback), options);
  auto catch_up = std::make_unique<internal::Subscriber::CatchUp>();
  uint64_t pin = 0;
  {
    // From here on the queue collects new commits, while the log is read for the older ones.
    auto writer = AcquireWriterFirst();
    const uint64_t lsn = _lsn.load(std::memory_order_relaxed);
    const ChangePosition current = ChangePositionAt(lsn);
    if (position.history_high != current.history_high ||
        position.history_low != current.history_low) {
      return absl::OutOfRangeError("The position is from another history of this database");
    }
    if (position.lsn > lsn) {
      return absl::OutOfRangeError("The position is ahead of this database");
    }
    if (position.lsn < lsn) {
      if (_wal == nullptr || !_wal->SegmentFor(position.lsn + 1).has_value()) {
        return absl::OutOfRangeError("The log no longer reaches back to the position");
      }
      pin = PinWal(position.lsn);
      catch_up->snapshot.emplace(*this);
    }
    catch_up->start = position;
    catch_up->end = current;
    _subscribers.push_back(subscriber);
  }

  absl::Status status;
  if (position.lsn < catch_up->end.lsn) {
    // By ID, as the log has it. Foreign collections have no subscribers.
    std::vector<std::pair<uint32_t, size_t>> ids;
    for (size_t c = 0; c < _schema.collections.size(); ++c) {
      if (!_collections[c]->ephemeral && (subscriber->wanted.empty() || subscriber->wanted[c])) {
        ids.emplace_back(_collections[c]->id, c);
      }
    }
    std::set<std::pair<size_t, std::string>> keys;
    internal::WalCursor cursor(*_wal);
    uint64_t read_to = position.lsn;
    std::string frames;
    while (status.ok() && read_to < catch_up->end.lsn) {
      frames.clear();
      auto read = cursor.Read(&read_to, catch_up->end.lsn, size_t{1} << 20, &frames);
      if (!read.ok()) {
        status = read.status();
      } else if (!*read) {
        status = absl::OutOfRangeError("The log no longer reaches back to the position");
      }
      std::string_view rest = frames;
      while (status.ok() && !rest.empty()) {
        internal::Frame frame;
        if (internal::ParseFrame(rest, &frame) != internal::ParseStatus::kOk) {
          status = absl::DataLossError("Damaged commit in the log");
          break;
        }
        internal::CommitDecoder commit(frame.body);
        for (uint32_t i = 0; i < commit.count; ++i) {
          const internal::CommitEntry entry = commit.Next();
          if (!commit.ok()) {
            status = absl::DataLossError("Malformed commit in the log");
            break;
          }
          for (const auto& [id, collection] : ids) {
            if (id == entry.collection_id) keys.emplace(collection, std::string(entry.key));
          }
        }
        rest.remove_prefix(frame.raw.size());
      }
    }
    UnpinWal(pin);
    catch_up->keys.assign(keys.begin(), keys.end());
  }
  if (!status.ok()) {
    Unsubscribe(subscriber);
    return status;
  }
  if (!catch_up->keys.empty()) subscriber->catch_up = std::move(catch_up);
  subscriber->thread = std::thread([raw = subscriber.get()] { raw->Run(); });
  return Subscription(std::move(subscriber));
}

ChangePosition Engine::ChangePositionAt(uint64_t lsn) const noexcept {
  return {_history.high, _history.low, lsn};
}

ChangePosition Engine::CurrentChangePosition() {
  auto writer = AcquireWriterFirst();
  return ChangePositionAt(_lsn.load(std::memory_order_relaxed));
}

void Engine::Unsubscribe(const std::shared_ptr<internal::Subscriber>& subscriber) {
  {
    auto writer = AcquireWriterFirst();
    std::erase(_subscribers, subscriber);
  }
  subscriber->StopThread();
}

void Subscription::Cancel() {
  if (_subscriber == nullptr) return;
  Engine* engine;
  {
    std::lock_guard lock(_subscriber->mutex);
    engine = _subscriber->engine;
  }
  if (engine != nullptr) engine->Unsubscribe(_subscriber);
  _subscriber.reset();
}

void Subscription::WaitUntilDelivered() const {
  if (_subscriber == nullptr || _subscriber->delivery == Delivery::kInline) return;
  std::unique_lock lock(_subscriber->mutex);
  const uint64_t target = _subscriber->pushed;
  _subscriber->delivered_cv.wait(
      lock, [&] { return _subscriber->stop || _subscriber->delivered >= target; });
}

void Engine::Notify(uint64_t seq, std::span<const Change> changes) {
  const CommitEvent event{seq, changes, ChangePositionAt(_lsn.load(std::memory_order_relaxed))};
  for (const auto& subscriber : _subscribers) subscriber->Deliver(event);
}

void Engine::StopSubscribers() {
  for (const auto& subscriber : _subscribers) subscriber->StopThread();
}

}  // namespace gendb
