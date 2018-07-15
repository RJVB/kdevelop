/* This is part of KDevelop
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

#ifndef KDEVPLATFORM_TOPDUCONTEXTDYNAMICDATA_P_H
#define KDEVPLATFORM_TOPDUCONTEXTDYNAMICDATA_P_H

#include <QByteArray>
#include <QFile>

#include <languageexport.h>

namespace KDevelop {

// #define KDEV_TOPCONTEXTS_DB_TESTING
// #define KDEV_TOPCONTEXTS_USE_FILES
#define KDEV_TOPCONTEXTS_USE_LMDB
// #define KDEV_TOPCONTEXTS_USE_LEVELDB
// #define KDEV_TOPCONTEXTS_USE_KYOTO

// thin wrapper around QFile, implementing the default TopDUContext
// storage mechanism but also used for migration purposes in the
// database stores.
class KDEVPLATFORMLANGUAGE_EXPORT TopDUContextFile : public QFile
{
public:
    TopDUContextFile(uint topContextIndex);
    void commit();
    static bool exists(uint topContextIndex);
    static bool remove(uint topContextIndex);
};

#if defined(KDEV_TOPCONTEXTS_USE_LMDB) || defined(KDEV_TOPCONTEXTS_USE_LEVELDB) || defined(KDEV_TOPCONTEXTS_USE_KYOTO)
class KDEVPLATFORMLANGUAGE_EXPORT TopDUContextDB
{
public:
  virtual ~TopDUContextDB() {};
  virtual bool open(QIODevice::OpenMode mode) = 0;
  virtual void commit() = 0;
  virtual bool flush() = 0;
  bool resize(qint64);
  qint64 write(const char* data, qint64 len);
  qint64 read(char* data, qint64 maxSize);
  QByteArray read(qint64 maxSize);
  QByteArray readAll();
  qint64 pos() const;
  bool seek(qint64 pos);
  qint64 size();
  QString errorString() const;
  QString fileName() const;
  static bool exists(uint) { return false; };
  static bool remove(uint) { return false; };

protected:
  bool open(QIODevice::OpenMode mode, const QString &backendName);
  virtual bool getCurrentKeyValue() = 0;
  virtual bool isValid() = 0;
  virtual bool currentKeyExists() = 0;
  static bool exists(const QByteArray&) { return false; };
  static QByteArray indexKey(uint idx);
  static QByteArray indexKey(uint* idx);
  bool migrateFromFile();
  QByteArray m_currentKey;
  uint m_currentIndex;
  QByteArray m_currentValue;
  qint64 m_currentLen, m_readCursor;
  int m_mode;
  QString m_errorString;
};
#endif

#ifdef KDEV_TOPCONTEXTS_USE_LMDB
class KDEVPLATFORMLANGUAGE_EXPORT TopDUContextLMDB : public TopDUContextDB
{
public:
  typedef union { char *bytes; qint32 *qint32Ptr; } LZ4Frame;
  TopDUContextLMDB(uint topContextIndex);
  virtual ~TopDUContextLMDB();
  bool open(QIODevice::OpenMode mode) override;
  void commit() override;
  bool flush() override;
  QString fileName() const;
  static bool exists(uint topContextIndex);
  static bool remove(uint topContextIndex);

protected:
  virtual bool getCurrentKeyValue() override;
  virtual bool isValid() override;
  virtual bool currentKeyExists() override;
  static bool exists(const QByteArray& key);
  static uint s_DbRefCount;
};
#endif

#ifdef KDEV_TOPCONTEXTS_USE_LEVELDB
class KDEVPLATFORMLANGUAGE_EXPORT TopDUContextLevelDB : public TopDUContextDB
{
public:
  TopDUContextLevelDB(uint topContextIndex);
  virtual ~TopDUContextLevelDB();
  bool open(QIODevice::OpenMode mode) override;
  void commit() override;
  bool flush() override;
  static bool exists(uint topContextIndex);
  static bool remove(uint topContextIndex);

protected:
  virtual bool getCurrentKeyValue() override;
  virtual bool isValid() override;
  virtual bool currentKeyExists() override;
  static bool exists(const QByteArray& key);
  static uint s_DbRefCount;
};
#endif

#ifdef KDEV_TOPCONTEXTS_USE_KYOTO
class KDEVPLATFORMLANGUAGE_EXPORT TopDUContextKyotoCabinet : public TopDUContextDB
{
public:
  TopDUContextKyotoCabinet(uint topContextIndex);
  virtual ~TopDUContextKyotoCabinet();
  bool open(QIODevice::OpenMode mode) override;
  void commit() override;
  bool flush() override;
  QString fileName() const;
  static bool exists(uint topContextIndex);
  static bool remove(uint topContextIndex);

protected:
  virtual bool getCurrentKeyValue() override;
  virtual bool isValid() override;
  virtual bool currentKeyExists() override;
  static bool exists(const QByteArray& key);
  static uint s_DbRefCount;
};
#endif

}
#endif
