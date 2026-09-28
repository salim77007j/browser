// scheme_handler.cpp
#include "scheme_handler.h"
#include "internal_pages.h"
#include "bridge.h"

#include <QWebEngineUrlRequestJob>
#include <QBuffer>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>
#include <QPointer>

KestrelSchemeHandler::KestrelSchemeHandler(bool isPrivate, QObject *parent)
    : QWebEngineUrlSchemeHandler(parent), m_isPrivate(isPrivate) {}

void KestrelSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job) {
    const QUrl url = job->requestUrl();
    const bool isPrivate = m_isPrivate;

    // Single-origin design: all internal pages live on host "ui".
    // kestrel://ui/<page>  → page HTML
    // kestrel://ui/bridge  → JSON dispatch (fetch bridge)
    // kestrel://ui/logo    → logo svg
    if (url.host() == "ui" || url.host() == "logo" ||
        (!url.host().isEmpty() && url.host() != "bridge")) {
        const QString page = url.host() == "ui" ? url.path().mid(1) : url.host();

        if (page == "bridge" || url.path() == "/bridge") {
            // Parse m= & a= from the query
            const QString query = url.query();
            QString method;
            QByteArray argsRaw = "[]";
            for (const QString &kv : query.split('&')) {
                const int eq = kv.indexOf('=');
                if (eq <= 0) continue;
                const QString k = kv.left(eq);
                const QString v = QUrl::fromPercentEncoding(kv.mid(eq + 1).toUtf8());
                if (k == "m") method = v;
                else if (k == "a") argsRaw = v.toUtf8();
            }
            QVariantList args;
            {
                QJsonParseError err;
                const QJsonDocument adoc = QJsonDocument::fromJson(argsRaw, &err);
                if (!adoc.isNull() && adoc.isArray()) {
                    for (const auto &e : adoc.array()) args.append(e.toVariant());
                }
            }
            static QPointer<KestrelBridge> sharedBridge;
            if (sharedBridge.isNull())
                sharedBridge = new KestrelBridge(this);
            const QVariant result = sharedBridge->dispatch(method, args);
            QBuffer *jbuf = new QBuffer(job);
            jbuf->setData(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));
            jbuf->open(QIODevice::ReadOnly);
            job->reply(QByteArrayLiteral("application/json"), jbuf);
            return;
        }

        if (page == "logo") {
            static const QByteArray logo = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
<stop offset="0" stop-color="#4D9FFF"/><stop offset="1" stop-color="#7C5CFF"/></linearGradient></defs>
<path d="M8 6c14 2 26 8 32 20l16 24c-8-4-16-5-22-4l-10-9C12 24 9 14 8 6z" fill="url(#g)"/>
<path d="M18 10c9 3 17 8 21 17l10 15c-5-2-10-3-14-2L26 32c-6-6-8-14-8-22z" fill="#0E1116" opacity=".35"/>
</svg>)SVG";
            QBuffer *buf = new QBuffer(job);
            buf->setData(logo);
            buf->open(QIODevice::ReadOnly);
            job->reply(QByteArrayLiteral("image/svg+xml"), buf);
            return;
        }

        const QString html = InternalPages::render(QUrl(QStringLiteral("kestrel://") + page), isPrivate);
        QBuffer *buf = new QBuffer(job);
        buf->setData(html.toUtf8());
        buf->open(QIODevice::ReadOnly);
        job->reply(QByteArrayLiteral("text/html"), buf);
        return;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}
