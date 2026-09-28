// omnibox.cpp
#include "omnibox.h"
#include "app.h"
#include "kestrel_core.h"

#include <QCompleter>
#include <QStringListModel>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QAbstractItemView>
#include <QPainter>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QJsonObject>

Omnibox::Omnibox(QWidget *parent)
    : QLineEdit(parent) {
    setObjectName("Omnibox");
    setPlaceholderText(tr("Search or enter address"));
    setClearButtonEnabled(false);
    setAttribute(Qt::WA_MacShowFocusRect, false);

    m_model = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(8);
    m_completer->setObjectName("OmniboxCompleter");
    m_completer->popup()->setObjectName("OmniboxCompleter");
    m_completer->popup()->setWindowFlags(m_completer->popup()->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setCompleter(m_completer);

    m_suggestTimer = new QTimer(this);
    m_suggestTimer->setSingleShot(true);
    m_suggestTimer->setInterval(160);
    connect(m_suggestTimer, &QTimer::timeout, this, [this] { networkSuggestions(text()); });

    connect(this, &QLineEdit::textEdited, this, &Omnibox::onEdited);
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &text) {
                // completer rows are "title — url" or plain urls
                QString t = text;
                const int sep = t.lastIndexOf(QStringLiteral(" — "));
                if (sep > 0) t = t.mid(sep + 3);
                QUrl u(t);
                if (u.isValid() && (u.scheme() == "http" || u.scheme() == "https" || u.scheme() == "kestrel"))
                    emit navigateRequested(u);
                else
                    emit searchRequested(t);
            });
}

void Omnibox::focusInEvent(QFocusEvent *e) {
    QLineEdit::focusInEvent(e);
    if (!text().isEmpty()) selectAll();
}

void Omnibox::mousePressEvent(QMouseEvent *e) {
    QLineEdit::mousePressEvent(e);
}

void Omnibox::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        resetToCurrent(QUrl(m_currentUrl));
        clearFocus();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        const QString t = text().trimmed();
        if (t.isEmpty()) return;
        const QUrl u = QUrl::fromUserInput(t);
        const bool looksUrl = t.startsWith("http://") || t.startsWith("https://") ||
                              t.startsWith("kestrel://") || t.startsWith("localhost") ||
                              (t.contains('.') && !t.contains(' ')) ||
                              (t.startsWith("127.") || t == "about:blank");
        if (looksUrl && u.isValid()) {
            emit navigateRequested(u);
        } else {
            emit searchRequested(t);
        }
        clearFocus();
        e->accept();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

void Omnibox::onEdited(const QString &text) {
    if (text.trimmed().isEmpty()) {
        m_model->setStringList({});
        return;
    }
    // Local history + bookmarks via core (synchronous, fast with index)
    KestrelApp *app = KestrelApp::instance();
    if (app->core()) {
        char *res = kestrel_history_query(static_cast<KestrelCore *>(app->core()),
                                          text.toUtf8().constData(), 6);
        QStringList rows;
        if (res) {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(res));
            kestrel_string_free(res);
            for (const auto &v : doc.array()) {
                const QString url = v.toObject().value("url").toString();
                const QString title = v.toObject().value("title").toString();
                rows << (title.isEmpty() ? url : QStringLiteral("%1 — %2").arg(title, url));
            }
        }
        m_model->setStringList(rows);
    }
    m_suggestTimer->start();
    updateModel();
}

void Omnibox::networkSuggestions(const QString &text) {
    if (KestrelApp::instance()->getSetting("search_suggestions", "1") != "1") return;
    QUrl url(QStringLiteral("https://duckduckgo.com/ac/"));
    QUrlQuery q;
    q.addQueryItem("q", text);
    q.addQueryItem("type", "list");
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        // format: [query, [suggestions...]]
        if (doc.isArray() && doc.array().size() == 2) {
            m_netSuggestions = doc.array().at(1).toArray();
        } else {
            m_netSuggestions = QJsonArray();
        }
        suggestionsDone();
    });
}

void Omnibox::suggestionsDone() { updateModel(); }

void Omnibox::updateModel() {
    QStringList rows;
    // net suggestions first (most query-like)
    for (const auto &v : m_netSuggestions) {
        rows << v.toString();
        if (rows.size() >= 5) break;
    }
    const QStringList local = m_model->stringList();
    for (const QString &r : local) {
        if (!rows.contains(r)) rows << r;
        if (rows.size() >= 8) break;
    }
    const QString prefix = text();
    if (!prefix.trimmed().isEmpty() && !rows.isEmpty())
        m_model->setStringList(rows);
    if (!rows.isEmpty()) m_completer->complete();
}

void Omnibox::setUrlState(const QUrl &url, bool secure, bool internal) {
    m_currentUrl = url.toString();
    // show path-only display for http(s)
    if (url.scheme() == "http" || url.scheme() == "https") {
        QString disp = url.toString(QUrl::RemoveScheme | QUrl::RemoveFragment);
        if (url.scheme() == "https" && disp.startsWith("//"))
            disp = disp.mid(1);
        setText(disp);
        setCursorPosition(0);
    } else {
        setText(m_currentUrl);
    }
    setProperty("secure", secure);
    setProperty("internal", internal);
}

void Omnibox::resetToCurrent(const QUrl &url) {
    KestrelApp *app = KestrelApp::instance();
    const bool secure = url.scheme() == "https" || url.scheme() == "kestrel";
    const bool internal = url.scheme() == "kestrel";
    setUrlState(url, secure, internal);
    Q_UNUSED(app);
}
