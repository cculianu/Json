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
#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <stdexcept>

/// A namespace for a custom JSON parser and serializer that doesn't
/// suffer from the 128MB limit and heap fragmentation that Qt 5.14.x
/// and below suffer from, and has none of the bugs that Qt 5.15.0 has.
/// See:
///     https://bugreports.qt.io/browse/QTBUG-47629
///     https://bugreports.qt.io/browse/QTBUG-84610
namespace Json {
    /// Generic Json error (usually if ParseOption is violated)
    struct Error : public std::runtime_error {
        using std::runtime_error::runtime_error;
        Error(const QString &msg) : std::runtime_error(msg.toUtf8().constData()) {}
        ~Error() override;
    };
    /// More specific Json error -- usually if trying to parse malformed JSON text.
    struct ParseError : public Error { using Error::Error; };

    enum class ParseOption {
        RequireObject,     ///< Reject any JSON that is not embeded in a JSON object { ... }
        RequireArray,      ///< Reject any JSON that is not embeded in a JSON array [ ... ]
        AcceptAnyValue     ///< Do not require a root-level container: accept any JSON value that is valid e.g. "str", null, true, etc
    };

    /// If ParseOption is not satisfied, throws Error. May also throw Error on invalid JSON or throw
    /// std::exception too on low-level error (bad_alloc, etc).
    extern QVariant parseUtf8(const QByteArray &json, ParseOption = ParseOption::AcceptAnyValue);
    /// Convenience method -- loads all data from file and calls parseUtf8 on it.
    extern QVariant parseFile(const QString &file, ParseOption = ParseOption::AcceptAnyValue);

    enum class SerOption { NoBareNull, BareNullOk };
    /// Serialization, may throw Error, may throw std::exception on low-level error (bad_alloc, etc).
    /// Will throw also if given an empty QVariant{}, unless BareNullOk is specified.
    /// If compact = true, the generated JSON will have no whitespace between tokens.
    /// If compact = false, the generated JSON will have newlines and 4-spaces as indentation per indentation level.
    extern QByteArray toUtf8(const QVariant &, bool compact = false, SerOption = SerOption::BareNullOk);

    /// Low-level function -- like toUtf8() but lets you control the indentation level, etc.
    /// prettyIndent = 0 - compact (no whitespace), otherwise it will be the amount to indent at each level
    /// May throw Error or std::exception, or return an empty QByteArray on error.
    extern QByteArray serialize(const QVariant &v, unsigned prettyIndent = 0, unsigned indentLevel = 0);
}
