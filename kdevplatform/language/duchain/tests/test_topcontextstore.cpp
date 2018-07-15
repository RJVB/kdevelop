/*
 * This file is part of KDevelop
 *
 * Copyright 2018 R.J.V. Bertin <rjvbertin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

// with LMDB, the store is about 1.1x larger than the length of the stored values (but
// our LZ4 compression can reach as much as a 2x reduction in average value size).
// with KyotoCabinet-lzo, the store is about 1.76x smaller than the length of the stored values

// timing results on Linux 4.14 running on an 1.6Ghz Intel N3150 and ZFSonLinux 0.7.6

// KDEV_TOPCONTEXTS_USE_FILES:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 6000 items, max length= 38494 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 115232709 total key bytes: 24000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 6000 keys (last flushed): 1.583 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 6000 keys: 0.445 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 6000 keys: 0.736 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 3426ms
// ## Idem, with only 1500 keys
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38484 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 28350833 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 0.351 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 0.092 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.232 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 1171ms
// ## idem, on NFS:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 6000 items, max length= 38488 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 115800253 total key bytes: 24000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 6000 keys (last flushed): 108.579 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 6000 keys: 50.502 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 6000 keys: 27.651 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 194162ms
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38462 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 29375929 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 26.459 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 11.778 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 6.494 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 51921ms
// KDEV_TOPCONTEXTS_USE_LMDB:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38424 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 29024029 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 1.585 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 0.126 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.517 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// QINFO  : TestTopDUContextStore::benchTopContextStoreFlush() Flushing the database to disk: 0.656 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreFlush()
// total 14M
// 512 -rw-r--r-- 1 bertin bertin 8.0K May  8 14:19 lock.mdb
// 14M -rw-r--r-- 1 bertin bertin  27M May  8 14:19 data.mdb
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 22 passed, 0 failed, 0 skipped, 0 blacklisted, 18941ms
// ********* Finished testing of TestTopDUContextStore *********
// average LZ4 compression ratio: 1.95162; 1501 compressed of 3000
// ## idem, on NFS:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38496 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 29311234 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 12.362 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 0.113 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.221 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// QINFO  : TestTopDUContextStore::benchTopContextStoreFlush() Flushing the database to disk: 8.763 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreFlush()
// total 26M
// 8.0K -rw-r--r-- 1 bertin bertin 8.0K May  8  2018 lock.mdb
//  26M -rw-r--r-- 1 bertin bertin  26M May  8  2018 data.mdb
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 22 passed, 0 failed, 0 skipped, 0 blacklisted, 91060ms
// ********* Finished testing of TestTopDUContextStore *********
// average LZ4 compression ratio: 1.95829; 1499 compressed of 3000
// KDEV_TOPCONTEXTS_USE_KYOTO:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38467 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 29759286 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 7.891 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 6.139 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.212 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 37116ms
// ## idem, on NFS:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38468 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 28638008 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 27.135 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 17.317 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.218 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 180553ms

// ## on a 2.7Ghz i7 running Mac OS X 10.9.5
// KDEV_TOPCONTEXTS_USE_FILES:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 6000 items, max length= 38495 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 115547442 total key bytes: 24000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 6000 keys (last flushed): 1.622 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 6000 keys: 0.13 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 6000 keys: 0.924 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 8829ms
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38463 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 28304777 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 0.299 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 0.032 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.208 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 3508ms
// KDEV_TOPCONTEXTS_USE_LMDB:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38474 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 29277850 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 0.267 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 0.028 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.035 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// QINFO  : TestTopDUContextStore::benchTopContextStoreFlush() Flushing the database to disk: 0.188 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreFlush()
// total 52984
//    16 -rw-r--r--  1 bertin  bertin   8.0K May  8 14:44 lock.mdb
// 52968 -rw-r--r--  1 bertin  bertin    26M May  8 14:44 data.mdb
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 22 passed, 0 failed, 0 skipped, 0 blacklisted, 4704ms
// ********* Finished testing of TestTopDUContextStore *********
// average LZ4 compression ratio: 1.95736; 1500 compressed of 3000
// KDEV_TOPCONTEXTS_USE_KYOTO:
// QINFO  : TestTopDUContextStore::benchTopContextStore() 1500 items, max length= 38492 from sample of length 38497
// QINFO  : TestTopDUContextStore::benchTopContextStore()  total value bytes: 28963844 total key bytes: 6000
// QINFO  : TestTopDUContextStore::benchTopContextStore() Writing 1500 keys (last flushed): 22.364 seconds
// PASS   : TestTopDUContextStore::benchTopContextStore()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRead() Reading 1500 keys: 3.267 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRead()
// QINFO  : TestTopDUContextStore::benchTopContextStoreRemove() Removing 1500 keys: 0.052 seconds
// PASS   : TestTopDUContextStore::benchTopContextStoreRemove()
// PASS   : TestTopDUContextStore::cleanupTestCase()
// Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 38792ms


#include "test_topcontextstore.h"

#include <unistd.h>

#include <QTest>
#include <QDir>

#include <tests/autotestshell.h>
#include <tests/testcore.h>

#include <language/duchain/duchain.h>
#include <language/codegen/coderepresentation.h>

#include <language/duchain/topducontextdynamicdata_p.h>
#include <language/duchain/topducontextdynamicdata.h>

QTEST_MAIN(TestTopDUContextStore)

using namespace KDevelop;

uint TestTopDUContextStore::testKeys = 16;
uint TestTopDUContextStore::benchKeys = 1500;

static QString basePath()
{
    return globalItemRepositoryRegistry().path() + "/topcontexts/";
}

void TestTopDUContextStore::initTestCase()
{
    AutoTestShell::init();
    TestCore::initialize(Core::NoUi);

    DUChain::self()->disablePersistentStorage();
    CodeRepresentation::setDiskChangesForbidden(true);
    QDir().mkpath(basePath());

    // fetch real TopDUContext data from the qresource
    // (data obtained from a KDevelop project of the KDevelop source).
    QFile qfp(QStringLiteral(":/topcontextdata.base64"));
    if (qfp.open(QIODevice::ReadOnly)) {
        realData = QByteArray::fromBase64(qfp.readAll());
        qfp.close();
    }
}

void TestTopDUContextStore::cleanupTestCase()
{
#ifdef Q_OS_UNIX
    system(QStringLiteral("ls -lshtr %1").arg(basePath()).toLatin1());
#endif
    TestCore::shutdown();
}

void TestTopDUContextStore::testTopContextStore_data()
{
    QTest::addColumn<uint>("key");
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<int>("size");

    for (uint i = 0 ; i < benchKeys ; ++i) {
        TopDUContextStore writer(i);
        QByteArray data;
        uint size;
        if (i == 0 /*|| i >= testKeys*/) {
          // 1st key/value pair will use the stored real data so that we can
          // test our own LZ4 compression scheme in the LMDB backend (random
          // values won't compress).
          // The additional values >= testKeys serve to prime the database store
          // to a mostly constant size.
          data = realData;
        } else {
          size = qrand() * 32767.0 / RAND_MAX;
          data.resize(size);
          char *d = data.data();
          for (uint j = 0 ; j < size ; ++j) {
              d[j] = qrand() * 256.0 / RAND_MAX;
          }
        }
        if (i < testKeys) {
            // we store all keys that will be used in the benchmark so the store will be primed
            // but only do write/read/exists/remove verifications for the requested number of items.
            QTest::newRow("keyval") << i << data << data.size();
        }
        if (writer.open(QIODevice::WriteOnly)) {
            writer.write(data.constData(), data.size());
            writer.commit();
            const auto errString = writer.errorString();
            if (!errString.isEmpty() && errString != "Unknown error") {
                qCritical() << "Failed to write entry for key" << i << "to" << writer.fileName() << ":" << writer.errorString();
            } else {
                QVERIFY(writer.flush());
            }
        } else {
            qCritical() << "Failed to open" << writer.fileName();
        }
    }
    sync();
}

