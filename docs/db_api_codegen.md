## Basic concepts of code generation

### Table definition
Table fields are described as a FlatBuffer (FB) message:
```
table AccountFb {
  account_id: int;
  trader_id: string;
  name: string;
  address: string;
  age: int;
  is_active: bool;
  balance: float;
}
```

FB will generate `class AccountFb {}` - an not owning class which has copy-free get methods for the message's byte buffer.

Additionally, AccountBuf will be generated - a wrapper around AccountFb which owns it's own memory buffer.

```cpp
class AccountBuf {
public:
  static AccountBuf From(AccountFb); // Copies to _buf.

  // Getters for convenience.
  int account_id() const;
  string_view name() const;

  const AccountFb* Fb() const;

private:
  vector<byte> _buf;
  AccountFb* _msg; // points to _buf or to a default message.
}
```

### Value partial updates
To support partial updates, the `UpdateFb` table is used:
```
table UpdateFb {
  value: [ubyte];       // Serialized FlatBuffer of the partial object, type-erased.
  updated_fields: [int];  // Specifies, which fields should be updated.
}
```

E.g.
```json
{
  value: /* Serialized AccountFb {account_id: 100, age: 10} */,
  updated_fields: [/*age_field_num*/]
}
```

will set the age to 10 for the account with id=100. The primary key presence is required, primary keys will be introduced later in the doc.

The next methods will be generated to support partially updates:
```cpp
void ApplyAccountUpdate(UpdateFb update, span<const byte> src, bytes& dst);
void ApplyAccountUpdateInplace(UpdateFb update, span<byte>& src_dst); // Only if the update doesn't change buffer layout. E.g. primitive type updates.
```

### Database schema definition
The database schema is described using JSON markup:
```json
tables: [{
  name: "accounts",
  type: “Account”
  primary_key: [“account_id”]
  indices: [
    { name: "ByAge", fields: [ "age" ], kind: BTREE },
    { name: "ByTraderId", fields: [ "trader_id" ], kind: BTREE },
  ]
}]
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
  Iterator<AccountFb> ByAgeRange(int from, int to) const;
  Iterator<AccountFb> ByAgeEq(int value) const;
};
```


Including get, insert, update type-safe methods:
```cpp
  Db db;
  // ...
  {
    auto guard = db.Guard();
    AccountFb acc;
    guard.Accounts().Get(104, acc);
    // acc is copy-free reference to the db record.
  }

  AccountBuf acc_buf;
  db.Guard().Accounts().Get(104, acc_buf); // lock is immediately released.
  // acc_buf - is a copy of the db record.
  // Copy contains of (possible) 1 allocation and 1 memcpy call.

  AccountFb acc_dangling;
  db.Guard().Accounts().Get(104, acc_dangling); // Error: possible dangling reference after the lock is released.

  {
    // Zero-copy iteration on the consistent view of Db using an index.
    Snapshot snap;
    for (const AccountFb acc : snap.Accounts().ByTraderId().Eq("aa")) {
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

AccountBuf acc;
// .. build the acc record ...
db.ScopedWrite().Accounts().Insert(acc.Fb()); // The change is immediately committed.

{
  auto writer = db.ScopedWrite();
  writer.Accounts().Insert(account2_example);
  writer.Accounts().Update(account2_update);
  AccountFb account1;
  writer.Accounts().Get(/*account_id=*/ 1, account1);
  for (const AccountFb acc : writer.Accounts().ByTraderId().Eq("aa")) {
    // comprehensive reads are supported in write context.
  }
  writer.Accounts().Delete(/*account_id=*/ 1);
  writer.Commit();  // All changes will be atomically applied. All relevant indices are updated.
} 

db.ScopedWrite().Accounts().UpdateField(/*account_id=*/ 1, &AccountFb::balance, 200.0); // Single field update.
```

### Read queries
Combining read primitives from above the next read patterns can be written for Snapshot or Guard:

```sql
SELECT * FROM accounts WHERE trader_id = {trader_id}
```

The generated read function:
```cpp
template <template DbAccessor>
void GetAccountsByTraderId(const DbAccessor& db, string_view trader_id, std::vector<AccountFb>& accounts) const {
  for (auto AccountFb acc : db.Accounts().ByTrader().Eq(trader_id)) {
    // no deserialization, no copies, no allocations (except the vector growth)
    accounts.push_back(acc);
  }
}
```

Note the return type, AccountFb, doesn’t own the message memory. It’s cheap to use as a return value, however the caller has to be sure that Snapshot or Guard will outlive the `accounts` vector.

Another query example:
```sql
SELECT account_id FROM accounts WHERE is_active = TRUE AND age BETWEEN {min_age} AND {max_age}
```

The generated read function will be:
```cpp
template <typename DbAccessor>
void GetActiveAccountIdsInAgeRange(const DbAccessor& db, int min_age, int max_age, std::vector<int>& account_ids) const {
  for (AccountFb acc : db.Accounts().ByAge().Range(min_age, max_age)) {
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

  for (AccountFb acc : db.Accounts().Iterate()) { // Cache friendly iteration through all records.
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
  for (AccountFb acc : db.Accounts().ByCurrency().Iterate()) { // The records are sorted by Currency (BTree).
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
    vector<pair<int, ConfigFb>>& result) 
{
    for (AccountFb acc : db.Accounts().ByTraderId().Eq(trader_id)) {
      ConfigFb config;
      if (!db.Configs().Get(acc.config_name(), config)) continue;
      results.push_back({acc.account_id(), config});
    }
}
```