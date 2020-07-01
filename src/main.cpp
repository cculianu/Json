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
#include "Json_Parser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtDebug>
#include <QTimer>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>

namespace {
/// Super class of Debug, Warning, Error classes.  Can be instantiated for regular log messages.
class Log
{
    QString str = "";
    QTextStream s = QTextStream(&str, QIODevice::WriteOnly);
public:
    ~Log() { std::cout << str.toUtf8().constData() << std::endl; }
    template <class T> Log & operator<<(const T & t) { s << t; return *this;  }
};


struct BadArgs : public Json::Error {
    using Json::Error::Error;
    ~BadArgs() override;
};

BadArgs::~BadArgs() {}
using Exception = Json::Error; // alias

static const auto t0 = std::chrono::high_resolution_clock::now();
double getTimeSecs() {
    const auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - t0).count() / 1e6;
}
}
void bench(const QString &dir = "bench") {
    using namespace Json;
    QDir dataDir(dir);
    if (!dataDir.exists()) throw BadArgs(QString("Bench data directory '%1' does not exist").arg(dir));
    auto files = dataDir.entryList({{"*.json"}}, QDir::Filter::Files);
    if (files.isEmpty()) throw BadArgs(QString("Bench data directory '%1' does not have any *.json files").arg(dir));
    std::vector<QByteArray> fileData;
    std::size_t total = 0;
    Log() << "Reading " << files.size() << " *.json files from DATADIR=" << dir << " ...";

    for (auto & fn : files) {
        QFile f(dataDir.path() + QDir::separator() + fn);
        if (!f.open(QFile::ReadOnly|QFile::Text))
            throw Exception(QString("Cannot open %1").arg(f.fileName()));
        fileData.push_back(f.readAll());
        total += fileData.back().size();
    }
    Log() << "Read " << total << " bytes total";
    std::vector<QVariant> parsed;
    int iters = 1;
    {
        auto itenv = std::getenv("ITERS");
        if (itenv) {
            bool ok;
            iters = QString(itenv).toInt(&ok);
            if (!ok || iters <= 0)
                throw BadArgs("Expected ITERS= to be a positive integer");
        }
    }
    Log() << "---";
    Log() << "Benching custom Json lib parse: Iterating " << iters << " times ...";
    double t0 = getTimeSecs();
    for (int i = 0; i < iters; ++i) {
        for (const auto & ba : fileData) {
            auto var = parseUtf8(ba, ParseOption::AcceptAnyValue);
            if (var.isNull()) throw Exception("Parse result is null");
            if (parsed.size() != fileData.size())
                parsed.push_back(var); // save parsed data
        }
    }
    double tf = getTimeSecs();
    Log() << "Custom lib parse - total: " << (tf-t0) << " secs" << " - per-iter: "
          << QString::asprintf("%1.16g", ((tf-t0)/iters) * 1e3) << " msec";
    parsed.clear();

    Log() << "---";
    Log() << "Benching Qt Json parse: Iterating " << iters << " times ...";
    t0 = getTimeSecs();
    for (int i = 0; i < iters; ++i) {
        for (const auto & ba : fileData) {
            QJsonParseError err;
            auto d = QJsonDocument::fromJson(ba, &err);
            if (d.isNull())
                throw Exception(QString("Could not parse: %1").arg(err.errorString()));
            auto var = d.toVariant();
            if (var.isNull()) throw Exception("Parse result is null");
            if (parsed.size() != fileData.size())
                parsed.push_back(var); // save parsed data
        }
    }
    tf = getTimeSecs();
    Log() << "Qt Json parse - total: " << (tf-t0) << " secs" << " - per-iter: "
          << QString::asprintf("%1.16g", ((tf-t0)/iters) * 1e3) << " msec";

    Log() << "---";
    Log() << "Benching custom Json lib serialize: Iterating " << iters << " times ...";
    t0 = getTimeSecs();
    for (int i = 0; i < iters; ++i) {
        for (const auto & var : parsed) {
            auto json = serialize(var, 4); // throw on error
        }
    }
    tf = getTimeSecs();
    Log() << "Custom lib serialize - total: " << (tf-t0) << " secs" << " - per-iter: "
          << QString::asprintf("%1.16g", ((tf-t0)/iters) * 1e3) << " msec";

    Log() << "---";
    Log() << "Benching Qt lib serialize: Iterating " << iters << " times ...";
    t0 = getTimeSecs();
    for (int i = 0; i < iters; ++i) {
        for (const auto & var : parsed) {
            auto d = QJsonDocument::fromVariant(var);
            auto json = d.toJson(QJsonDocument::JsonFormat::Indented);
            if (json.isEmpty()) throw Exception("Serializaiton error");
        }
    }
    tf = getTimeSecs();
    Log() << "Qt lib serialize - total: " << (tf-t0) << " secs" << " - per-iter: "
          << QString::asprintf("%1.16g", ((tf-t0)/iters) * 1e3) << " msec";

    Log() << "---";
}