void TestTopDUContextStore::testTopContextStore()
{
    QFETCH(uint, key);
    QFETCH(QByteArray, value);
    QFETCH(int, size);

    TopDUContextStore reader(key);
    QByteArray testval;
    if (reader.open(QIODevice::ReadOnly)) {
        testval = reader.readAll();
        if (testval.isEmpty() && !reader.errorString().isEmpty()) {
            qCritical() << "Failed to read entry for key" << key  << "from" << reader.fileName() << ":" << reader.errorString();
        }
    } else {
        qCritical() << "Failed to open" << reader.fileName();
    }
    QCOMPARE(testval.size(), size);
    QCOMPARE(testval, value);
    QVERIFY(TopDUContextStore::exists(key));
    QVERIFY(TopDUContextStore::remove(key));
    QVERIFY(!TopDUContextStore::exists(key));
}

void TestTopDUContextStore::benchTopContextStore_data()
{
    uint maxSize = 0, totalSize = 0;
    benchValues.reserve(benchKeys);
    // subsample random strings from the real topcontext data
    // (always from the left, there should be no interest to
    // randomising the offset).
    for (uint i = 0 ; i < benchKeys ; ++i) {
        uint size = qrand() * double(realData.size()) / RAND_MAX;
        if (size > maxSize) {
            maxSize = size;
        }
        benchValues.append(realData.left(size));
        totalSize += size;
        if (i >= testKeys) {
          // remove keys that weren't removed in testTopContextStore()
          // so we will time writing, not overwriting
          TopDUContextStore::remove(i);
        }
    }
    sync();
    sleep(1);
    qInfo() << benchValues.size() << "items, max length=" << maxSize << "from sample of length" << realData.size();
    qInfo() << "\ttotal value bytes:" << totalSize << "total key bytes:" << benchKeys * sizeof(uint);
}

