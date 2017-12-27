/*
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *  Copyright (C) 2011 Felix Geyer <debfx@fobos.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "TestKeys.h"

#include <QBuffer>
#include <QTest>

#include "config-keepassx-tests.h"

#include "core/Database.h"
#include "core/Metadata.h"
#include "core/Tools.h"
#include "crypto/Crypto.h"
#include "crypto/CryptoHash.h"
#include "format/KeePass2Reader.h"
#include "format/KeePass2Writer.h"
#include "keys/FileKey.h"
#include "keys/PasswordKey.h"

QTEST_GUILESS_MAIN(TestKeys)
Q_DECLARE_METATYPE(FileKey::Type);

void TestKeys::initTestCase()
{
    QVERIFY(Crypto::init());
}

void TestKeys::testComposite()
{
    QScopedPointer<CompositeKey> compositeKey1(new CompositeKey());
    QScopedPointer<PasswordKey> passwordKey1(new PasswordKey());
    QScopedPointer<PasswordKey> passwordKey2(new PasswordKey("test"));
    bool ok;
    QString errorString;

    // make sure that addKey() creates a copy of the keys
    compositeKey1->addKey(*passwordKey1);
    compositeKey1->addKey(*passwordKey2);

    QByteArray transformed = compositeKey1->transform(QByteArray(32, '\0'), 1, &ok, &errorString);
    QVERIFY(ok);
    QCOMPARE(transformed.size(), 32);

    // make sure the subkeys are copied
    QScopedPointer<CompositeKey> compositeKey2(compositeKey1->clone());
    QCOMPARE(compositeKey2->transform(QByteArray(32, '\0'), 1, &ok, &errorString), transformed);
    QVERIFY(ok);

    QScopedPointer<CompositeKey> compositeKey3(new CompositeKey());
    QScopedPointer<CompositeKey> compositeKey4(new CompositeKey());

    // test clear()
    compositeKey3->addKey(PasswordKey("test"));
    compositeKey3->clear();
    QCOMPARE(compositeKey3->rawKey(), compositeKey4->rawKey());

    // test assignment operator
    compositeKey3->addKey(PasswordKey("123"));
    *compositeKey4 = *compositeKey3;
    QCOMPARE(compositeKey3->rawKey(), compositeKey4->rawKey());

    // test self-assignment
    *compositeKey3 = *compositeKey3;
    QCOMPARE(compositeKey3->rawKey(), compositeKey4->rawKey());
}

void TestKeys::testFileKey()
{
    QFETCH(FileKey::Type, type);
    QFETCH(QString, typeString);

    QString name = QString("FileKey").append(typeString);

    KeePass2Reader reader;

    QString dbFilename = QString("%1/%2.kdbx").arg(QString(KEEPASSX_TEST_DATA_DIR), name);
    QString keyFilename = QString("%1/%2.key").arg(QString(KEEPASSX_TEST_DATA_DIR), name);

    CompositeKey compositeKey;
    FileKey fileKey;
    QVERIFY(fileKey.load(keyFilename));
    QCOMPARE(fileKey.rawKey().size(), 32);

    QCOMPARE(fileKey.type(), type);

    compositeKey.addKey(fileKey);

    QScopedPointer<Database> db(reader.readDatabase(dbFilename, compositeKey));
    QVERIFY(db);
    QVERIFY(!reader.hasError());
    QCOMPARE(db->metadata()->name(), QString("%1 Database").arg(name));
}

void TestKeys::testFileKey_data()
{
    QTest::addColumn<FileKey::Type>("type");
    QTest::addColumn<QString>("typeString");
    QTest::newRow("Xml")             << FileKey::KeePass2XML    << QString("Xml");
    QTest::newRow("XmlBrokenBase64") << FileKey::Hashed         << QString("XmlBrokenBase64");
    QTest::newRow("Binary")          << FileKey::FixedBinary    << QString("Binary");
    QTest::newRow("Hex")             << FileKey::FixedBinaryHex << QString("Hex");
    QTest::newRow("Hashed")          << FileKey::Hashed         << QString("Hashed");
}

void TestKeys::testCreateFileKey()
{
    QBuffer keyBuffer1;
    keyBuffer1.open(QBuffer::ReadWrite);

    FileKey::create(&keyBuffer1, 128);
    QCOMPARE(keyBuffer1.size(), 128);

    QBuffer keyBuffer2;
    keyBuffer2.open(QBuffer::ReadWrite);
    FileKey::create(&keyBuffer2, 64);
    QCOMPARE(keyBuffer2.size(), 64);
}

void TestKeys::testCreateAndOpenFileKey()
{
    const QString dbName("testCreateFileKey database");

    QBuffer keyBuffer;
    keyBuffer.open(QBuffer::ReadWrite);

    FileKey::create(&keyBuffer);
    keyBuffer.reset();

    FileKey fileKey;
    QVERIFY(fileKey.load(&keyBuffer));
    CompositeKey compositeKey;
    compositeKey.addKey(fileKey);

    QScopedPointer<Database> dbOrg(new Database());
    QVERIFY(dbOrg->setKey(compositeKey));
    dbOrg->metadata()->setName(dbName);

    QBuffer dbBuffer;
    dbBuffer.open(QBuffer::ReadWrite);

    KeePass2Writer writer;
    writer.writeDatabase(&dbBuffer, dbOrg.data());
    dbBuffer.reset();

    KeePass2Reader reader;
    QScopedPointer<Database> dbRead(reader.readDatabase(&dbBuffer, compositeKey));
    QVERIFY(dbRead);
    QVERIFY(!reader.hasError());
    QCOMPARE(dbRead->metadata()->name(), dbName);
}

void TestKeys::testFileKeyHash()
{
    QBuffer keyBuffer;
    keyBuffer.open(QBuffer::ReadWrite);

    FileKey::create(&keyBuffer);

    CryptoHash cryptoHash(CryptoHash::Sha256);
    cryptoHash.addData(keyBuffer.data());

    FileKey fileKey;
    fileKey.load(&keyBuffer);

    QCOMPARE(fileKey.rawKey(), cryptoHash.result());
}

void TestKeys::testFileKeyError()
{
    bool result;
    QString errorMsg;
    const QString fileName(QString(KEEPASSX_TEST_DATA_DIR).append("/does/not/exist"));

    FileKey fileKey;
    result = fileKey.load(fileName, &errorMsg);
    QVERIFY(!result);
    QVERIFY(!errorMsg.isEmpty());
    errorMsg = "";

    result = FileKey::create(fileName, &errorMsg);
    QVERIFY(!result);
    QVERIFY(!errorMsg.isEmpty());
    errorMsg = "";
}

void TestKeys::benchmarkTransformKey()
{
    QByteArray env = qgetenv("BENCHMARK");

    if (env.isEmpty() || env == "0" || env == "no") {
        QSKIP("Benchmark skipped. Set env variable BENCHMARK=1 to enable.");
    }

    PasswordKey pwKey;
    pwKey.setPassword("password");
    CompositeKey compositeKey;
    compositeKey.addKey(pwKey);

    QByteArray seed(32, '\x4B');

    bool ok;
    QString errorString;

    QBENCHMARK {
        compositeKey.transform(seed, 1e6, &ok, &errorString);
    }
}
