
## Focus:
1. (Done) Deserialization-free message format.
2. (P0) copy-free reads of committed state.
3. (P1) In-memory secondary indices
4. (P0) Codegenerated DB API.
5. Snapshotting.


```cpp
// Db can be:
// 1. Transaction (uncomitted state)
// 2. Comitted DB
// 3. Snapshot

// SELECT trader_id FROM accounts WHERE age < 18 AND is_active = true;

// Multiple concurrent writes? Can we delegate it to the user?
// This function will compile only for Db under read lock or for Snapshots. Because it uses index traversing + zero-copy AccountFb.
template <typename DbHandler>
std::vector<int, B> MyFunc(DbHandler&& db) {
    auto accounts = db.Collection<AccountFb>();

    std::unordered_set<std::string> trader_ids;

    auto it = db.Index<AccountByAge>().Iterator(std::nullopt, 18);
    while (it) {
        int account_id = it->second;
        AccountFb account = accounts.Get<AccountFb>(account_id);  // Zero copy method is available for DB under read lock or for Snapshots.
        trader_ids.insert(account.trader_id());
        ++it;
    }
    return trader_ids;
}

std::vector<int, B> GetAccountsByTraderId(DbSnapshot&& db, std::string trader_id) {
    auto accounts = db.Collection<AccountFb>();
    std::vector<AccountFb> accounts;

    std::vector<int> account_ids = db.Index<AccountByTrader>().Range(trader_id, trader_id);
    for (auto account_id : account_ids) {
        AccountFb account = accounts.Get<AccountFb>(account_id);
        accounts.insert(account.trader_id());
    }
    return accounts;
}


Db<Schema>, DbSnapshot<Schema>, DbGuard<Schema>, DbWriter<Schema>

// Db<Schema>::Collection<Account>

by_age_range
by_trader_id_range

class AccountBuf {
public:
    AccountBuf() : msg {GetDefultAccountFb()}
    {}

    static AccountBuf From(AccountFb* msg) {
        AccountBuf buf;
        buf.CopyFrom(msg);
    }

    int account_id() const {
        msg->account_id();
    }

    string_view name() const {
        msg->name();
    }

    void CopyFrom(AccountFb* msg) {
        buffer = BufferFrom(msg);
        msg = GetRoot(buffer);
    }

    std::vector<byte> buffer;
    AccountFb* msg;

    static const AccountFb* GetDefultAccountFb();
};


// // Strong types generated for the db named 'Name'
// class Schema {
// public:
//     using Db = Db;
//     using Snapshot = Snapshot;
//     using Writer = Writer;
//     using Guard = Guard;
// };

// Owns all the data. Must outlive Snapshot, Writer.
// Thread safe.
class Db {
public:
    Snapshot Snapshot() const;
    Guard Guard() const;

    ScopedWrite ScopedWrite();

private:
    std::unordered_map<int, bytes> accounts;  // collection of AccountFb.
    std::multimap</*std::string*/, bytes*> by_trader_id; // Index for accounts.
};

    // auto db_guard = db.guard();
    // for (auto [id, is_active] : db_guard.Account().ByTraderId().Eq(trader_id).Project<AccountFb::account_id, AccountFb::is_active>()) {
    //     if (!active) continue;
    //     count << id << is_active << std::endl;
    // }

struct Patch {
    bitset<uint64_t> updated_fields;
    std::vector<int8_t> buffer;
};

AccountFb::ApplyPatchInplace(Patch)

AccountFb::CanPatchInplace(patch) {
    // check that patch modifies only fixed-size fields.
    // Removal is conserning.

    updated_fields & buffer.presense_bit == updated_fields -> no unset.

    updated_fields & fixed-size fields bits == updated_fields; -> updates only for fixed size fields

    AccountFb::presense_bit & updated_fields = acc buffer has
}

High level patch API:
{
    // Does it support uncommitted read?
    auto writer = db.ScopedWrite();
    writer.Accounts().Update(account_id, patch);
    PositionView position;
    writer.Positions().Get(position_id, position);
    volume_patch.set_volume(position.volume() + 10.0);
    writer.Positions().Update(position_id, volume_patch);

    PositionView position_updated;
    writer.Positions().Get(position_id, position_updated);
    position_updated.volume(); // == ?
    position.volume(); // == ?  // the buffer might have been reallocated, and the position is invalidated.

    // Can we make it as fast as:
    //  auto it = position_map.find(position_id);
    //  it->second.volume += 10.0;

    int transaction_id = writer.CommitAsync();
    writer.CommitSync();

    // Rollback is much less frequently. We should optimize for optimistic scenario.
    // Update in place. Keep in memory rollback patches.
    // E.g. Patch [position_id, volume = 10.0] will become rollback patch [position_id, volume = prev_value];
    // rollback patches are applied backward in case of failure.
    // What about adding new records? Or deleting records?

    // But snapshot & regular reads should see only committed state?

    // So it can be multi level: tmp level collects uncommitted changes.

    // uncommitted page is visible only inside ScopedWrite.

    // Snapshot or Guard reads can't see the uncommitted page.
    // Uncommitted page can be always empty if there is no TLog to durable write to.

    // Collection uncommitted page - diffs and/or materialized objects
    // Index uncommitted page - ??

    // We should have and array of uncommitted pages
    // (while the bottom one has been fixed and sent to durable storage, the top one is available for write/reads).

    // If pages is failed to be writted. All changes in the page + all pages above are invalidated.

}

db.WaitDurable(transaction_id);

class Update {
    // string_view key;
    hint key_hint;

    bytes overwrite;
    byte_path update;

    // overwrite or update;
}


// In-place or overwrite update.
using UpdateFunc = absl::AnyInvocable<
    std::optional<std::vector<uint8_t>>(std::span<uint8_t>)
>;

struct UpdateEntry {
    int collection_id;
    std::string key;

    UpdateFunc func;
};

// With/without snapshotting
class KvBackend {
public:
    void BulkUpdates(const std::vector<UpdateEntry>& updates) {
        for (const auto& entry : updates) {
            std::vector<uint8_t>& buffer = GetBuffer(entry.collection_id, entry.key);
            std::optional<std::vector<uint8_t>> maybe_new = entry.func(std::span<uint8_t>(buffer));
            if (maybe_new) {
                buffer = std::move(*maybe_new);
            }
        }
    }

    // What can be a good DS for bulk updates?
    // Collection Id, key, value
    vector<pair</*collection id*/ int, /*key*/string, /*value*/string>>
    Update(string_view key, F&& updater, key_hint);
    Put
    Has
    Delete(key_hint)
    Snapshot() (if supported)
};

// Consistent repeatable reads. Non-locking. Zero-copy.
class Snapshot {
public:
    AccountSnapshotAccessor Accounts() const;
    PositionSnapshotAccessor Position() const;

    // account collection access by trader id.
    Iterator<AccountFb> ByTraderIdRange(string_view from, string_view to) const;
    Iterator<AccountFb> ByTraderIdEq(string_view value) const;

    // account collection access by by age.
    Iterator<AccountFb> ByAgeRange(int from, int to) const;
    Iterator<AccountFb> ByAgeEq(int from, int to) const;

};

class Guard {
public:
    AccountAccessor Accounts() const;
    PositionAccessor Positions() const;

private:
    DbContext db_context;
};

class internal::DbContext {
    const Db& db_;
    bool is_shared_locked = true;
    int snapshot_version = -1;

    int min_snapshot_version;
    int latest_committed_version;
    int latest_uncommitted_version;
};

class AccountAccessor {
    // PK operations.
    status Put(int account_id);
    status Get(int account_id, AccountFb&);
    status GetCopy(int account_id, AccountFub);
    status Delete(int account_id);

    // Full scan.
    Iterate() const;

    // Indices.
    ByTraderIdIndex ByTraderId() const;
    ByAgeIndex ByAge() const;

private:
    const Db& db_;
    bool is_shared_locked = true;
    int snapshot_version = -1;
};

class ByTraderIdIndex {
public:
    Iterator Range(string_view from, string_view to) const;
    Iterator Eq(string_view value) const;

    class Iterator {
        statusor<AccountFb> operator*() const;

        private:
        const Db& db_;
        bool is_shared_locked = true;
        int snapshot_version = -1;
    };

private:
    const Db& db_;
    bool is_shared_locked = true;
    int snapshot_version = -1;
};

struct DbVisibilityState {
    int min_snapshot_version;
    int max_snapshot_version;
    int latest_committed_version;
    int latest_uncommitted_version;
};

class Index {
    AddKey(Key, PrimaryKey, /*write_timestamp*/ version) {
        index.insert(key, primary_key, version);
    }
    DeleteKey(Key, PrimaryKey, /*write_timestamp*/ version) {
        it = index.upper_bound(Key, PrimaryKey)
        --it;
        ASSERT(version > it->version);
        _deletion_queue.push_back(pair<it, version>);
        MarkedDeleted(it); // as an optimization?
    }
    // UpdateKey(OldKey, NewKey, PrimaryKey, /*write_timestamp*/);

    // Some of the PrimaryKeys are known to be invalidated.
    // We need to check later? In iterator?
    // We can avoid storing creation version if we allow later checks for every message.
    // If we remove versions, we have to skip duplicates.
    auto LowerBound(Key, version);
    auto UpperBound(Key, version);

    void PurgDeletedVersions(DbVisibilityState, max_workload);

    // I need a bit which shows the record should be deleted.
    std::map<pair<Key, PrimaryKey>, /*creation version*/ int>> index;  
    // Only add/delete from the ends (to not invalidate iterators).
    std::deque<pair<map::iterator, /*deletion version*/ int, /*rolled back*/ bool> _deletion_queue;  // if rolled_back == true - no-op.
    std::deque<tuple<map::iterator, /*deletion_queue*/deque::iterator, /*uncommitted version*/ int> _rollback_queue;  // when we rollback we should also restore from _deletion_queue;
};



void UpdateValue(key, patch, DbVisibilityState, transaction_id) {
    // Patch can be inplace or will require a copy naturally.
    // If it requires copy, we can directly insert the copy to collection. as a new version.

    // If no snapshots min_snapshot_version == -1. We can update in-place.
    {
        auto it = collection.find(key, latest_version);
        if (it->second.version == transaction_id) {
            // Multiple updates in the same transaction.

            // We should build a cumulative inverted_patch and overwrite it in undo_list.
        } else {
            it->second.version = transaction_id;
            auto inverted_pacth = InvertPath(it->second.value);
            _undo_list.push_back(inverted_patch)
        }
        it->second.value.UpdateInplace(patch);
    }
    // else if older snapshots exist but it->second.version > max_snapshot_version
    {
        // So, it->second.version isn't visible for max_snapshot_version, so it's safe to update inplace.
        // See the previous block.
    }
    // else
    {
        // Snapshots exist, for which we will break the visibility. Do copy.
        auto it = collection.find(key, latest_version);
        collection.insert(key, it->second.value.UpdateWithCopy(patch));

        // put inverted patch to with "delete on rollback command".
    }
}

Value& ReadValue(key, DbVisibilityState, read_id) {
    auto it = collection.upper_bound(key, read_id);
    if (it.version > latest_committed_version) {
        // the value was updated not yet committed.
        if (read_id > latest_committed_version) {
            // read uncommitted state. OK.
            return it.value;
        } else if (read_id == latest_committed_version) {
            // We should take it.version and apply inverted path from undo list, and store the value with version latest_committed_version.
            auto patch = _undo_maps[it.version % 2].find(key)
            // WARNING: here we do writes in read method. It should be rare. Can we upgrade mutex for writes?
            auto cit = collection.insert(key, it.value, latest_committed_version);  
            cit.value.ApplyPatch(path);
            _gc_lsit.insert(cit, it.version /*when the record became invalid*/);

            // copy, put into the GC list and return.
            // TODO: undo list should support look up by key.
        }
    } else {
        // version <= latest_committed_version.
        return value;
    }
}

void OnCommit(latest_committed_version) {
    // Get write lock.
    _undo_maps[latest_committed_version % 2].clear();
}

void OnRollback(rollback_version) {
    // unpatch everything from latest_committed_version % 2

    // If copy was not made for the value -> there were no snapshots looking at latest_comitted_version.
    // Thus, we can restore the previous version with version = `rollback_version - 1` (it will not hurt visibility)
}

std::deque<tuple<it, version>> _gc_list;
std::array<UndoMap<Key>, 2> _undo_maps;  // We support two uncommitted "transaction batches". One being written, another being populating.


//What if we don't have undos (e.g. no TL)?
```