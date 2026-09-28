// request_interceptor.h — adblock + HTTPS-first + referer trim + tracking-param strip
#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QSet>
#include <QMutex>

class RequestInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit RequestInterceptor(QObject *parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

    static QString resourceTypeName(QWebEngineUrlRequestInfo::ResourceType type);

    // HTTPS-first fallback registry (process-wide)
    static void markTlsFailure(const QString &host);
    static bool tlsFailed(const QString &host);

private:
    QString stripTrackingParams(const QUrl &url) const;
    bool isInternalUrl(const QUrl &url) const;
};
