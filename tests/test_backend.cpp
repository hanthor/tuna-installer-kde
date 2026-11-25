#include "offline.h"
#include "productname.h"
#include "readiness.h"
#include "recipe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

class BackendTest : public QObject
{
    Q_OBJECT

private slots:
    void recipeDefaults();
    void recipeJsonOmitsOptionalValues();
    void recipeJsonIncludesConfiguredValues();
    void recipeValidation_data();
    void recipeValidation();
    void recipeJsonRoundTrip();
    void productNameParsing_data();
    void productNameParsing();
    void offlineCommandHelpers();
    void readinessStampFormat();
    void readinessStampUnknownPage();
    void readinessStampRejectsBadDir();
    void readinessStampReplacesPrevious();
};

void BackendTest::recipeDefaults()
{
    const Recipe recipe;

    QCOMPARE(recipe.filesystem, QStringLiteral("xfs"));
    QCOMPARE(recipe.encryption.type, QStringLiteral("none"));
    QCOMPARE(recipe.distroID, QStringLiteral("tunaos"));
    QCOMPARE(recipe.hostname, QStringLiteral("tunaos"));
    QVERIFY(recipe.selinuxDisabled);
    QVERIFY(!recipe.liveMode);
}

void BackendTest::recipeJsonOmitsOptionalValues()
{
    const QJsonObject json = Recipe{}.toJson();

    QVERIFY(!json.contains(QStringLiteral("image")));
    QVERIFY(!json.contains(QStringLiteral("targetImgref")));
    QVERIFY(!json.contains(QStringLiteral("bootloader")));
    QVERIFY(!json.contains(QStringLiteral("composeFsBackend")));
    QVERIFY(!json.contains(QStringLiteral("flatpaks")));
    QVERIFY(!json.contains(QStringLiteral("additionalImageStores")));
    QCOMPARE(json.value(QStringLiteral("encryption")).toObject()
                 .value(QStringLiteral("type")).toString(),
             QStringLiteral("none"));
}

void BackendTest::recipeJsonIncludesConfiguredValues()
{
    Recipe recipe;
    recipe.disk = QStringLiteral("/dev/nvme0n1");
    recipe.image = QStringLiteral("ghcr.io/tuna-os/tunaos:latest");
    recipe.targetImgref = QStringLiteral("ghcr.io/tuna-os/tunaos:stable");
    recipe.bootloader = QStringLiteral("systemd");
    recipe.composeFsBackend = true;
    recipe.flatpaks = {QStringLiteral("org.mozilla.firefox")};
    recipe.additionalImageStores = {QStringLiteral("/run/media/oci")};
    recipe.encryption.type = QStringLiteral("luks-passphrase");
    recipe.encryption.passphrase = QStringLiteral("secret");

    const QJsonObject json = recipe.toJson();
    QCOMPARE(json.value(QStringLiteral("disk")).toString(), recipe.disk);
    QCOMPARE(json.value(QStringLiteral("image")).toString(), recipe.image);
    QCOMPARE(json.value(QStringLiteral("targetImgref")).toString(), recipe.targetImgref);
    QCOMPARE(json.value(QStringLiteral("bootloader")).toString(), recipe.bootloader);
    QVERIFY(json.value(QStringLiteral("composeFsBackend")).toBool());
    QCOMPARE(json.value(QStringLiteral("flatpaks")).toArray().first().toString(),
             recipe.flatpaks.first());
    QCOMPARE(json.value(QStringLiteral("additionalImageStores")).toArray().first().toString(),
             recipe.additionalImageStores.first());
    QCOMPARE(json.value(QStringLiteral("encryption")).toObject()
                 .value(QStringLiteral("passphrase")).toString(),
             recipe.encryption.passphrase);
}

