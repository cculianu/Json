/*
Json - A lightweight JSON parser and serializer for Qt.
Copyright (c) 2020 Calin A. Culianu <calin.culianu@gmail.com>

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include "Json.h"

#include <QMetaType>
#include <QtDebug>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#ifdef __clang__
// turn off the dreaded "warning: class padded with xx bytes, etc" since we aren't writing wire protocols using structs..
#pragma clang diagnostic ignored "-Wpadded"
#endif
// EXPECT, LIKELY, and UNLIKELY
#if defined(__clang__) || defined(__GNUC__)
#define EXPECT(expr, constant) __builtin_expect(expr, constant)
#else
#define EXPECT(expr, constant) (expr)
#endif

#define LIKELY(bool_expr)   EXPECT(bool(bool_expr), 1)
#define UNLIKELY(bool_expr) EXPECT(bool(bool_expr), 0)

namespace {

enum jtokentype {
    JTOK_ERR        = -1,
    JTOK_NONE       =  0,                           // eof
    JTOK_OBJ_OPEN,
    JTOK_OBJ_CLOSE,
    JTOK_ARR_OPEN,
    JTOK_ARR_CLOSE,
    JTOK_COLON,
    JTOK_COMMA,
    JTOK_KW_NULL,
    JTOK_KW_TRUE,
    JTOK_KW_FALSE,
    JTOK_NUMBER,
    JTOK_STRING,
};

inline bool jsonTokenIsValue(jtokentype jtt) noexcept {
    switch (jtt) {
    case JTOK_KW_NULL:
    case JTOK_KW_TRUE:
    case JTOK_KW_FALSE:
    case JTOK_NUMBER:
    case JTOK_STRING:
        return true;

    default:
        return false;
    }

    // not reached
}

inline bool json_isspace(uint8_t ch) noexcept {
    switch (ch) {
    case 0x20:
    case 0x09:
    case 0x0a:
    case 0x0d:
        return true;

    default:
        return false;
    }

    // not reached
}

/*
 * According to stackexchange, the original json test suite wanted
 * to limit depth to 22.  Widely-deployed PHP bails at depth 512,
 * so we will follow PHP's lead, which should be more than sufficient
 * (further stackexchange comments indicate depth > 32 rarely occurs).
 */
inline constexpr size_t MAX_JSON_DEPTH = 512;

inline bool json_isdigit(uint8_t ch) noexcept { return ch >= '0' && ch <= '9'; }

// convert hexadecimal (big endian) string to unsigned integer (machine byte orer)
const char *hextouint(const char *first, const char *last, unsigned &out) noexcept
{
    unsigned result = 0;
    for (; first < last; ++first)
    {
        int digit;
        if (json_isdigit(*first))
            digit = *first - '0';

        else if (*first >= 'a' && *first <= 'f')
            digit = *first - 'a' + 10;

        else if (*first >= 'A' && *first <= 'F')
            digit = *first - 'A' + 10;

        else
            break;

        result = 16 * result + digit;
    }
    out = result;

    return first;
}

/**
 * Filter that generates and validates UTF-8, as well as collates UTF-16
 * surrogate pairs as specified in RFC4627.
 */
class JSONUTF8StringFilter
{
public:
    explicit JSONUTF8StringFilter(QByteArray &s) noexcept
        : str(s), is_valid(true), codepoint(0), state(0), surpair(0)
    {}
    // Write single 8-bit char (may be part of UTF-8 sequence)
    void push_back(unsigned char ch)
    {
        if (state == 0) {
            if (ch < 0x80) // 7-bit ASCII, fast direct pass-through
                str.push_back(ch);
            else if (ch < 0xc0) // Mid-sequence character, invalid in this state
                is_valid = false;
            else if (ch < 0xe0) { // Start of 2-byte sequence
                codepoint = (ch & 0x1f) << 6;
                state = 6;
            } else if (ch < 0xf0) { // Start of 3-byte sequence
                codepoint = (ch & 0x0f) << 12;
                state = 12;
            } else if (ch < 0xf8) { // Start of 4-byte sequence
                codepoint = (ch & 0x07) << 18;
                state = 18;
            } else // Reserved, invalid
                is_valid = false;
        } else {
            if ((ch & 0xc0) != 0x80) // Not a continuation, invalid
                is_valid = false;
            state -= 6;
            codepoint |= (ch & 0x3f) << state;
            if (state == 0)
                push_back_u(codepoint);
        }
    }
    // Write codepoint directly, possibly collating surrogate pairs
    void push_back_u(unsigned int codepoint_)
    {
        if (state) // Only accept full codepoints in open state
            is_valid = false;
        if (codepoint_ >= 0xD800 && codepoint_ < 0xDC00) { // First half of surrogate pair
            if (surpair) // Two subsequent surrogate pair openers - fail
                is_valid = false;
            else
                surpair = codepoint_;
        } else if (codepoint_ >= 0xDC00 && codepoint_ < 0xE000) { // Second half of surrogate pair
            if (surpair) { // Open surrogate pair, expect second half
                // Compute code point from UTF-16 surrogate pair
                append_codepoint(0x10000 | ((surpair - 0xD800)<<10) | (codepoint_ - 0xDC00));
                surpair = 0;
            } else // Second half doesn't follow a first half - fail
                is_valid = false;
        } else {
            if (surpair) // First half of surrogate pair not followed by second - fail
                is_valid = false;
            else
                append_codepoint(codepoint_);
        }
    }
    // Check that we're in a state where the string can be ended
    // No open sequences, no open surrogate pairs, etc
    bool finalize() noexcept
    {
        if (state || surpair)
            is_valid = false;
        return is_valid;
    }
private:
    QByteArray &str;
    bool is_valid;
    // Current UTF-8 decoding state
    unsigned int codepoint;
    int state; // Top bit to be filled in for next UTF-8 byte, or 0

