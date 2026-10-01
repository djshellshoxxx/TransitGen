// TransitGen core — a small self-contained JSON reader (message thread only; allocates).
// Strict RFC 8259 subset: no comments, no trailing commas, UTF-8 passed through.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tg::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type   type = Type::Null;
    bool   b = false;
    double num = 0.0;
    bool   isInteger = false;      // written without fraction or exponent
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;   // in document order

    const Value* get(std::string_view key) const noexcept
    {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

/// Parses `text` into `out`. On failure returns false and sets `error` ("line L, column C: ...").
bool parse(std::string_view text, Value& out, std::string& error);

/// Correctly rounded for up to 19 significant digits with |exp10| <= 22 (Clinger's fast path);
/// otherwise a deterministic, portable approximation. Never depends on the C locale.
double decimalToDouble(bool negative, uint64_t mantissa, int exp10) noexcept;

} // namespace tg::json
