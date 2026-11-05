## Basic concepts of code generation

This document is the original design of the generated database API. Message classes, collections,
secondary indices and sequences work as described; for transactions and snapshots as implemented,
see [storage_engine.md](storage_engine.md). The accessor style in the sections below
(`db.Accounts().Get`) is a sketch that is not implemented, and `Guard` was replaced by `Snapshot`.

### Table definition
Table fields are described in a FlatBuffers schema (`.fbs`). flatc only parses the schema: the
stored bytes use the [GenDB message format](message_format.md), not FlatBuffers.
```
table Account {
  account_id: ulong;
  trader_id: string;
  name: string;
  address: string;
  age: int;
  is_active: bool;
  balance: float;
}
```

Three classes are generated per table:

- `Account`: a non-owning, zero-copy view with `has_*()` and getter methods.
- `AccountBuilder`: serializes a new message into `std::vector<uint8_t>`.
- `AccountPatchBuilder`: builds a `gendb::MessagePatch`, a partial update.

### Value partial updates
A patch sets or clears individual fields of a stored message:

```cpp
auto patch = AccountPatchBuilder().set_age(10).clear_name().Build();
writer.UpdateAccount(/*account_id=*/100, patch);
```

`gendb::ApplyPatch` overwrites bytes in place when every change keeps the field's storage class and
length, and rebuilds the message otherwise. Fields unknown to this schema are preserved either way.

### Database schema definition
The database schema is described in YAML, as in `tests/schemas/db.yaml`:
```yaml
collections:
  - name: accounts
    type: gendb.tests.Account
    primary_key:
      - account_id

indices:
  - name: account_by_age
    collection: accounts
    fields:
      - age
```
The database might contain multiple tables.

The table and its indices will be code generated:
```cpp
class Db {
public:
  Snapshot Snapshot() const; // Consistent view on the db. Without blocking the writes.
  Guard Guard() const;  // Latest consistent view on the db, guarded by read mutex.

  ScopedWrite ScopedWrite() const;  // Provides exclusive write API. For serializable writes.

private:
  // Data structures which actually holds the data.
  hash_map</*account_id*/ int, bytes>             accounts;
  btree</*age*/ int, /*account_id*/ int>          by_age;
  btree</*trader_id*/ string, /*account_id*/ int> by_trader_id;
};

// Provides zero-copy read methods. Including bulk read and index scan.
class Snapshot {
  auto Accounts() const;  // Thin wrappers to scope access objects related to the account collection.
  ~Snapshot(); // Will free the old versions held for the snapshot.
};

// The same API as Snapshot. Only the access mechanism differs: snapshot is non-blocking. Guard is blocking.
class Guard {
  auto Accounts() const;
  ~Guard(); // Will free the lock.
};

// Thin wrapper over index functionality.
class ByAgeIndex {
public:
  Iterator<Account> ByAgeRange(int from, int to) const;
  Iterator<Account> ByAgeEq(int value) const;
};
```


Including get, insert, update type-safe methods:
```cpp
  Db db;
  // ...
  {
    auto guard = db.Guard();
    Account acc;
    guard.Accounts().Get(104, acc);
    // acc is copy-free reference to the db record.
  }

  std::vector<uint8_t> acc_copy;
  db.Guard().Accounts().Get(104, acc_copy); // lock is immediately released.
  // acc_copy - is a copy of the db record.
  // Copy contains of (possible) 1 allocation and 1 memcpy call.

  Account acc_dangling;
  db.Guard().Accounts().Get(104, acc_dangling); // Error: possible dangling reference after the lock is released.

  {
    // Zero-copy iteration on the consistent view of Db using an index.
    Snapshot snap;
    for (const Account acc : snap.Accounts().ByTraderId().Eq("aa")) {
      std::cout << acc.account_id() << endl;
    }
  }
```

