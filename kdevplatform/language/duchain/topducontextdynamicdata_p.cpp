/* This  is part of KDevelop

   Copyright 2018 R.J.V. Bertin <rjvbertin@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "topducontextdynamicdata_p.h"
#include "topducontextdynamicdata.h"

#include <QFileInfo>

#ifndef KDEV_TOPCONTEXTS_USE_FILES
#ifdef KDEV_TOPCONTEXTS_USE_LMDB
#include <lmdb++.h>
// we roll our own compression
#include <lz4.h>
#else
#define MDB_RDONLY 0x20000
#endif
#ifdef KDEV_TOPCONTEXTS_USE_LEVELDB
#include <leveldb/db.h>
#include <leveldb/comparator.h>
#endif
#ifdef KDEV_TOPCONTEXTS_USE_KYOTO
#include <kcpolydb.h>
#endif
#endif

#include <debug.h>

//#define DEBUG_DATA_INFO

using namespace KDevelop;

#ifndef KDEV_TOPCONTEXTS_USE_FILES

bool TopDUContextDB::open(QIODevice::OpenMode mode, const QString &backendName)
{
    if (isValid() && !m_currentKey.isEmpty()) {
        int lmMode = mode == QIODevice::ReadOnly ? MDB_RDONLY : 0;
        if (lmMode == MDB_RDONLY && !currentKeyExists()) {
            // migration: see if the index file exists
            if (migrateFromFile()) {
                return true;
            }
            m_errorString = QStringLiteral("No item #") + QByteArray::number(m_currentIndex) + QStringLiteral(" in database");
            return false;
        }
        m_mode = lmMode;
        m_currentValue.clear();
        m_currentLen = 0;
        m_readCursor = -1;
        m_errorString.clear();
        return true;
    }
    m_errorString = QStringLiteral("%1 database backend not initialised properly").arg(backendName);
    return false;
}

QString TopDUContextDB::fileName() const
{
    return TopDUContextDynamicData::basePath() + "...#" + QByteArray::number(m_currentIndex);
}

QString TopDUContextDB::errorString() const
{
    return m_errorString;
}

bool TopDUContextDB::resize(qint64)
{
    return m_mode != MDB_RDONLY;
}

qint64 TopDUContextDB::write(const char *data, qint64 len)
{
    if (m_mode != MDB_RDONLY) {
        m_currentValue.append(QByteArray::fromRawData(data, len));
        m_currentLen += len;
        return len;
    }
    return 0;
}

// read the current key value into m_currentValue if necessary, and return the
// requested @p maxSize bytes from the current read position in @p data. Update
// the read position afterwards, and reset m_currentValue when all data has
// been returned.
// Special case: data==NULL and maxSize==-1; return the number of remaining bytes
// and do NOT reset m_currentValue
qint64 TopDUContextDB::read(char *data, qint64 maxSize)
{
    if (isValid() && !m_currentKey.isEmpty() && m_mode == MDB_RDONLY) {
        if (!getCurrentKeyValue()) {
            return -1;
        }
        if (m_readCursor >= 0 && m_readCursor < m_currentLen) {
            qint64 rlen = m_currentLen - m_readCursor;
            if (Q_LIKELY(maxSize >= 0)) {
                if (maxSize < rlen) {
                    rlen = maxSize;
                }
                const char *val = m_currentValue.constData();
                memcpy(data, &val[m_readCursor], rlen);
                m_readCursor += rlen;
                if (m_readCursor >= m_currentLen) {
                    // all read, clear the cache
                    m_currentValue.clear();
                    m_currentLen = 0;
                    m_readCursor = -1;
                }
            } else {
                // special case: don't update m_readCursor;
            }
            return rlen;
        }
    }
    return -1;
}

QByteArray TopDUContextDB::read(qint64 maxSize)
{
    QByteArray data;
    data.resize(maxSize);
    auto len = read(data.data(), maxSize);
    data.resize(len >= 0 ? len : 0);
    return data;
}

// Reads all the remaining data, returned as a QByteArray
QByteArray TopDUContextDB::readAll()
{
    QByteArray data;
    auto readLen = read(nullptr, -1);
    if (readLen > 0) {
        // should always be true:
        if (Q_LIKELY(m_readCursor >= 0 && m_readCursor + readLen <= m_currentValue.size())) {
            if (m_readCursor == 0) {
                data = m_currentValue;
            } else {
                data.resize(readLen);
                const char *val = m_currentValue.constData();
                memcpy(data.data(), &val[m_readCursor], readLen);
            }
        } else {
            qCWarning(LANGUAGE) << Q_FUNC_INFO << "m_readCursor=" << m_readCursor << "readLen=" << readLen
              << "m_currentValue.size=" << m_currentValue.size();
            if (readLen == m_currentValue.size()) {
                // m_readCursor should have been 0!!!
                qCWarning(LANGUAGE) << "\tm_readCursor should have been 0!!";
                data = m_currentValue;
            }
        }
        // all read, clear the cache
        m_currentValue.clear();
        m_currentLen = 0;
        m_readCursor = -1;
    }
    return data;
}

qint64 TopDUContextDB::pos() const
{
    return m_readCursor < 0 ? 0 : m_readCursor;
}

bool TopDUContextDB::seek(qint64 pos)
{
    if (pos <= m_currentLen) {
        m_readCursor = pos;
        return true;
    }
    return false;
}

qint64 TopDUContextDB::size()
{
    if (!m_currentLen && m_mode == MDB_RDONLY) {
        // cache the key value
        read(nullptr, -1);
    }
    return m_currentLen;
}

QByteArray TopDUContextDB::indexKey(uint idx)
{
    return QByteArray::number(idx);
}

// NB: @p idx must point to a variable that will not be outlived by the returned QByteArray!
QByteArray TopDUContextDB::indexKey(uint *idx)
{
    return QByteArray::fromRawData(reinterpret_cast<const char *>(idx), sizeof(uint));
}

bool TopDUContextDB::migrateFromFile()
{
    TopDUContextFile migrateFile(m_currentIndex);
    if (migrateFile.open(QIODevice::ReadOnly)) {
        // should we care about empty files here?
        qCDebug(LANGUAGE) << "Migrating" << migrateFile.fileName();
        const QByteArray content = migrateFile.readAll();
        migrateFile.close();
        m_mode = 0;
        m_currentValue = content;
        m_currentLen = content.size();
        // commit() will reset the key so we need to cache it
        const QByteArray key = m_currentKey;
        m_errorString.clear();
        commit();
        if (m_errorString.isEmpty()) {
            // migration was successful, remove the file
            QFile::remove(migrateFile.fileName());
        }
        m_errorString.clear();
        // take care that we don't have to read the data back in
        m_currentKey = key;
        m_currentValue = content;
        m_currentLen = content.size();
        m_readCursor = 0;
        m_mode = MDB_RDONLY;
        return true;
    }
    return false;
}

#ifdef KDEV_TOPCONTEXTS_USE_LMDB
// TopDUContextLMDB : wraps the QFile API needed for TopDUContexts around LMDB

static QString lmdbxx_exception_handler(const lmdb::error &e, const QString &operation)
{
    const QString msg = QStringLiteral("LMDB exception in \"%1\": %2").arg(operation).arg(e.what());
    qCWarning(LANGUAGE) << msg;
    if (qEnvironmentVariableIsSet("KDEV_TOPCONTEXTS_STORE_FAILURE_ABORT")) {
        qFatal(msg.toLatin1());
    }
    return msg;
}

// there is exactly 1 topcontexts directory per session, so we can make do with a single
// global static LMDB env instance which is not exported at all (= no need to include
// lmdb.h and/or lmdbxx.h in our own headerfile).

static double compRatioSum = 0;
static size_t compRatioN = 0;
static size_t uncompN = 0;

static void printCompRatio()
{
    if (compRatioN) {
        fprintf(stderr, "average LZ4 compression ratio: %g; %lu compressed of %lu\n",
            compRatioSum / compRatioN, compRatioN, compRatioN + uncompN);
    }
}

class LMDBHook
{
public:
    ~LMDBHook()
    {
      if (s_envExists) {
          s_lmdbEnv.close();
          s_envExists = false;
          delete s_lz4State;
          printCompRatio();
      }
    }

    static bool init()
    {
        if (!s_envExists) {
            s_errorString.clear();
            try {
                s_lmdbEnv = lmdb::env::create();
                // Open the environment in async mode. From the documentation:
                // if the filesystem preserves write order [...], transactions exhibit ACI 
                // (atomicity, consistency, isolation) properties and only lose D (durability).
                // I.e. database integrity is maintained, but a system crash may undo the final transactions.
                // This should be acceptable for caching self-generated data, given how much faster
                // transactions become.
                s_lmdbEnv.open(TopDUContextDynamicData::basePath().toLatin1().constData(), MDB_NOSYNC);
                MDB_envinfo stat;
                lmdb::env_info(s_lmdbEnv.handle(), &stat);
                if (stat.me_mapsize > s_mapSize) {
                    s_mapSize = stat.me_mapsize;
                }
                s_lmdbEnv.set_mapsize(s_mapSize);
                s_lz4State = new char[LZ4_sizeofState()];
                s_envExists = true;
                qCDebug(LANGUAGE) << "s_lmdbEnv=" << s_lmdbEnv << "mapsize=" << stat.me_mapsize << "LZ4 state buffer:" << LZ4_sizeofState();
            } catch (const lmdb::error &e) {
                s_errorString = lmdbxx_exception_handler(e, QStringLiteral("database creation"));
                // as per the documentation: the environment must be closed even if creation failed!
                s_lmdbEnv.close();
            }
        }
        return false;
    }

    inline lmdb::env* instance()
    {
        return s_envExists? &s_lmdbEnv : nullptr;
    }
    inline MDB_env* handle()
    {
        return s_envExists? s_lmdbEnv.handle() : nullptr;
    }

    void growMapSize()
    {
        s_mapSize *= 2;
        qCDebug(LANGUAGE) << "\tgrowing mapsize to" << s_mapSize;
        s_lmdbEnv.set_mapsize(s_mapSize);
    }


    static lmdb::env s_lmdbEnv;
    static bool s_envExists;
    static char* s_lz4State;
    static size_t s_mapSize;
    static QString s_errorString;
};
static LMDBHook LMDB;

lmdb::env LMDBHook::s_lmdbEnv{nullptr};
bool LMDBHook::s_envExists = false;
char *LMDBHook::s_lz4State = nullptr;
// set the initial map size to 64Mb
size_t LMDBHook::s_mapSize = 1024UL * 1024UL * 64UL;
QString LMDBHook::s_errorString;

uint TopDUContextLMDB::s_DbRefCount = 0;

TopDUContextLMDB::TopDUContextLMDB(uint topContextIndex)
{
    m_currentIndex = topContextIndex;
    if (!LMDB.instance() && Q_LIKELY(QFileInfo(TopDUContextDynamicData::basePath()).isWritable())) {
        if (!LMDB.init()) {
            m_errorString = LMDB.s_errorString;
        }
    }
    if (LMDB.instance()) {
        m_currentKey = indexKey(m_currentIndex);
        s_DbRefCount += 1;
    }
    m_currentLen = -1;
    // the remaining member vars are initialised elsewhere intentionally.
}

TopDUContextLMDB::~TopDUContextLMDB()
{
    if (LMDB.instance()) {
        s_DbRefCount -= 1;
        if (s_DbRefCount <= 0) {
            s_DbRefCount = 0;
#ifdef DEBUG
            flush();
#endif
        }
    } else {
        s_DbRefCount = 0;
    }
}

bool TopDUContextLMDB::open(QIODevice::OpenMode mode)
{
    return TopDUContextDB::open(mode, QStringLiteral("LMDB"));
}

void TopDUContextLMDB::commit()
{
    if (LMDB.instance() && m_mode != MDB_RDONLY) {
        if (m_currentValue.size() != m_currentLen) {
            // m_currentLen is the true size
            qCDebug(LANGUAGE) << "TopDUContextLMDB index" << QByteArray::number(m_currentIndex) << "internal size mismatch:"
              << m_currentValue.size() << "vs" << m_currentLen;
        }
        char *data;
        size_t dataLen = 0;
        int lz4BufLen = m_currentLen > 2 * sizeof(qint64) ? LZ4_compressBound(m_currentLen) : 0;
        lmdb::val value;
        if (lz4BufLen) {
            data = new char[lz4BufLen + sizeof(qint64)];
            // compress to an qint64-sized offset into the dest buffer; the original size will be stored in
            // those first 64 bits.
            dataLen = LZ4_compress_fast_extState(LMDB.s_lz4State, m_currentValue.constData(), &data[sizeof(qint64)],
              m_currentLen, lz4BufLen, 1);
            if (dataLen && dataLen + sizeof(qint64) < m_currentLen) {
                LZ4Frame frame;
                frame.bytes = data;
                frame.qint32Ptr[0] = m_currentLen;
                frame.qint32Ptr[1] = dataLen;
                value = lmdb::val(data, dataLen + sizeof(qint64));
                compRatioSum += double(m_currentLen) / value.size();
                compRatioN += 1;
            } else {
                qCDebug(LANGUAGE) << "Index" << m_currentIndex << "compression failed or useless: m_currentLen=" << m_currentLen
                  << "compressedLen=" << dataLen << "LZ4_compressBound=" << lz4BufLen;
                delete data;
                dataLen = 0;
                lz4BufLen = 0;
            }
        }
        if (!dataLen) {
            value = lmdb::val(m_currentValue.constData(), m_currentLen);
            uncompN += 1;
        }
        lmdb::val key(m_currentKey.constData(), m_currentKey.size());
        try {
            auto txn = lmdb::txn::begin(LMDB.handle());
            auto dbi = lmdb::dbi::open(txn, nullptr);
            try {
                lmdb::dbi_put(txn, dbi, key, value);
                txn.commit();
            } catch (const lmdb::error &e) {
                if (e.code() == MDB_MAP_FULL) {
                    try {
                        qCDebug(LANGUAGE) << "aborting LMDB write to grow mapsize";
                        txn.abort();
                        lmdb::dbi_close(LMDB.handle(), dbi);
                        LMDB.growMapSize();
                        commit();
                    } catch (const lmdb::error &e) {
                        m_errorString = lmdbxx_exception_handler(e, QStringLiteral("growing mapsize to ")
                            + QString::number(LMDB.s_mapSize));
                    }
                } else {
                    m_errorString = lmdbxx_exception_handler(e, QStringLiteral("committing index ")
                        + QByteArray::number(m_currentIndex) + " size " + QString::number(m_currentLen));
                }
            }
        } catch (const lmdb::error &e) {
            m_errorString = lmdbxx_exception_handler(e, QStringLiteral("committing index ")
                + QByteArray::number(m_currentIndex) + " size " + QString::number(m_currentLen));
        }
        m_currentKey.clear();
        m_currentValue.clear();
        m_currentLen = 0;
        if (lz4BufLen && data) {
            delete data;
        }
    }
}

bool TopDUContextLMDB::flush()
{
    if (LMDB.instance() && m_mode != MDB_RDONLY) {
        try {
            return mdb_env_sync(LMDB.handle(), true) == MDB_SUCCESS;
        } catch (const lmdb::error &e) {
            m_errorString = lmdbxx_exception_handler(e, QStringLiteral("database flush"));
        }
    }
    return false;
}

bool TopDUContextLMDB::getCurrentKeyValue()
{
    // we only return false if a read error occurred; if a key doesn't exist
    // m_currentValue will remain empty.
    bool ret = true;
    if (m_currentValue.isEmpty()) {
        // read the key value from storage into cache
        try {
            auto rtxn = lmdb::txn::begin(LMDB.handle(), nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(rtxn, nullptr);
            auto cursor = lmdb::cursor::open(rtxn, dbi);
            lmdb::val key(m_currentKey.constData(), m_currentKey.size());
            lmdb::val val {};
            bool found = cursor.get(key, val, MDB_SET);
            if (found) {
                bool isRaw = true;
                if (val.size() > sizeof(qint64)) {
                    LZ4Frame frame;
                    frame.bytes = val.data();
                    int orgSize = frame.qint32Ptr[0];
                    int compressedSize = val.size() - sizeof(qint64);
                    if (orgSize > 0 && frame.qint32Ptr[1] == compressedSize) {
                        // size m_currentValue so decompression can go into the final destination directly
                        m_currentValue.resize(orgSize);
                        if (LZ4_decompress_safe(&frame.bytes[sizeof(qint64)], m_currentValue.data(), compressedSize, orgSize) == orgSize) {
                            isRaw = false;
                            m_currentLen = m_currentValue.size();
                        } else {
//                           qCDebug(LANGUAGE) << "Index" << m_currentIndex << "failed LZ4 decompression from size" << compressedSize << "to size" << orgSize;
                        }
                    }
                }
                if (isRaw) {
//                     qCDebug(LANGUAGE) << "Index" << m_currentIndex << "is uncompressed, size=" << val.size();
                    m_currentValue = QByteArray(val.data(), val.size());
                    m_currentLen = m_currentValue.size();
                }
                m_readCursor = 0;
            }
            cursor.close();
            rtxn.abort();
        } catch (const lmdb::error &e) {
            m_errorString = lmdbxx_exception_handler(e, QStringLiteral("reading index ") + QByteArray::number(m_currentIndex));
            ret = false;
        }
    }
    return ret;
}

bool TopDUContextLMDB::isValid()
{
    return LMDB.instance();
}

bool TopDUContextLMDB::currentKeyExists()
{
    return exists(m_currentKey);
}

bool TopDUContextLMDB::exists(const QByteArray &key)
{
    if (LMDB.instance()) {
        try {
            auto rtxn = lmdb::txn::begin(LMDB.handle(), nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(rtxn, nullptr);
            auto cursor = lmdb::cursor::open(rtxn, dbi);
            lmdb::val k(key.constData(), key.size());
            bool ret = cursor.get(k, nullptr, MDB_SET);
            cursor.close();
            rtxn.abort();
            return ret;
        } catch (const lmdb::error &e) {
            lmdbxx_exception_handler(e, QStringLiteral("checking for index") + key);
        }
    }
    return false;
}

QString TopDUContextLMDB::fileName() const
{
    return TopDUContextDynamicData::basePath() + "data.mdb" + ":#" + QByteArray::number(m_currentIndex);
}

bool TopDUContextLMDB::exists(uint topContextIndex)
{
    return exists(indexKey(topContextIndex));
}

bool TopDUContextLMDB::remove(uint topContextIndex)
{
    if (LMDB.instance()) {
        const auto key = indexKey(topContextIndex);
        lmdb::val k {key.constData(), static_cast<size_t>(key.size())};
        try {
            auto txn = lmdb::txn::begin(LMDB.handle());
            auto dbi = lmdb::dbi::open(txn, nullptr);
            bool ret = lmdb::dbi_del(txn, dbi, k, nullptr);
            txn.commit();
            // also remove the file if it (still) exists
            QFile::remove(TopDUContextDynamicData::pathForTopContext(topContextIndex));
            return ret;
        } catch (const lmdb::error &e) {
            lmdbxx_exception_handler(e, QStringLiteral("removing index %1").arg(topContextIndex));
        }
    }
    return false;
}
#endif

#ifdef KDEV_TOPCONTEXTS_USE_LEVELDB
// LevelDB storage backend
static QString leveldb_exception_handler(const std::exception &e, const QString &operation)
{
    const QString msg = QStringLiteral("LevelDB exception in \"%1\": %2").arg(operation).arg(e.what());
    qCWarning(LANGUAGE) << msg;
    if (qEnvironmentVariableIsSet("KDEV_TOPCONTEXTS_STORE_FAILURE_ABORT")) {
        qFatal(msg.toLatin1());
    }
    return msg;
}

// our keys are uint values and stored as uint* in the original QByteArray m_currentKey
// use that knowledge to avoid sorting lexographically.
class QuickSortingComparator : public leveldb::Comparator
{
public:
    // Comparison function that doesn't result in any sorting
    int Compare(const leveldb::Slice &a, const leveldb::Slice &b) const
    {
        const uint *ia = reinterpret_cast<const uint *>(a.data());
        const uint *ib = reinterpret_cast<const uint *>(b.data());
        return *ia - *ib;
    }

    const char *Name() const
    {
        return "QuickSortingComparator";
    }
    void FindShortestSeparator(std::string *, const leveldb::Slice &) const {}
    void FindShortSuccessor(std::string *) const {}
};
static QuickSortingComparator leveldbComparator;

// there is exactly 1 topcontexts directory per session, so we can make do with a single
// global static LevelDB instance which is not exported at all (= no need to include
// db.h in our own headerfile).

class LevelDBHook
{
public:
    ~LevelDBHook()
    {
        if (s_levelDB) {
            delete s_levelDB;
        }
    }

    static const leveldb::Status init()
    {
        leveldb::Options options;
        options.create_if_missing = true;
        options.comparator = &leveldbComparator;
        int attempts = 0;
        leveldb::Status status;
        do {
            attempts += 1;
            leveldb::Status status = leveldb::DB::Open(options, TopDUContextDynamicData::basePath().toStdString(), &s_levelDB);
            if (!status.ok()) {
                s_levelDB = nullptr;
                if (status.IsInvalidArgument()) {
                    // retry opening a fresh store
                    leveldb::DestroyDB(TopDUContextDynamicData::basePath().toStdString(), options);
                }
            }
        } while (!s_levelDB && attempts < 2);
        return status;
    }

    inline leveldb::DB *instance()
    {
        return s_levelDB;
    }

    static leveldb::DB *s_levelDB;
};
static LevelDBHook levelDB;

leveldb::DB *LevelDBHook::s_levelDB = nullptr;
uint TopDUContextLevelDB::s_DbRefCount = 0;

TopDUContextLevelDB::TopDUContextLevelDB(uint topContextIndex)
{
    m_currentIndex = topContextIndex;
    if (!levelDB.instance() && Q_LIKELY(QFileInfo(TopDUContextDynamicData::basePath()).isWritable())) {
        leveldb::Status status = LevelDBHook::init();
        if (!status.ok()) {
            m_errorString = QStringLiteral("Error opening LevelDB database:") + QString::fromStdString(status.ToString());
            qCWarning(LANGUAGE) << m_errorString;
        }
    }
    if (levelDB.instance()) {
        m_currentKey = indexKey(&m_currentIndex);
        s_DbRefCount += 1;
    }
    m_currentLen = -1;
    // the remaining member vars are initialised elsewhere intentionally.
}

TopDUContextLevelDB::~TopDUContextLevelDB()
{
    if (levelDB.instance()) {
        s_DbRefCount -= 1;
        if (s_DbRefCount <= 0) {
            // optimisation: don't delete the global DB handle; too many TopDUContextLevelDB
            // instances are created too frequently that are deleted immediately after a
            // single use.
            s_DbRefCount = 0;
        }
    } else {
        s_DbRefCount = 0;
    }
}

bool TopDUContextLevelDB::open(QIODevice::OpenMode mode)
{
    return TopDUContextDB::open(mode, QStringLiteral("LevelDB"));
}

void TopDUContextLevelDB::commit()
{
    if (isValid() && m_mode != MDB_RDONLY) {
        if (m_currentValue.size() != m_currentLen) {
            // m_currentLen is the true size
            qCDebug(LANGUAGE) << "TopDUContextLevelDB index" << QByteArray::number(m_currentIndex) << "internal size mismatch:"
              << m_currentValue.size() << "vs" << m_currentLen;
        }
        leveldb::Status status;
        leveldb::Slice key(m_currentKey.constData(), m_currentKey.size());
        leveldb::Slice value(m_currentValue.constData(), m_currentLen);
        try {
            status = levelDB.instance()->Put(leveldb::WriteOptions(), key, value);
        } catch (const std::exception &e) {
            m_errorString = leveldb_exception_handler(e, QStringLiteral("committing value for ") + QByteArray::number(m_currentIndex));
            qCWarning(LANGUAGE) << m_errorString;
        }
        if (!status.ok()) {
            m_errorString = QStringLiteral("Error committing index ")
              + QByteArray::number(m_currentIndex) + " size " + QString::number(m_currentLen)
              + QStringLiteral(": ") + QString::fromStdString(status.ToString());
            qCWarning(LANGUAGE) << m_errorString;
        }
        m_currentKey.clear();
        m_currentValue.clear();
        m_currentLen = 0;
    }
}

bool TopDUContextLevelDB::flush()
{
    if (isValid() && m_mode != MDB_RDONLY) {
        try {
            levelDB.instance()->CompactRange(nullptr, nullptr);
            return true;
        } catch (const std::exception &e) {
            m_errorString = leveldb_exception_handler(e, QStringLiteral("database flush (CompactRange)"));
            qCWarning(LANGUAGE) << m_errorString;
        }
    }
    return false;
}

bool TopDUContextLevelDB::getCurrentKeyValue()
{
    // we only return false if a read error occurred; if a key doesn't exist
    // m_currentValue will remain empty.
    bool ret = true;
    if (m_currentValue.isEmpty()) {
        // read the key value from storage into cache
        leveldb::Slice key(m_currentKey.constData(), m_currentKey.size());
        std::string value;
        leveldb::Status status = levelDB.instance()->Get(leveldb::ReadOptions(), key, &value);
        if (status.ok()) {
            m_currentValue = QByteArray(value.data(), value.size());
            m_currentLen = value.size();
            m_readCursor = 0;
        } else if (status.IsCorruption() || status.IsIOError() || status.IsNotSupportedError() || status.IsInvalidArgument()) {
            ret = false;
        }
    }
    return ret;
}

bool TopDUContextLevelDB::isValid()
{
    return levelDB.instance();
}

bool TopDUContextLevelDB::currentKeyExists()
{
    return exists(m_currentKey);
}

bool TopDUContextLevelDB::exists(const QByteArray &key)
{
    if (levelDB.instance()) {
        std::string value;
        leveldb::Slice k(key.constData(), key.size());
        leveldb::Status status = levelDB.instance()->Get(leveldb::ReadOptions(), k, &value);
        if (!status.ok() && !status.IsNotFound()) {
            qCWarning(LANGUAGE) << QStringLiteral("Error checking for index") + key
                + QStringLiteral(": ") + QString::fromStdString(status.ToString());
            return false;
        }
        return status.ok();
    }
    return false;
}

bool TopDUContextLevelDB::exists(uint topContextIndex)
{
    return exists(indexKey(&topContextIndex));
}

bool TopDUContextLevelDB::remove(uint topContextIndex)
{
    if (levelDB.instance()) {
        const auto key = indexKey(&topContextIndex);
        leveldb::Slice k(key.constData(), key.size());
        leveldb::Status status = levelDB.instance()->Delete(leveldb::WriteOptions(), k);
        if (!status.ok()) {
            qCWarning(LANGUAGE) << QStringLiteral("Error removing index %1").arg(topContextIndex)
                + QStringLiteral(": ") + QString::fromStdString(status.ToString());
            return false;
        }
        // also remove the file if it (still) exists
        QFile::remove(TopDUContextDynamicData::pathForTopContext(topContextIndex));
        return true;
    }
    return false;
}

#endif // KDEV_TOPCONTEXTS_USE_LEVELDB

#ifdef KDEV_TOPCONTEXTS_USE_KYOTO
using namespace kyotocabinet;

static QString kyotocabinet_exception_handler(const std::exception &e, const QString &operation)
{
    const QString msg = QStringLiteral("KyotoCabinet exception in \"%1\": %2").arg(operation).arg(e.what());
    qCWarning(LANGUAGE) << msg;
    if (qEnvironmentVariableIsSet("KDEV_TOPCONTEXTS_STORE_FAILURE_ABORT")) {
        qFatal(msg.toLatin1());
    }
    return msg;
}

// there is exactly 1 topcontexts directory per session, which is fine because we can only use a single
// global static KyotoCabinet instance anyway. Wrap it in a local class which is not exported at all
// (= no need to include kcpolydb.h in our own headerfile).

class KyotoCabinetHook
{
public:
    ~KyotoCabinetHook()
    {
        if (s_kyotoCab) {
            try {
                if (!s_kyotoCab->close()) {
                    qCWarning(LANGUAGE) << "Error closing KyotoCabinet database:" << errorString();
                }
                delete s_kyotoCab;
            } catch (const std::exception &e) {
                qCWarning(LANGUAGE) << kyotocabinet_exception_handler(e, QStringLiteral("closing cabinet.kch"));
                // don't delete s_kyotoCab if something went wrong closing the DB
            }
        }
    }

    static bool init()
    {
        s_kyotoCab = new PolyDB;
        bool ok = false;
        s_errorString.clear();
        try {
            int attempts = 0;
            uint32_t flags = PolyDB::OWRITER | PolyDB::OCREATE;
            do {
                attempts += 1;
                // for logging, append
                // #log=+#logkinds=debug
                // for compression (ZLIB deflate), append
                // #opts=sc#zcomp=def
                ok = s_kyotoCab->open(TopDUContextDynamicData::basePath().toStdString() + "cabinet.kch#opts=sc#zcomp=lzo", flags);
                if (!ok && s_kyotoCab->error().code() == BasicDB::Error::INVALID) {
                    // try by recreating the database
                    flags = PolyDB::OWRITER | PolyDB::OTRUNCATE;
                }
            } while (!ok && attempts < 2);
        } catch (const std::exception &e) {
            s_errorString = kyotocabinet_exception_handler(e, QStringLiteral("opening cabinet.kch"));
            ok = false;
        }
        if (!ok) {
            if (s_errorString.isEmpty()) {
                s_errorString = QStringLiteral("Error opening KyotoCabinet database:")
                                + errorString();
            }
            delete s_kyotoCab;
            s_kyotoCab = nullptr;
        }
        return s_kyotoCab;
    }

    inline PolyDB *instance()
    {
        return s_kyotoCab;
    }

    static inline QString errorString()
    {
        return QString::fromStdString(s_kyotoCab->error().name());
    }

    static PolyDB *s_kyotoCab;
    static QString s_errorString;
};
static KyotoCabinetHook kyotoCabinet;

PolyDB *KyotoCabinetHook::s_kyotoCab = nullptr;
QString KyotoCabinetHook::s_errorString;

uint TopDUContextKyotoCabinet::s_DbRefCount = 0;

TopDUContextKyotoCabinet::TopDUContextKyotoCabinet(uint topContextIndex)
{
    m_currentIndex = topContextIndex;
    if (!kyotoCabinet.instance() && Q_LIKELY(QFileInfo(TopDUContextDynamicData::basePath()).isWritable())) {
        if (!kyotoCabinet.init()) {
            m_errorString = kyotoCabinet.s_errorString;
            qCWarning(LANGUAGE) << m_errorString;
        }
    }
    if (kyotoCabinet.instance()) {
        m_currentKey = indexKey(&m_currentIndex);
        s_DbRefCount += 1;
    }
    m_currentLen = -1;
    // the remaining member vars are initialised elsewhere intentionally.
}

TopDUContextKyotoCabinet::~TopDUContextKyotoCabinet()
{
    if (kyotoCabinet.instance()) {
        s_DbRefCount -= 1;
        if (s_DbRefCount <= 0) {
            // optimisation: don't delete the global DB handle; too many TopDUContextKyotoCabinet
            // instances are created too frequently that are deleted immediately after a
            // single use.
            s_DbRefCount = 0;
        }
#ifdef DEBUG
        kyotoCabinet.instance()->synchronize(s_DbRefCount == 0);
#endif
    } else {
        s_DbRefCount = 0;
    }
}

QString TopDUContextKyotoCabinet::fileName() const
{
    return TopDUContextDynamicData::basePath() + "cabinet.kch:#" + QByteArray::number(m_currentIndex);
}

bool TopDUContextKyotoCabinet::open(QIODevice::OpenMode mode)
{
    return TopDUContextDB::open(mode, QStringLiteral("KyotoCabinet"));
}

void TopDUContextKyotoCabinet::commit()
{
    if (isValid() && m_mode != MDB_RDONLY) {
        if (m_currentValue.size() != m_currentLen) {
            // m_currentLen is the true size
            qCDebug(LANGUAGE) << "TopDUContextKyotoCabinet index" << QByteArray::number(m_currentIndex) << "internal size mismatch:"
              << m_currentValue.size() << "vs" << m_currentLen;
        }
        try {
            if (!kyotoCabinet.instance()->set(m_currentKey.constData(), m_currentKey.size(),
                  m_currentValue.constData(), m_currentLen)) {
                m_errorString = QStringLiteral("Error committing index ")
                    + QByteArray::number(m_currentIndex) + " size " + QString::number(m_currentLen)
                    + QStringLiteral(": ") + kyotoCabinet.errorString();
                qCWarning(LANGUAGE) << m_errorString;
            }
        } catch (const std::exception &e) {
            m_errorString = kyotocabinet_exception_handler(e, QStringLiteral("committing value for ") + QByteArray::number(m_currentIndex));
            qCWarning(LANGUAGE) << m_errorString;
        }
        m_currentKey.clear();
        m_currentValue.clear();
        m_currentLen = 0;
    }
}

bool TopDUContextKyotoCabinet::flush()
{
    if (isValid() && m_mode != MDB_RDONLY) {
        try {
            return kyotoCabinet.instance()->synchronize(true);
        } catch (const std::exception &e) {
            m_errorString = kyotocabinet_exception_handler(e, QStringLiteral("database flush"));
            qCWarning(LANGUAGE) << m_errorString;
        }
    }
    return false;
}

bool TopDUContextKyotoCabinet::getCurrentKeyValue()
{
    // we only return false if a read error occurred; if a key doesn't exist
    // m_currentValue will remain empty.
    bool ret = true;
    if (m_currentValue.isEmpty()) {
        // read the key value from storage into cache
        size_t size;
        try {
            const char *value = kyotoCabinet.instance()->get(m_currentKey.constData(), m_currentKey.size(), &size);
            if (value) {
                m_currentValue = QByteArray(value, size);
                m_currentLen = m_currentValue.size();
                m_readCursor = 0;
                delete[] value;
            } else {
                qCWarning(LANGUAGE) << "read NULL for index" << m_currentIndex << "; exists=" << currentKeyExists();
            }
        } catch (const std::exception &e) {
            m_errorString = kyotocabinet_exception_handler(e, QStringLiteral("reading value for ") + QByteArray::number(m_currentIndex));
            qCWarning(LANGUAGE) << m_errorString;
            ret = false;
        }
    }
    return ret;
}

bool TopDUContextKyotoCabinet::isValid()
{
    return kyotoCabinet.instance();
}

bool TopDUContextKyotoCabinet::currentKeyExists()
{
    return exists(m_currentKey);
}

bool TopDUContextKyotoCabinet::exists(const QByteArray &key)
{
    if (kyotoCabinet.instance()) {
        try {
            bool found = kyotoCabinet.instance()->check(key.constData(), key.size()) >= 0;
            if (!found && kyotoCabinet.instance()->error().code() != BasicDB::Error::NOREC) {
                qCWarning(LANGUAGE) << QStringLiteral("Error checking for index") + key
                    + QStringLiteral(": ") + kyotoCabinet.errorString();
            }
            return found;
        } catch (const std::exception &e) {
            qCWarning(LANGUAGE) << kyotocabinet_exception_handler(e, QStringLiteral("checking presence of ") + key);
        }
    }
    return false;
}

bool TopDUContextKyotoCabinet::exists(uint topContextIndex)
{
    return exists(indexKey(&topContextIndex));
}

bool TopDUContextKyotoCabinet::remove(uint topContextIndex)
{
    if (kyotoCabinet.instance()) {
        const auto key = indexKey(&topContextIndex);
        try {
            bool ret = kyotoCabinet.instance()->remove(key.constData(), key.size());
            if (!ret) {
                qCWarning(LANGUAGE) << QStringLiteral("Error removing index %1").arg(topContextIndex)
                    + QStringLiteral(": ") + kyotoCabinet.errorString();
            }
            // also remove the file if it (still) exists
            QFile::remove(TopDUContextDynamicData::pathForTopContext(topContextIndex));
            return ret;
        } catch (const std::exception &e) {
            qCWarning(LANGUAGE) << kyotocabinet_exception_handler(e, QStringLiteral("removing ") + QByteArray::number(topContextIndex));
        }
    }
    return false;
}
#endif // KDEV_TOPCONTEXTS_USE_KYOTO

#endif // KDEV_TOPCONTEXTS_USE_FILES

// TopDUContextFile : thin wrapper around the QFile API needed for TopDUContexts
// so TopDUContextLMDB can be used as a drop-in replacement instead of this class.
TopDUContextFile::TopDUContextFile(uint topContextIndex)
    : QFile(TopDUContextDynamicData::pathForTopContext(topContextIndex))
{
}

bool TopDUContextFile::exists(uint topContextIndex)
{
    return QFile::exists(TopDUContextDynamicData::pathForTopContext(topContextIndex));
}

bool TopDUContextFile::remove(uint topContextIndex)
{
    return QFile::remove(TopDUContextDynamicData::pathForTopContext(topContextIndex));
}

void TopDUContextFile::commit()
{
    QFile::close();
}
