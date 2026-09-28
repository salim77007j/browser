// downloads.cpp
#include "downloads.h"
#include "app.h"
#include "kestrel_core.h"

#include <QWebEngineDownloadRequest>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static KestrelCore *core() {
    KestrelApp *a = KestrelApp::instance();
    return a ? static_cast<KestrelCore *>(a->core()) : nullptr;
}

static bool isRisky(const QString &filename) {
    static const QStringList risky = {"exe", "msi", "bat", "cmd", "scr", "com", "ps1",
                                      "vbs", "jar", "app", "deb", "rpm", "sh", "dll"};
    const QString ext = QFileInfo(filename).suffix().toLower();
    return risky.contains(ext);
}

Downloads::Downloads(QObject *parent) : QObject(parent) {}

void Downloads::handleRequested(QWebEngineDownloadRequest *request) {
    KestrelApp *app = KestrelApp::instance();
    KestrelCore *c = core();
    if (!c || !request) return;

    const QString suggestedName = QFileInfo(request->downloadFileName()).fileName();
    const QString url = request->url().toString();
    const QString mime = request->mimeType();

    const QString id = QStringLiteral("dl_%1_%2").arg(++m_counter).arg(QDateTime::currentMSecsSinceEpoch());

    QString dir = app->getSetting("download_dir",
                                  QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    QString filename = suggestedName;

    if (app->getSetting("ask_download_path", "0") == "1") {
        const QString path = QFileDialog::getSaveFileName(nullptr, tr("Save file"),
                                                          QDir(dir).filePath(filename));
        if (path.isEmpty()) {
            request->cancel();
            return;
        }
        const QFileInfo fi(path);
        dir = fi.absolutePath();
        filename = fi.fileName();
    }

    if (app->getSetting("warn_risky_downloads", "1") == "1" && isRisky(filename)) {
        const QMessageBox::StandardButton r = QMessageBox::question(
            nullptr, tr("Risky download"),
            tr("\"%1\" is an executable file type.\n\nKeep the download?").arg(filename),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) {
            request->cancel();
            return;
        }
    }

    request->setDownloadDirectory(dir);
    request->setDownloadFileName(filename);
    request->accept();

    const qint64 total = request->totalBytes();
    kestrel_download_add(c, id.toUtf8().constData(), url.toUtf8().constData(),
                         QDir(dir).filePath(filename).toUtf8().constData(),
                         filename.toUtf8().constData(), mime.toUtf8().constData(), total);
    m_active.insert(id, request);

    connect(request, &QWebEngineDownloadRequest::receivedBytesChanged, this, [this, request, id, c]() {
        kestrel_download_update(c, id.toUtf8().constData(), request->receivedBytes(),
                                request->totalBytes(), "in_progress");
        emit progressChanged();
    });
    connect(request, &QWebEngineDownloadRequest::stateChanged, this,
            [this, request, id, c, filename](QWebEngineDownloadRequest::DownloadState state) {
        switch (state) {
            case QWebEngineDownloadRequest::DownloadCompleted:
                kestrel_download_update(c, id.toUtf8().constData(),
                                        request->receivedBytes(), request->totalBytes(), "completed");
                m_active.remove(id);
                break;
            case QWebEngineDownloadRequest::DownloadInterrupted:
                kestrel_download_update(c, id.toUtf8().constData(),
                                        request->receivedBytes(), request->totalBytes(), "interrupted");
                m_active.remove(id);
                break;
            case QWebEngineDownloadRequest::DownloadCancelled:
                kestrel_download_update(c, id.toUtf8().constData(),
                                        request->receivedBytes(), request->totalBytes(), "cancelled");
                m_active.remove(id);
                break;
            default:
                break;
        }
        emit progressChanged();
    });
    emit progressChanged();
}

void Downloads::openById(const QString &id) {
    KestrelCore *c = core();
    if (!c) return;
    char *v = kestrel_downloads_json(c, 200);
    if (!v) return;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(v));
    kestrel_string_free(v);
    for (const auto &e : doc.array()) {
        const QJsonObject o = e.toObject();
        if (o.value("id").toString() == id) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(o.value("path").toString()));
            return;
        }
    }
}
