# Json

A lightweight JSON parser and serializer for Qt5 & Qt6 by Calin Culianu <calin.culianu@gmail.com>.

I initially wrote this code for use in my [Fulcrum](https://github.com/cculianu/Fulcrum) server software. Thinking it might be useful to others, I decided to also release this code as a stand-alone library.

### FAQ

**Q:** *Doesn't Qt already have a JSON parser and serializer in `QJsonDocument`? Why does this library even exist??*

**A:** Yes, it does. But in Qt versions prior to Qt 5.15.x, the [QJsonDocument serializer and parser suffered from a 128MB limitation](https://bugreports.qt.io/browse/QTBUG-47629). It also would fragment the heap over time because it would pre-alloc a huge buffer to operate on.  In Qt 5.15.0 they fixed that limitation and the heap fragmentation, but made serialization brutally slow (like 5x slower in some cases!) and they also [broke things](https://bugreports.qt.io/browse/QTBUG-84610). Given that basically you can't rely on the Qt JSON support across versions to be consistent, you can't rely on its performance, and you can't even rely on it to produce the same JSON, it was inadequate.  An independent library that doesn't change over time is needed.  You can just drop the files from this library in your project and be sure your JSON semantics and/or behavior and/or performance won't change as you target various Qt versions.


### Key Highlights:
- Requires C++17 and Qt5 or Qt6
- Like `QJsonDocument`, supports parsing/serialization directly to/from `QVariant`
- Performance, parsing:
  - Default backend: comparable to Qt 5.14.x (not quite as fast on parsing, but fast enough)
  - SimdJson backend: 2x faster than Qt!
- Performance, serializing:
  - Faster than Qt 5.15.x for serialization by a longshot.
- Doesn't fragment the heap (in Qt 5.14.x and earlier this could be a problem since it would cause your process memory to grow over time).
- Doesn't suffer from a 128MB limitation. Can work on arbitrarily* large data.
  - *Parser nesting limited to 512 deep by default but can be changed easily by modifying a compile-time constant (in `Json_Parser.cpp`).
- Supports more data types than Qt 5.15.x's JSON backend. Supports the following data types nested arbitrarily in your `QVariant`s:
  - `QString`, `QByteArray`, `QStringList`, `QByteArrayList`, `QVariantList`, `QVariantMap`, `QVariantHash`, `bool`, `int`, `long`, `unsigned int`, `unsigned long`, `uint64_t`, `int64_t`, `float`, `double`.
- Does not have Qt's quirks across versions. For example: Qt 5.14.x would allow you to put `QByteArray` directly in your `QVariant`s for encoding to JSON (they would just end up as utf-8 strings).  However, for some unexplained reason Qt 5.15.x does the unexpected thing of supporting `QByteArray`, but **auto-encoding** them for you to **base64** (!!), which is unexpected and unnecessarily enforces some weird application logic rules on what should basically just be an application-neutral data format.

### How to use in your project

1. Copy `Json.h`, `Json.cpp` and `Json_Parser.cpp` into your project.
2. Optionally, if you wish to use the SimdJson backend, copy `simdjson/simdjson.h` and `simdjson/simdjson/cpp` into your project as well.
2. Enjoy!  (The license here is MIT so you can use this in any project, commercial or open source).

### Quick Example

```c++
#include "Json.h"

// ... somewhere in your code:

auto somejson = "[\"astring\",\"anotherstring\",\"laststring\",null]";
// parse the above, convert to QVariantList
QVariantList l = Json::parseUtf8(somejson).toList();

// generate the same list programatically
QVariantList l2 = {{ "astring", "anotherstring", "laststring", QVariant{} }};
// serialize the list using a compact encoding
auto json = Json::toUtf8(l2, true);

// these should be equal now
assert(json == somejson);

// you can also work with maps:
QVariantMap m{{
    {"not quite pi", 3.140000001},
    {"a null", QVariant{}},
}};
// work with map to add more keys
m["some other key"] = false;
m["this one is a string"] = "hello world!";
// serialize the above to a JSON object which is "pretty" indented
auto mapjson = Json::toUtf8(m, false);
// print it, etc
std::cout << mapjson.constData() << '\n';
```

### More examples
See `main.cpp` in the project -- this contains tests and benches and maybe can be used to illustrate how to use this code as well.

License
----

MIT

### Donations
Yes please!  **Bitcoin** or **Bitcoin Cash** to this address please: **`1Ca1inCimwRhhcpFX84TPRrPQSryTgKW6N`**
