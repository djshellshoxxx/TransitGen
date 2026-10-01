#include "Json.h"

#include <cstdio>

namespace tg::json {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    Parser(std::string_view t, std::string& err) : text_(t), err_(err) {}

    bool document(Value& out)
    {
        skipWs();
        if (!value(out, 0)) return false;
        skipWs();
        if (pos_ != text_.size()) return fail("unexpected trailing characters");
        return true;
    }

private:
    bool fail(const char* what)
    {
        int line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') { ++line; col = 1; } else ++col;
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "line %d, column %d: %s", line, col, what);
        err_ = buf;
        return false;
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipWs()
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word) return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool value(Value& v, int depth)
    {
        if (depth > kMaxDepth) return fail("nesting too deep");
        switch (peek()) {
            case '{': return object(v, depth);
            case '[': return array(v, depth);
            case '"': v.type = Value::Type::String; return string(v.str);
            case 't': v.type = Value::Type::Bool; v.b = true; return literal("true");
            case 'f': v.type = Value::Type::Bool; v.b = false; return literal("false");
            case 'n': v.type = Value::Type::Null; return literal("null");
            case '\0': return fail("unexpected end of input");
            default: return number(v);
        }
    }

    bool object(Value& v, int depth)
    {
        v.type = Value::Type::Object;
        ++pos_;
        skipWs();
        if (peek() == '}') { ++pos_; return true; }
        for (;;) {
            skipWs();
            if (peek() != '"') return fail("expected a string key");
            std::string key;
            if (!string(key)) return false;
            for (const auto& kv : v.obj) if (kv.first == key) return fail(("duplicate key \"" + key + "\"").c_str());
            skipWs();
            if (peek() != ':') return fail("expected ':'");
            ++pos_;
            skipWs();
            v.obj.emplace_back(std::move(key), Value{});
            if (!value(v.obj.back().second, depth + 1)) return false;
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool array(Value& v, int depth)
    {
        v.type = Value::Type::Array;
        ++pos_;
        skipWs();
        if (peek() == ']') { ++pos_; return true; }
        for (;;) {
            skipWs();
            v.arr.emplace_back();
            if (!value(v.arr.back(), depth + 1)) return false;
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    static int hexVal(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(uint32_t& cp)
    {
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = hexVal(peek());
            if (h < 0) return fail("invalid \\u escape");
            cp = (cp << 4) | static_cast<uint32_t>(h);
            ++pos_;
        }
        return true;
    }

    static void appendUtf8(std::string& s, uint32_t cp)
    {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out)
    {
        ++pos_;   // opening quote
        for (;;) {
            if (atEnd()) return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') { out += c; continue; }
            const char esc = peek();
            ++pos_;
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        uint32_t lo;
                        if (peek() != '\\' || (pos_ + 1 < text_.size() && text_[pos_ + 1] != 'u')) return fail("unpaired surrogate");
                        pos_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo >= 0xE000) return fail("unpaired surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp < 0xE000) {
                        return fail("unpaired surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape");
            }
        }
    }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    bool number(Value& v)
    {
        v.type = Value::Type::Number;
        bool neg = false;
        if (peek() == '-') { neg = true; ++pos_; }
        if (!isDigit(peek())) return fail("invalid value");
        uint64_t mant = 0;
        int sig = 0, exp10 = 0;
        auto addDigit = [&](char c, bool fraction) {
            if (sig == 0 && c == '0') { if (fraction) --exp10; return; }   // leading zeros are not significant
            if (sig < 19) {
                mant = mant * 10 + static_cast<uint64_t>(c - '0');
                ++sig;
                if (fraction) --exp10;
            } else if (!fraction) {
                ++exp10;   // dropped integer digit
            }
        };
        if (peek() == '0') {
            ++pos_;
            if (isDigit(peek())) return fail("leading zeros are not allowed");
        } else {
            while (isDigit(peek())) addDigit(text_[pos_++], false);
        }
        bool integer = true;
        if (peek() == '.') {
            integer = false;
            ++pos_;
            if (!isDigit(peek())) return fail("expected a digit after '.'");
            while (isDigit(peek())) addDigit(text_[pos_++], true);
        }
        if (peek() == 'e' || peek() == 'E') {
            integer = false;
            ++pos_;
            bool eneg = false;
            if (peek() == '+' || peek() == '-') { eneg = peek() == '-'; ++pos_; }
            if (!isDigit(peek())) return fail("expected a digit in the exponent");
            int e = 0;
            while (isDigit(peek())) { if (e < 100000) e = e * 10 + (text_[pos_] - '0'); ++pos_; }
            exp10 += eneg ? -e : e;
        }
        v.num = decimalToDouble(neg, mant, exp10);
        v.isInteger = integer;
        return true;
    }

    std::string_view text_;
    std::string&     err_;
    size_t           pos_ = 0;
};

constexpr double kPow10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                               1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

} // namespace

double decimalToDouble(bool negative, uint64_t mantissa, int exp10) noexcept
{
    double d = 0.0;
    if (mantissa != 0) {
        d = static_cast<double>(mantissa);   // exact when mantissa <= 2^53
        if (exp10 > 330) d = 1e308 * 10.0;
        else if (exp10 < -360) d = 0.0;
        else if (exp10 >= 0) {
            while (exp10 > 22) { d *= kPow10[22]; exp10 -= 22; }
            d *= kPow10[exp10];
        } else {
            while (exp10 < -22) { d /= kPow10[22]; exp10 += 22; }
            d /= kPow10[-exp10];
        }
    }
    return negative ? -d : d;
}

bool parse(std::string_view text, Value& out, std::string& error)
{
    out = Value{};
    Parser p(text, error);
    return p.document(out);
}

} // namespace tg::json