    // Keep track of the following state to handle the following section of
    // RFC4627:
    //
    //    To escape an extended character that is not in the Basic Multilingual
    //    Plane, the character is represented as a twelve-character sequence,
    //    encoding the UTF-16 surrogate pair.  So, for example, a string
    //    containing only the G clef character (U+1D11E) may be represented as
    //    "\uD834\uDD1E".
    //
    //  Two subsequent \u.... may have to be replaced with one actual codepoint.
    unsigned int surpair; // First half of open UTF-16 surrogate pair, or 0

    void append_codepoint(unsigned int codepoint_)
    {
        if (codepoint_ <= 0x7f)
            str.push_back(char(codepoint_));
        else if (codepoint_ <= 0x7FF) {
            str.push_back(char(0xC0 | (codepoint_ >> 6)));
            str.push_back(char(0x80 | (codepoint_ & 0x3F)));
        } else if (codepoint_ <= 0xFFFF) {
            str.push_back(char(0xE0 | (codepoint_ >> 12)));
            str.push_back(char(0x80 | ((codepoint_ >> 6) & 0x3F)));
            str.push_back(char(0x80 | (codepoint_ & 0x3F)));
        } else if (codepoint_ <= 0x1FFFFF) {
            str.push_back(char(0xF0 | (codepoint_ >> 18)));
            str.push_back(char(0x80 | ((codepoint_ >> 12) & 0x3F)));
            str.push_back(char(0x80 | ((codepoint_ >> 6) & 0x3F)));
            str.push_back(char(0x80 | (codepoint_ & 0x3F)));
        }
    }
};