```cpp
// ScopedWrites accumulates the write changes in tmp buffer.
// The write buffer is applied transitionally on Commit().
// The Db API ensures that at most one ScopedWrite object exists at a time.
class ScopedWrite {
  auto Account(); // Thin wrapper which holds all write methods related to accounts.
}

// Usage example:

std::vector<uint8_t> acc;
// .. build the acc record ...
db.ScopedWrite().Accounts().Insert(acc); // The change is immediately committed.

{
  auto writer = db.ScopedWrite();
  writer.Accounts().Insert(account2_example);
  writer.Accounts().Update(account2_update);
  Account account1;
  writer.Accounts().Get(/*account_id=*/ 1, account1);
  for (const Account acc : writer.Accounts().ByTraderId().Eq("aa")) {
    // comprehensive reads are supported in write context.
  }
  writer.Accounts().Delete(/*account_id=*/ 1);
  writer.Commit();  // All changes will be atomically applied. All relevant indices are updated.
} 

db.ScopedWrite().Accounts().UpdateField(/*account_id=*/ 1, &Account::balance, 200.0); // Single field update.
```

### Read queries
Combining read primitives from above the next read patterns can be written for Snapshot or Guard:

```sql
SELECT * FROM accounts WHERE trader_id = {trader_id}
```

The generated read function:
```cpp
template <template DbAccessor>
void GetAccountsByTraderId(const DbAccessor& db, string_view trader_id, std::vector<Account>& accounts) const {
  for (auto Account acc : db.Accounts().ByTrader().Eq(trader_id)) {
    // no deserialization, no copies, no allocations (except the vector growth)
    accounts.push_back(acc);
  }
}
```

Note the return type, Account, doesn’t own the message memory. It’s cheap to use as a return value, however the caller has to be sure that Snapshot or Guard will outlive the `accounts` vector.

Another query example:
```sql
SELECT account_id FROM accounts WHERE is_active = TRUE AND age BETWEEN {min_age} AND {max_age}
```

The generated read function will be:
```cpp
template <typename DbAccessor>
void GetActiveAccountIdsInAgeRange(const DbAccessor& db, int min_age, int max_age, std::vector<int>& account_ids) const {
  for (Account acc : db.Accounts().ByAge().Range(min_age, max_age)) {
    if (acc.is_active()) account_ids.push_back(acc.account_id());
  }
}
```

Note, that we don’t need to copy nor return the whole value.

    
### More complex queries
 
```sql
SELECT currency, SUM(balance) FROM accounts WHERE active = TRUE GROUP BY currency
```
    
```cpp
template <typename DbAccessor>
unordered_map<string, float> SumByCurrency(const DbAccessor& db, ) {
  unordered_map<string, float> total_by_currency;

  for (Account acc : db.Accounts().Iterate()) { // Cache friendly iteration through all records.
      if (!acc.is_active()) continue;
      total_by_currency[acc.currency()] += acc.balance();
  }

  return total_by_currency;
}
```

#### In case the ByCurrency index exists.

```cpp
template <typename DbAccessor>
vector<pair<string, float>> SumByCurrency(const DbAccessor& db, ) {
  vector<string, float> total_by_currency;

  string_view current_currency = "";
  for (Account acc : db.Accounts().ByCurrency().Iterate()) { // The records are sorted by Currency (BTree).
    if (!acc.is_active()) continue;
    if (acc.currency().empty()) continue;

    if (current_currency != acc.currency()) {
      total_by_currency.emplace_back(acc.currency(), 0.0);
      current_currency = acc.currency();
    }

    total_by_currency.back().second += acc.balance();
  }
  return total_by_currency;
}
```


#### Join example

```sql
SELECT account_id, configs.* FROM accounts INNER JOIN configs ON config_name WHERE trader_id = {trader_id}
```

```cpp
template <typename DbAccessor>
void GetAccountsWithConfigByTrader(
    const DbAccessor& db,
    const string& trader_id, 
    vector<pair<int, Config>>& result) 
{
    for (Account acc : db.Accounts().ByTraderId().Eq(trader_id)) {
      Config config;
      if (!db.Configs().Get(acc.config_name(), config)) continue;
      results.push_back({acc.account_id(), config});
    }
}
```