void test(const QString &dir = "test")
{
    using namespace Json;
    // basic tests
    {
        const auto expect1 = "[\"astring\",\"anotherstring\",\"laststring\",null]";
        const auto expect2 = "[\"astringl1\",\"anotherstringl2\",\"laststringl3\",\"\"]";
        const auto expect3 = "{\"7 item list\":[1,true,false,1.4e-07,null,{},[-777777.293678102,null,"
                             "-999999999999999999]],\"a bytearray\":\"bytearray\",\"a null\":null,"
                             "\"a null bytearray\":null,\"a null string\":\"\",\"a string\":\"hello\","
                             "\"an empty bytearray\":null,\"an empty string\":\"\",\"another empty bytearray\":"
                             "null,\"empty balist\":[],\"empty strlist\":[],\"empty vlist\":[],\"nested map key\":"
                             "3.140000001,\"u64_max\":18446744073709551615,\"z_i64_min\":-9223372036854775808}";
        QByteArray json;
        QByteArrayList bal = {{ "astring", "anotherstring", "laststring", QByteArray{} }};
        QVariant v;
        v.setValue(bal);
        Log() << "QByteArrayList -> JSON: " << (json=toUtf8(v, true, SerOption::BareNullOk));
        if (json != expect1) throw Exception(QString("Json does not match, excpected: %1").arg(expect1));
        QStringList sl = {{ "astringl1", "anotherstringl2", "laststringl3", QString{} }};
        v.setValue(sl);
        Log() << "QStringList -> JSON: " << (json=toUtf8(v, true, SerOption::BareNullOk));
        if (json != expect2) throw Exception(QString("Json does not match, excpected: %1").arg(expect2));
        QVariantHash h;
        QByteArray empty; empty.resize(10); empty.resize(0);
        h["key1"] = 1.2345;
        h["another key"] = sl;
        h["mapkey"] = QVariantMap{{
           {"nested map key", 3.140000001},
           {"a null", QVariant{}},
           {"a null bytearray", QByteArray{}},
           {"a null string", QString{}},
           {"an empty string", QString{""}},
           {"an empty bytearray", QByteArray{""}},
           {"another empty bytearray", empty},
           {"a string", QString{"hello"}},
           {"a bytearray", QByteArray{"bytearray"}},
           {"empty vlist", QVariantList{}},
           {"empty strlist", QStringList{}},
           {"empty balist", QVariant::fromValue(QByteArrayList{})},
           {"7 item list", QVariantList{{
                1,true,false,14e-8,QVariant{}, QVariantMap{}, QVariantList{{-777777.293678102, QVariant{},
                qlonglong(-999999999999999999)}}}},
           },
           {"u64_max", qulonglong(18446744073709551615ULL)},
           {"z_i64_min", qlonglong(0x8000000000000000LL)},
        }};
        Log() << "QVariantHash -> JSON: " << toUtf8(h, true, SerOption::BareNullOk);
        // we can't do the top-level hash since that has random order based on hash seed.. so we do this
        json = toUtf8(h, false /* !compact */, SerOption::BareNullOk);
        auto hh = parseUtf8(json, ParseOption::RequireObject).toMap();
        json = toUtf8(hh["mapkey"], true, SerOption::BareNullOk);
        if (json != expect3) throw Exception(QString("Json \"mapkey\" does not match\nexcpected:\n%1\n\ngot:\n%2").arg(expect3).arg(QString(json)));
        Log() << "Basic tests: passed";
    }
    // /end basic tests
    QDir dataDir(dir);
    if (!dataDir.exists()) throw BadArgs(QString("DATADIR '%1' does not exist").arg(dir));
    struct TFile {
        QString path;
        bool wantsFail{}, wantsRound{};
    };
    std::list<TFile> files;
    for (auto & file : dataDir.entryList({{"*.json"}}, QDir::Filter::Files)) {
        TFile t;
        if (file.startsWith("pass"))
            t.wantsFail = false;
        else if (file.startsWith("fail"))
            t.wantsFail = true;
        else if (file.startsWith("round"))
            t.wantsFail = false, t.wantsRound = true;
        else
            // skip unrelated json file
            continue;
        t.path = dataDir.path() + QDir::separator() + file;
        files.push_back(std::move(t));
    }
    if (files.empty()) throw BadArgs(QString("DATADIR '%1' does not have any [pass/fail/round]*.json files").arg(dir));
    Log() << "Found " << files.size() << " json test files, running extended tests ...";
    const auto runTest = [](const TFile &t) {
        QFile f(t.path);
        auto baseName = QFileInfo(t.path).baseName();
        if (!f.open(QFile::ReadOnly|QFile::Text))
            throw Exception(QString("Cannot open %1").arg(f.fileName()));
        const QByteArray json = f.readAll();
        QVariant var;
        bool didFail = false;
        try {
            var = parseUtf8(json, ParseOption::AcceptAnyValue);
        } catch (...) {
            if (!t.wantsFail)
                throw;
            didFail = true;
        }
        if (t.wantsFail && !didFail)
            throw Exception(QString("Expected to fail test: %1 (Json: %2)").arg(baseName).arg(QString(toUtf8(var, true, SerOption::BareNullOk))));
        if (t.wantsRound) {
            if (auto json2 = toUtf8(var, true, SerOption::BareNullOk); json.trimmed() != json2.trimmed())
                throw Exception(QString("Round-trip deser/ser failed for: %1\n\nExpected:\n%2\nHex: %3\n\nGot:\n%4\nHex: %5").arg(baseName)
                                .arg(QString(json)).arg(QString(json.toHex()))
                                .arg(QString(json2)).arg(QString(json2.toHex())));
        }
        Log() << baseName << ": passed";
    };
    for (const auto & t : files)
        runTest(t);
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    if (argc >= 2 && QByteArrayLiteral("bench") == argv[1]) {
        QTimer::singleShot(0, &a, [&]{
            try {
                if (argc > 2)
                    bench(argv[2]);
                else
                    bench();
                a.exit(0);
            } catch (const std::exception &e) {
                qCritical() << "Caught exception:" << e.what();
                a.exit(1);
            }
        });
    } else if (argc >= 2 && QByteArrayLiteral("test") == argv[1]) {
        QTimer::singleShot(0, &a, [&]{
            try {
                if (argc > 2)
                    test(argv[2]);
                else
                    test();
                a.exit(0);
            } catch (const std::exception &e) {
                qCritical() << "Caught exception:" << e.what();
                a.exit(1);
            }
        });
    } else {
        Log() << "Please specify one of: bench, test";
        return 1;
    }
    return a.exec();
}