void BackendTest::recipeValidation_data()
{
    QTest::addColumn<QString>("disk");
    QTest::addColumn<QString>("image");
    QTest::addColumn<bool>("liveMode");
    QTest::addColumn<QString>("hostname");
    QTest::addColumn<QString>("encryptionType");
    QTest::addColumn<QString>("passphrase");
    QTest::addColumn<QString>("expectedError");

    QTest::newRow("valid-image") << "/dev/vda" << "example/image" << false << "tunaos" << "none" << "" << "";
    QTest::newRow("valid-live") << "/dev/vda" << "" << true << "tunaos" << "none" << "" << "";
    QTest::newRow("missing-disk") << "" << "example/image" << false << "tunaos" << "none" << "" << "No disk selected";
    QTest::newRow("missing-image") << "/dev/vda" << "" << false << "tunaos" << "none" << "" << "No OS image specified";
    QTest::newRow("missing-hostname") << "/dev/vda" << "example/image" << false << "" << "none" << "" << "Hostname is required";
    QTest::newRow("unknown-encryption") << "/dev/vda" << "example/image" << false << "tunaos" << "plain" << "" << "Unknown encryption type: plain";
    QTest::newRow("missing-passphrase") << "/dev/vda" << "example/image" << false << "tunaos" << "luks-passphrase" << "" << "Encryption passphrase is required";
    QTest::newRow("valid-passphrase") << "/dev/vda" << "example/image" << false << "tunaos" << "luks-passphrase" << "secret" << "";
}

void BackendTest::recipeValidation()
{
    QFETCH(QString, disk);
    QFETCH(QString, image);
    QFETCH(bool, liveMode);
    QFETCH(QString, hostname);
    QFETCH(QString, encryptionType);
    QFETCH(QString, passphrase);
    QFETCH(QString, expectedError);

    Recipe recipe;
    recipe.disk = disk;
    recipe.image = image;
    recipe.liveMode = liveMode;
    recipe.hostname = hostname;
    recipe.encryption.type = encryptionType;
    recipe.encryption.passphrase = passphrase;

    QCOMPARE(recipe.validationError(), expectedError);
    QCOMPARE(recipe.isValid(), expectedError.isEmpty());
}

void BackendTest::recipeJsonRoundTrip()
{
    Recipe original;
    original.disk = QStringLiteral("/dev/sda");
    original.filesystem = QStringLiteral("ext4");
    original.image = QStringLiteral("ghcr.io/tuna-os/yellowfin:gnome");
    original.targetImgref = QStringLiteral("ghcr.io/tuna-os/yellowfin:stable");
    original.bootloader = QStringLiteral("systemd");
    original.composeFsBackend = true;
    original.flatpaks = {QStringLiteral("org.gnome.TextEditor"), QStringLiteral("org.kde.kcalc")};
    original.additionalImageStores = {QStringLiteral("/run/media/store")};
    original.distroID = QStringLiteral("tunaos");
    original.hostname = QStringLiteral("custom-host");
    original.encryption.type = QStringLiteral("luks-passphrase");
    original.encryption.passphrase = QStringLiteral("pass");

    const QJsonObject json = original.toJson();
    const Recipe restored = Recipe::fromJson(json);

    QCOMPARE(restored.disk, original.disk);
    QCOMPARE(restored.filesystem, original.filesystem);
    QCOMPARE(restored.image, original.image);
    QCOMPARE(restored.targetImgref, original.targetImgref);
    QCOMPARE(restored.bootloader, original.bootloader);
    QCOMPARE(restored.composeFsBackend, original.composeFsBackend);
    QCOMPARE(restored.flatpaks, original.flatpaks);
    QCOMPARE(restored.additionalImageStores, original.additionalImageStores);
    QCOMPARE(restored.distroID, original.distroID);
    QCOMPARE(restored.hostname, original.hostname);
    QCOMPARE(restored.encryption.type, original.encryption.type);
    QCOMPARE(restored.encryption.passphrase, original.encryption.passphrase);
}

