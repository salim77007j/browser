// request_interceptor.cpp
#include "request_interceptor.h"
#include "app.h"
#include "kestrel_core.h"

#include <QUrl>
#include <QUrlQuery>
#include <QMutexLocker>
#include <cstdio>

static QSet<QString> g_httpsFailed;
static QMutex g_httpsMutex;

void RequestInterceptor::markTlsFailure(const QString &host) {
    QMutexLocker l(&g_httpsMutex);
    g_httpsFailed.insert(host);
}

bool RequestInterceptor::tlsFailed(const QString &host) {
    QMutexLocker l(&g_httpsMutex);
    return g_httpsFailed.contains(host);
}

RequestInterceptor::RequestInterceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent) {}

bool RequestInterceptor::isInternalUrl(const QUrl &url) const {
    return url.scheme() == QStringLiteral("kestrel") ||
           url.scheme() == QStringLiteral("chrome") ||
           url.scheme() == QStringLiteral("devtools") ||
           url.scheme() == QStringLiteral("data") ||
           url.scheme() == QStringLiteral("about") ||
           url.scheme() == QStringLiteral("qrc");
}

QString RequestInterceptor::resourceTypeName(QWebEngineUrlRequestInfo::ResourceType t) {
    using T = QWebEngineUrlRequestInfo::ResourceType;
    switch (t) {
        case T::ResourceTypeMainFrame:      return "document";
        case T::ResourceTypeSubFrame:       return "sub_frame";
        case T::ResourceTypeStylesheet:     return "stylesheet";
        case T::ResourceTypeScript:         return "script";
        case T::ResourceTypeImage:          return "image";
        case T::ResourceTypeFontResource:   return "font";
        case T::ResourceTypeMedia:          return "media";
        case T::ResourceTypeXhr:            return "xhr";
        case T::ResourceTypePrefetch:       return "prefetch";
        case T::ResourceTypePing:           return "ping";
        case T::ResourceTypeCspReport:      return "csp-report";
        case T::ResourceTypeWebSocket:      return "websocket";
        case T::ResourceTypeFavicon:        return "image";
        default:                            return "other";
    }
}

QString RequestInterceptor::stripTrackingParams(const QUrl &url) const {
    static const QStringList kParams = {
        "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
        "utm_id", "utm_name", "utm_cid", "utm_reader", "utm_social", "utm_brand",
        "gclid", "gclsrc", "dclid", "gbraid", "wbraid", "fbclid", "msclkid",
        "twclid", "igshid", "mc_eid", "mc_cid", "_hsenc", "_hsmi", "vero_id",
        "wickedid", "ttclid", "s_kwcid", "elqTrackId", "ml_subscriber", "ml_subscriber_hash",
        "yclid", "_ga", "_gl", "icid", "igshid", "si"
    };
    QUrlQuery q(url);
    bool changed = false;
    QList<QPair<QString, QString>> items = q.queryItems(QUrl::FullyDecoded);
    for (const auto &p : kParams) {
        for (auto it = items.begin(); it != items.end();) {
            if (QString::compare(it->first, p, Qt::CaseInsensitive) == 0) {
                it = items.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    if (!changed) return QString();
    QUrl cleaned = url;
    QUrlQuery nq;
    for (const auto &kv : items) nq.addQueryItem(kv.first, kv.second);
    cleaned.setQuery(nq);
    return cleaned.toString();
}

void RequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info) {
    KestrelApp *app = KestrelApp::instance();
    if (!app || !app->core()) return;

    const QUrl url = info.requestUrl();
    const QString urlStr = url.toString();
    const QString scheme = url.scheme();

    if (isInternalUrl(url)) return;
    if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss") return;

    const bool mainFrame = info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame;
    QUrl initiator = info.initiator();
    const QString sourceStr = initiator.isValid() ? initiator.toString() : QString();
    const bool thirdParty = initiator.isValid() &&
        (initiator.host().isEmpty() || url.host().isEmpty()
            ? initiator.host() != url.host()
            : !url.host().endsWith(initiator.host()) && !initiator.host().endsWith(url.host()));

    /* ---- 1. HTTPS-First (main frame http → https) ----
       Never upgrade localhost / private-network hosts (standard practice). */
    auto isLocalHost = [](const QString &h) {
        return h == "localhost" || h == "127.0.0.1" || h == "::1" || h == "[::1]" ||
               h.endsWith(".local") || h.startsWith("192.168.") || h.startsWith("10.") ||
               h.startsWith("172.");
    };
    if (mainFrame && scheme == "http" && app->getSetting("https_first", "1") == "1" && !isLocalHost(url.host())) {
        if (!tlsFailed(url.host())) {
            QUrl upgraded = url;
            upgraded.setScheme("https");
            info.redirect(upgraded);
            return;
        }
    }

    /* ---- 2. Strip tracking params on navigations ---- */
    if (app->getSetting("strip_tracking_params", "1") == "1" &&
        (mainFrame || info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeSubFrame)) {
        const QString cleaned = stripTrackingParams(url);
        if (!cleaned.isEmpty()) {
            info.redirect(QUrl(cleaned));
            return;
        }
    }

    /* ---- 3. Referer privacy: cross-site → origin only; same-site → full ---- */
    if (initiator.isValid() && !initiator.host().isEmpty() && thirdParty) {
        info.setHttpHeader(QByteArray("Referer"), initiator.toString(QUrl::ComponentFormattingOptions(QUrl::RemoveUserInfo) | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8());
    }

    /* ---- 4. Adblock / tracker blocking (Rust core) ---- */
    if (app->getSetting("blocking_enabled", "1") == "1") {
        const QByteArray typeBa = resourceTypeName(info.resourceType()).toUtf8();
        const QByteArray urlBa = urlStr.toUtf8();
        const QByteArray srcBa = sourceStr.toUtf8();
        if (kestrel_check_url(static_cast<KestrelCore *>(app->core()),
                              urlBa.constData(), srcBa.constData(), typeBa.constData())) {
            info.block(true);
        }
    }
}
