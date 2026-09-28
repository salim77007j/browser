// downloads.h — real download handling via QWebEngineProfile::downloadRequested
#pragma once
#include <QObject>
#include <QHash>

class QWebEngineDownloadRequest;
class BrowserWindow;

class Downloads : public QObject {
    Q_OBJECT
public:
    explicit Downloads(QObject *parent = nullptr);

public slots:
    void handleRequested(QWebEngineDownloadRequest *request);
    void openById(const QString &id);

signals:
    void progressChanged();   // UI can refresh badge

private:
    QHash<QString, QWebEngineDownloadRequest *> m_active;   // id → live request
    int m_counter = 0;
};