// benchmarks don't use QBENCHMARK because they're not suitable for being
// called multiple times.
void TestTopDUContextStore::benchTopContextStore()
{
    QElapsedTimer timer;
    timer.start();
    for (uint i = 0 ; i < benchKeys ; ++i) {
        TopDUContextStore writer(i);
        if (writer.open(QIODevice::WriteOnly)) {
            QByteArray data = benchValues.at(i);
            writer.write(data.constData(), data.size());
            writer.commit();
            if (i == benchKeys - 1) {
              writer.flush();
            }
        }
    }
    qInfo() << "Writing" << benchKeys << "keys (last flushed):" << timer.elapsed() / 1000.0 << "seconds";
}

#include <unistd.h>
void TestTopDUContextStore::benchTopContextStoreRead()
{
    QElapsedTimer timer;
    timer.start();
    for (uint i = 0 ; i < benchKeys ; ++i)
    {
        TopDUContextStore reader(i);
        if (reader.open(QIODevice::ReadOnly)) {
#if defined(KDEV_TOPCONTEXTS_USE_FILES) && !defined(KDEV_TOPCONTEXTS_DONT_MMAP)
            uchar* data = reader.map(0, reader.size());
            reader.commit();
#else
            QByteArray data = reader.readAll();
#endif
        }
    }
    qInfo() << "Reading" << benchKeys << "keys:" << timer.elapsed() / 1000.0 << "seconds";
}

void TestTopDUContextStore::benchTopContextStoreRemove()
{
    QElapsedTimer timer;
    timer.start();
    for (uint i = 0 ; i < benchKeys ; ++i)
    {
        TopDUContextStore::remove(i);
    }
    qInfo() << "Removing" << benchKeys << "keys:" << timer.elapsed() / 1000.0 << "seconds";
}

void TestTopDUContextStore::benchTopContextStoreFlush()
{
#ifndef KDEV_TOPCONTEXTS_USE_FILES
    QElapsedTimer timer;
    timer.start();
    TopDUContextStore writer(0);
    if (writer.open(QIODevice::WriteOnly)) {
        writer.flush();
    }
    qInfo() << "Flushing the database to disk:" << timer.elapsed() / 1000.0 << "seconds";
#endif
}
