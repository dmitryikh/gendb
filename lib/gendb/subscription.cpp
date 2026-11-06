#include <utility>

#include "gendb/engine.h"
#include "gendb/engine_internal.h"

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
    std::vector<Change> changes;  // Views into `arena`.
    std::string arena;
    size_t bytes;
  };

  Engine* engine;  // Null once the subscription or the engine has ended.
  ChangeCallback callback;
  std::vector<bool> wanted;  // By collection; empty means all.
  Delivery delivery;
  size_t max_queue_bytes;
  std::vector<Change> scratch;  // Writer only.

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

  void Deliver(uint64_t seq, std::span<const Change> changes);
  void Enqueue(uint64_t seq, std::span<const Change> changes);
  void Run();
  void StopThread();
  void SetOverLimit(bool value);  // Holding `mutex`.
};

void Subscriber::Deliver(uint64_t seq, std::span<const Change> changes) {
  std::span<const Change> selected = changes;
  if (!wanted.empty()) {
    scratch.clear();
    for (const Change& change : changes) {
      if (wanted[change.collection]) scratch.push_back(change);
    }
    if (scratch.empty()) return;
    selected = scratch;
  }
  if (delivery == Delivery::kInline) {
    callback(CommitEvent{seq, selected});
  } else {
    Enqueue(seq, selected);
  }
}

void Subscriber::Enqueue(uint64_t seq, std::span<const Change> changes) {
  size_t arena_size = 0;
  for (const Change& change : changes) {
    arena_size += change.key.size();
    if (change.before.has_value()) arena_size += change.before->size();
    if (change.after.has_value()) arena_size += change.after->size();
  }
  QueuedCommit item{seq, {}, {}, arena_size + changes.size() * sizeof(Change)};
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

void Subscriber::Run() {
  t_on_subscriber_thread = true;
  std::unique_lock lock(mutex);
  for (;;) {
    has_work.wait(lock, [this] { return stop || !queue.empty(); });
    if (stop) return;
    const QueuedCommit item = std::move(queue.front());
    queue.pop_front();
    lock.unlock();
    callback(CommitEvent{item.seq, item.changes});
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

Subscription Engine::Subscribe(ChangeCallback callback, SubscribeOptions options) {
  auto subscriber = std::make_shared<internal::Subscriber>();
  subscriber->engine = this;
  subscriber->callback = std::move(callback);
  subscriber->delivery = options.delivery;
  subscriber->max_queue_bytes = options.max_queue_bytes;
  if (!options.collections.empty()) {
    subscriber->wanted.resize(_schema.collections.size(), false);
    for (size_t collection : options.collections) subscriber->wanted[collection] = true;
  }
  if (options.delivery == Delivery::kQueued) {
    subscriber->thread = std::thread([raw = subscriber.get()] { raw->Run(); });
  }
  auto writer = AcquireWriterFirst();
  _subscribers.push_back(subscriber);
  return Subscription(std::move(subscriber));
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
  for (const auto& subscriber : _subscribers) subscriber->Deliver(seq, changes);
}

void Engine::StopSubscribers() {
  for (const auto& subscriber : _subscribers) subscriber->StopThread();
}

}  // namespace gendb