jtokentype getJsonToken(QByteArray &tokenVal, unsigned &consumed, const char *raw, const char *end)
{
    tokenVal.clear();
    consumed = 0;

    const char *rawStart = raw;

    while (raw < end && (json_isspace(*raw)))          // skip whitespace
        ++raw;

    if (raw >= end)
        return JTOK_NONE;

    switch (*raw) {

    case '{':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_OBJ_OPEN;
    case '}':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_OBJ_CLOSE;
    case '[':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_ARR_OPEN;
    case ']':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_ARR_CLOSE;

    case ':':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_COLON;
    case ',':
        ++raw;
        consumed = (raw - rawStart);
        return JTOK_COMMA;

    case 'n':
    case 't':
    case 'f':
        if (0 == std::strncmp(raw, "null", 4)) {
            raw += 4;
            consumed = (raw - rawStart);
            return JTOK_KW_NULL;
        } else if (0 == std::strncmp(raw, "true", 4)) {
            raw += 4;
            consumed = (raw - rawStart);
            return JTOK_KW_TRUE;
        } else if (0 == std::strncmp(raw, "false", 5)) {
            raw += 5;
            consumed = (raw - rawStart);
            return JTOK_KW_FALSE;
        } else
            return JTOK_ERR;

    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        // part 1: int
        const char * const first = raw;
        const bool firstIsMinus = *first == '-';

        const char * const firstDigit = first + firstIsMinus; // if first == '-', firstDigit = first + 1, else firstDigit = first

        if (UNLIKELY(*firstDigit == '0' && firstDigit + 1 < end && json_isdigit(firstDigit[1])))
            return JTOK_ERR;

        ++raw;                                  // consume first char

        if (UNLIKELY(firstIsMinus && (raw >= end || !json_isdigit(*raw)))) // fail if buffer ends in '-', or matches '-[^0-9]'
            return JTOK_ERR;

        while (raw < end && json_isdigit(*raw)) {  // consume digits
            ++raw;
        }

        // part 2: frac
        if (raw < end && *raw == '.') {
            ++raw;                              // consume .

            if (UNLIKELY(raw >= end || !json_isdigit(*raw)))
                return JTOK_ERR;
            while (raw < end && json_isdigit(*raw)) { // consume digits
                ++raw;
            }
        }

        // part 3: exp
        if (raw < end && (*raw == 'e' || *raw == 'E')) {
            ++raw;                              // consume E

            if (raw < end && (*raw == '-' || *raw == '+')) { // consume +/-
                ++raw;
            }

            if (UNLIKELY(raw >= end || !json_isdigit(*raw)))
                return JTOK_ERR;
            while (raw < end && json_isdigit(*raw)) { // consume digits
                ++raw;
            }
        }
        tokenVal.append(first, raw - first);  // assign chars to output buffer (which is pre-cleared)
        consumed = raw - rawStart;
        return JTOK_NUMBER;
        }

    case '"': {
        constexpr int reserveSize = 0; // set to 0 to not pre-alloc anything
        ++raw;                                // skip "

        QByteArray valStr;
        if constexpr (reserveSize > 0)
            valStr.reserve(reserveSize);
        JSONUTF8StringFilter writer(valStr);

        while (true) {
            if (UNLIKELY(raw >= end || uint8_t(*raw) < 0x20))
                return JTOK_ERR;

            else if (*raw == '\\') {
                ++raw;                        // skip backslash

                if (UNLIKELY(raw >= end))
                    return JTOK_ERR;

                switch (*raw) {
                case '"':  writer.push_back('"'); break;
                case '\\': writer.push_back('\\'); break;
                case '/':  writer.push_back('/'); break;
                case 'b':  writer.push_back('\b'); break;
                case 'f':  writer.push_back('\f'); break;
                case 'n':  writer.push_back('\n'); break;
                case 'r':  writer.push_back('\r'); break;
                case 't':  writer.push_back('\t'); break;

                case 'u': {
                    unsigned int codepoint;
                    if (raw + 1 + 4 >= end || hextouint(raw + 1, raw + 1 + 4, codepoint) != raw + 1 + 4)
                        return JTOK_ERR;
                    writer.push_back_u(codepoint);
                    raw += 4;
                    break;
                    }
                default:
                    return JTOK_ERR;

                }

                ++raw;                        // skip esc'd char
            }

            else if (*raw == '"') {
                ++raw;                        // skip "
                break;                        // stop scanning
            }

            else {
                writer.push_back(*raw);
                ++raw;
            }
        }

        if (UNLIKELY(!writer.finalize()))
            return JTOK_ERR;

        if constexpr (reserveSize > 0)
            valStr.squeeze();
        tokenVal = std::move(valStr);
        consumed = raw - rawStart;
        return JTOK_STRING;
        }

    default:
        return JTOK_ERR;
    }
}

struct Container {
    enum Typ {
        Null, BoolFalse, BoolTrue, Num, Str, Arr, Obj
    };
    Typ typ = Null;
    // Note: I tried using a union class here to conserve memory but that was actually 10-20% slower
    // than simply doing this, and had lots of boilerplate for copy/move c'tor and copy/move assign, so
    // we just go with this.  This consumes ~48 bytes of extra memory on avg. per parsed json value;
    // but since this data structure is ephemeral and only used during parsing, that's acceptable since
    // the memory will be freed once parsing finishes.  It would only be a problem if we anticipated
    // parsing json that ends up containing hundreds of millions of json values, but since we don't,
    // this is fine.
    //
    // We could have also used a std::variant here but that is not implemented yet on all compilers
    // that we target.
    QByteArray data; // only for Num, Str
    std::vector<Container> values; // only for Arr
    std::vector<std::pair<QByteArray, Container>> entries; // only for Obj
    void clear() { data.clear(); values.clear(); entries.clear(); typ = Null; }
    void setArr() { clear(); typ = Arr; }
    void setObj() { clear(); typ = Obj; }
    void setBool(bool b) { clear(); typ = b ? BoolTrue: BoolFalse; }

    /// recursively scours this container and its sub-containers and builds the proper QVariant / nesting
    QVariant toVariant() const;
};

