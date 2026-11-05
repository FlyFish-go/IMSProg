#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QMessageBox>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <QElapsedTimer>
#define private public
#include "mainwindow.h"
#undef private
#include <cstdio>
#include <cstdlib>

class FinishedReply : public QNetworkReply {
public:
    explicit FinishedReply(bool fail) {
        if (fail) setError(QNetworkReply::ConnectionRefusedError, "simulated failure");
        setFinished(true);
    }
    void abort() override {}
    qint64 readData(char *, qint64) override { return -1; }
};

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static QByteArray read(const QString &path) {
    QFile file(path); check(file.open(QIODevice::ReadOnly), "open database"); return file.readAll();
}
static void write(const QString &path, const QByteArray &data) {
    QFile file(path); check(file.open(QIODevice::WriteOnly), "create fixture");
    check(file.write(data) == data.size(), "write fixture");
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    check(argc == 2, "database fixture argument");
    const QByteArray valid = read(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    const QString destination = directory.filePath("IMSProg.Dat");
    app.setProperty("app/userChipDatabaseFile", destination);
    MainWindow window;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
    });
    dismiss.start(1);
    const QByteArray original("existing database must survive failures");
    auto finish = [&](const QByteArray &payload, bool networkError = false) {
        window.tempFile = new QTemporaryFile(&window);
        check(window.tempFile->open(), "open downloaded fixture");
        check(window.tempFile->write(payload) == payload.size(), "stage download");
        window.totalBytesReceived = payload.size();
        window.currentReply = new FinishedReply(networkError);
        window.onFinished();
        check(!window.currentReply && !window.tempFile, "download resources cleaned up");
    };
    write(destination, original);
    finish(valid);
    check(read(destination) == valid, "valid upstream-format database replaces original");
    for (const QByteArray &invalid : {QByteArray("short"), valid.left(valid.size()-1),
                                     QByteArray(valid.size(), 'x')}) {
        write(destination, original);
        finish(invalid);
        check(read(destination) == original, "invalid download preserves original");
    }
    write(destination, original);
    finish(valid, true);
    check(read(destination) == original, "network failure preserves original");
    window.fileName = directory.path(); // A directory cannot be replaced by QSaveFile.
    finish(valid);
    check(QFileInfo(directory.path()).isDir() && read(destination) == original,
          "save failure preserves directory and original database");
    std::puts("PASS: updater valid replacement, malformed/truncated download, network/save failures and cleanup");
}
