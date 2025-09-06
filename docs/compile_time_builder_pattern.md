## Temporary code snippets demonstrating the ability for CompileTime builder optimizations.

```cpp
#include <array>
#include <optional>
#include <string>
#include <cstring>
#include <iostream>

// ------------------- field enum -------------------
enum class Field : int { Id=0, Name=1, Email=2 };

// ------------------- compile-time sort -------------------
// small constexpr bubble sort for fields
template<std::size_t N>
constexpr std::array<Field, N> sort_fields(std::array<Field, N> arr) {
    for (std::size_t i = 0; i < N; i++) {
        for (std::size_t j = 0; j + 1 < N; j++) {
            if ((int)arr[j] > (int)arr[j+1]) {
                auto tmp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = tmp;
            }
        }
    }
    return arr;
}

template<Field... Fs>
struct field_list {
    static constexpr std::array<Field, sizeof...(Fs)> values = { Fs... };
    static constexpr auto sorted() {
        return sort_fields(values);
    }
};

// ------------------- builder -------------------
template<Field... Fs>
struct AccountBuilder {
    int id;
    std::string_view name;
    std::string_view email;
private:
    // Only default constructible (for AccountBuilder<>())
    AccountBuilder() = default;

    // Disallow copy and move (ephemeral only)
    AccountBuilder(const AccountBuilder&) = delete;
    AccountBuilder(AccountBuilder&&) = default;
    AccountBuilder& operator=(const AccountBuilder&) = delete;
    AccountBuilder& operator=(AccountBuilder&&) = delete;

    // Allow conversions from other AccountBuilder<> (internal use only)
    template<Field... OtherFs>
    AccountBuilder(AccountBuilder<OtherFs...>&& other)
        : id(other.id), name(other.name), email(other.email) {}

    // Friend factory so user can start the chain
    template<Field...> friend struct AccountBuilder;

    // "construct from another builder"
    // AccountBuilder() = default;
    // AccountBuilder(const AccountBuilder&) = default;

//    template<Field... OtherFs>
//    AccountBuilder(AccountBuilder<OtherFs...>&& other)
//        : id(std::move(other.id)), name(std::move(other.name)), email(std::move(other.email)) {}
public:
    // setters
    auto set_id(int v) && {
        AccountBuilder<Fs..., Field::Id> copy = std::move(*this);
        copy.id = v;
        return copy;
    }

    auto set_name(std::string_view v) && {
        AccountBuilder<Fs..., Field::Name> copy = std::move(*this);
        copy.name = std::move(v);
        return copy;
    }

    auto set_email(std::string_view v) && {
        AccountBuilder<Fs..., Field::Email> copy = std::move(*this);
        copy.email = std::move(v);
        return copy;
    }

    // serialization target
    struct Message {
        std::array<char, 256> buffer{};
        std::size_t size{};
    };

    // Factory entry point
    static auto Create() {
        return AccountBuilder<>();
    }

    Message Build() const {
        // required field check
        constexpr bool hasId   = ((Fs == Field::Id)   || ...);
        constexpr bool hasName = ((Fs == Field::Name) || ...);
        static_assert(hasId && hasName, "Missing required field(s)");

        // compile-time sorted field order
        constexpr auto sorted = field_list<Fs...>::sorted();

        Message msg{};
        char* p = msg.buffer.data();

        for (auto f : sorted) {
            switch(f) {
            case Field::Id: {
                std::memcpy(p, &id, sizeof(id));
                p += sizeof(id);
                break;
            }
            case Field::Name: {
                std::memcpy(p, name.data(), name.size());
                p += name.size();
                break;
            }
            case Field::Email: {
                std::memcpy(p, email.data(), email.size());
                p += email.size();
                break;
            }
            }
        }
        msg.size = p - msg.buffer.data();
        return msg;
    }
};

// ------------------- usage -------------------
int main() {
    std::string name;
    std::cin >> name;
    auto msg = AccountBuilder<>::Create()
                 .set_name(name)
                 .set_id(42)
                 .set_email("bob@example.com")
                 .Build();

    std::cout << "Serialized size = " << msg.size << "\n";
    for (std::size_t i = 0; i < msg.size; i++) {
        std::cout << (int)(unsigned char)msg.buffer[i] << " ";
    }
    std::cout << "\n";
}
```