QVariant Container::toVariant() const
{
    QVariant ret;
    switch(typ) {
    case Null:
        // no further processing needed
        break;
    case Num: {
        if (UNLIKELY(data.isEmpty())) {
            // this should never happen
            throw Json::ParseError("Data is empty for a nested variant of type Num");
        }
        bool ok;
        if (data.contains('.') || data.contains('e') || data.contains('E')) {
            ret = data.toDouble(&ok);
        } else if (data.front() == '-') {
            ret = data.toLongLong(&ok);
        } else
            ret = data.toULongLong(&ok);
        if (UNLIKELY(!ok)) {
            // this should never happen
            throw Json::ParseError("Failed to parse a variant as a number");
        }
        break;
    }
    case BoolTrue:
        ret = true;
        break;
    case BoolFalse:
        ret = false;
        break;
    case Str:
        ret = QString::fromUtf8(data);
        break;
    case Arr: {
        QVariantList vl;
        vl.reserve(values.size());
        for (const auto & cont : values) {
            vl.push_back(cont.toVariant());
        }
        ret = vl;
        break;
    }
    case Obj: {
        QVariantMap vm;
        for (const auto & [key, cont] : entries) {
            vm[QString::fromUtf8(key)] = cont.toVariant();
        }
        ret = vm;
        break;
    }
    }
    return ret;
}
} // end anonymous namespace