void BackendTest::productNameParsing_data()
{
    QTest::addColumn<QString>("osRelease");
    QTest::addColumn<QString>("expected");

    QTest::newRow("double-quoted") << "NAME=TunaOS\nPRETTY_NAME=\"TunaOS KDE\"\n" << "TunaOS KDE";
    QTest::newRow("single-quoted") << "PRETTY_NAME='TunaOS KDE'\n" << "TunaOS KDE";
    QTest::newRow("unquoted") << "PRETTY_NAME=TunaOS\n" << "TunaOS";
    QTest::newRow("trimmed") << "  PRETTY_NAME=  \"TunaOS\"  \n" << "TunaOS";
    QTest::newRow("empty") << "PRETTY_NAME=\"\"\n" << "";
    QTest::newRow("absent") << "NAME=TunaOS\nID=tunaos\n" << "";
}

void BackendTest::productNameParsing()
{
    QFETCH(QString, osRelease);
    QFETCH(QString, expected);

    QCOMPARE(product::prettyNameFrom(osRelease), expected);
}

void BackendTest::offlineCommandHelpers()
{
    const QStringList cmd = offline::fishermanCommand();
    QVERIFY(!cmd.isEmpty());

    const QStringList raw = {QStringLiteral("echo"), QStringLiteral("test")};
    const QStringList hostWrapped = offline::hostCommand(raw);
    QVERIFY(!hostWrapped.isEmpty());

    if (offline::inFlatpak()) {
        QCOMPARE(hostWrapped.first(), QStringLiteral("flatpak-spawn"));
        QCOMPARE(hostWrapped.at(1), QStringLiteral("--host"));
    } else {
        QCOMPARE(hostWrapped, raw);
    }
}

namespace {
QByteArray readStamp(const QString &dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("tuna-installer-ready")));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

// The smoke test parses this file over SSH, so the exact keys, their order and
// the three-decimal seconds format are a contract.
void BackendTest::readinessStampFormat()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(readiness::writeStamp(dir.path(), QStringLiteral("welcome"), 1700000000500));

    QCOMPARE(readStamp(dir.path()),
             QByteArray("app_id=org.tunaos.InstallerKde\n"
                        "window=ApplicationWindow\n"
                        "signal=frame-swapped\n"
                        "mapped_at=1700000000.500\n"
                        "page=welcome\n"));
}

// A bare `page=` would parse downstream as a real page named "".
void BackendTest::readinessStampUnknownPage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(readiness::writeStamp(dir.path(), QString(), 1700000000500));

    const QByteArray body = readStamp(dir.path());
    QVERIFY(body.endsWith("page=unknown\n"));
    QVERIFY(!body.contains("page=\n"));
}

// Best-effort: an empty or unwritable runtime dir reports failure and leaves
// nothing behind instead of throwing or creating the directory.
void BackendTest::readinessStampRejectsBadDir()
{
    QVERIFY(!readiness::writeStamp(QString(), QStringLiteral("welcome"), 1700000000500));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("missing"));

    QVERIFY(!readiness::writeStamp(missing, QStringLiteral("welcome"), 1700000000500));
    QVERIFY(!QFileInfo::exists(missing));
}

// A second stamp replaces the first rather than appending, and the atomic
// write leaves no temporary file next to it.
void BackendTest::readinessStampReplacesPrevious()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(readiness::writeStamp(dir.path(), QStringLiteral("welcome"), 1700000000500));
    QVERIFY(readiness::writeStamp(dir.path(), QStringLiteral("done"), 1700000001500));

    const QByteArray body = readStamp(dir.path());
    QVERIFY(body.contains("page=done\n"));
    QVERIFY(body.contains("mapped_at=1700000001.500\n"));
    QVERIFY(!body.contains("welcome"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files),
             QStringList{QStringLiteral("tuna-installer-ready")});
}

QTEST_APPLESS_MAIN(BackendTest)

#include "test_backend.moc"