namespace Json {
namespace detail {
bool parse(QVariant &out, const QByteArray &bytes)
{
    enum ExpectBits : uint32_t {
        EXP_OBJ_NAME = 1U << 0,
        EXP_COLON = 1U << 1,
        EXP_ARR_VALUE = 1U << 2,
        EXP_VALUE = 1U << 3,
        EXP_NOT_VALUE = 1U << 4,
    };
    uint32_t expectMask = 0;
#   define expect(bit) (expectMask & ExpectBits::EXP_##bit)
#   define setExpect(bit) (expectMask |= ExpectBits::EXP_##bit)
#   define clearExpect(bit) (expectMask &= ~ExpectBits::EXP_##bit)

    out = QVariant{}; // ensure cleared

    Container root;
    std::vector<Container *> stack;

    QByteArray tokenVal;
    unsigned consumed;
    jtokentype tok = JTOK_NONE;
    jtokentype last_tok = JTOK_NONE;
    const char *raw = bytes.data();
    const char * const end = raw + bytes.size();
    do {
        using VType = Container::Typ;

        last_tok = tok;

        tok = getJsonToken(tokenVal, consumed, raw, end);
        if (tok == JTOK_NONE || tok == JTOK_ERR)
            return false;
        raw += consumed;

        const bool isValueOpen = jsonTokenIsValue(tok) || tok == JTOK_OBJ_OPEN || tok == JTOK_ARR_OPEN;

        if (expect(VALUE)) {
            if (!isValueOpen)
                return false;
            clearExpect(VALUE);

        } else if (expect(ARR_VALUE)) {
            bool isArrValue = isValueOpen || tok == JTOK_ARR_CLOSE;
            if (!isArrValue)
                return false;

            clearExpect(ARR_VALUE);

        } else if (expect(OBJ_NAME)) {
            bool isObjName = tok == JTOK_OBJ_CLOSE || tok == JTOK_STRING;
            if (!isObjName)
                return false;

        } else if (expect(COLON)) {
            if (tok != JTOK_COLON)
                return false;
            clearExpect(COLON);

        } else if (!expect(COLON) && tok == JTOK_COLON) {
            return false;
        }

        if (expect(NOT_VALUE)) {
            if (isValueOpen)
                return false;
            clearExpect(NOT_VALUE);
        }

        switch (tok) {

        case JTOK_OBJ_OPEN:
        case JTOK_ARR_OPEN: {
            VType utyp = (tok == JTOK_OBJ_OPEN ? VType::Obj : VType::Arr);
            if (stack.empty()) {
                if (utyp == VType::Obj)
                    root.setObj();
                else
                    root.setArr();
                stack.push_back(&root);
            } else {
                Container *top = stack.back();
                if (top->typ == VType::Obj) {
                    // paranoia
                    if (UNLIKELY(top->entries.empty())) {
                        qCritical() << "Json Parser ERROR: Obj 'entries' is empty; FIXME!";
                        return false;
                    }
                    // /paranoia
                    auto& entry = top->entries.back();
                    if (utyp == VType::Obj)
                        entry.second.setObj();
                    else
                        entry.second.setArr();
                    stack.push_back(&entry.second);
                } else {
                    top->values.emplace_back(Container{utyp, {}, {}, {}});
                    stack.push_back(&top->values.back());
                }
            }

            if (UNLIKELY(stack.size() > MAX_JSON_DEPTH))
                return false;

            if (utyp == VType::Obj)
                setExpect(OBJ_NAME);
            else
                setExpect(ARR_VALUE);
            break;
            }

        case JTOK_OBJ_CLOSE:
        case JTOK_ARR_CLOSE: {
            if (UNLIKELY(stack.empty() || last_tok == JTOK_COMMA))
                return false;

            VType utyp = (tok == JTOK_OBJ_CLOSE ? VType::Obj : VType::Arr);
            Container *top = stack.back();
            if (UNLIKELY(utyp != top->typ))
                return false;

            stack.pop_back();
            clearExpect(OBJ_NAME);
            setExpect(NOT_VALUE);
            break;
            }

        case JTOK_COLON: {
            if (UNLIKELY(stack.empty()))
                return false;

            Container *top = stack.back();
            if (UNLIKELY(top->typ != VType::Obj))
                return false;

            setExpect(VALUE);
            break;
            }

        case JTOK_COMMA: {
            if (UNLIKELY(stack.empty() || last_tok == JTOK_COMMA || last_tok == JTOK_ARR_OPEN))
                return false;

            Container *top = stack.back();
            if (top->typ == VType::Obj)
                setExpect(OBJ_NAME);
            else
                setExpect(ARR_VALUE);
            break;
            }

        case JTOK_KW_NULL:
        case JTOK_KW_TRUE:
        case JTOK_KW_FALSE: {
            Container tmpVal;
            switch (tok) {
            case JTOK_KW_NULL:
                // do nothing more
                break;
            case JTOK_KW_TRUE:
                tmpVal.setBool(true);
                break;
            case JTOK_KW_FALSE:
                tmpVal.setBool(false);
                break;
            default: /* impossible */ break;
            }

            if (stack.empty()) {
                root = std::move(tmpVal);
                break;
            }

            Container *top = stack.back();
            if (top->typ == VType::Obj) {
                // paranoia
                if (UNLIKELY(top->entries.empty())) {
                    qCritical() << "Json Parser ERROR: Obj 'entries' is empty when parsing a keyword; FIXME!";
                    return false;
                }
                // /paranoia
                top->entries.back().second = std::move(tmpVal);
            } else {
                top->values.emplace_back(std::move(tmpVal));
            }

            setExpect(NOT_VALUE);
            break;
            }

        case JTOK_NUMBER: {
            Container tmpVal{VType::Num, std::move(tokenVal), {}, {}};
            if (stack.empty()) {
                root = std::move(tmpVal);
                break;
            }

            Container *top = stack.back();
            if (top->typ == VType::Obj) {
                // paranoia
                if (UNLIKELY(top->entries.empty())) {
                    qCritical() << "Json Parser ERROR: Obj 'entries' is empty when parsing a number; FIXME!";
                    return false;
                }
                // /paranoia
                top->entries.back().second = std::move(tmpVal);
            } else {
                top->values.emplace_back(std::move(tmpVal));
            }

            setExpect(NOT_VALUE);
            break;
            }

        case JTOK_STRING: {
            if (expect(OBJ_NAME)) {
                Container *top = stack.back();
                top->entries.emplace_back(std::piecewise_construct,
                                          std::forward_as_tuple(std::move(tokenVal)),
                                          std::forward_as_tuple());
                clearExpect(OBJ_NAME);
                setExpect(COLON);
            } else {
                Container tmpVal{VType::Str, std::move(tokenVal), {}, {}};
                if (stack.empty()) {
                    root = std::move(tmpVal);
                    break;
                }
                Container *top = stack.back();
                if (top->typ == VType::Obj) {
                    // paranoia
                    if (UNLIKELY(top->entries.empty())) {
                        qCritical() << "Json Parser ERROR: Obj 'entries' is empty when parsing a string; FIXME!";
                        return false;
                    }
                    // /paranoia
                    top->entries.back().second = std::move(tmpVal);
                } else {
                    top->values.emplace_back(std::move(tmpVal));
                }
            }

            setExpect(NOT_VALUE);
            break;
            }

        default:
            return false;
        }
    } while (!stack.empty());

    /* Check that nothing follows the initial construct (parsed above).  */
    tok = getJsonToken(tokenVal, consumed, raw, end);
    if (tok != JTOK_NONE)
        return false;

    try {
        out = root.toVariant(); // convert to (possibly nested) QVariant containing QVariants
    } catch (const std::exception &e) {
        // this is unlikely to happen, but may if std::bad_alloc (or if bugs in this code).
        qWarning() << "Failed to parse JSON:" << e.what();
        return false;
    }

    return true;
#   undef expect
#   undef setExpect
#   undef clearExpect
}
} // end namespace detail
} // end namespace Json
