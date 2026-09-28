// web_page.h — per-tab QWebEnginePage: permissions, certs, security state,
// fingerprint protection scripts, cosmetic filters, devtools, new windows.
#pragma once
#include <QWebEnginePage>
#include <QWebEngineScriptCollection>
#include <QTimer>

class BrowserWindow;

class KestrelPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit KestrelPage(QWebEngineProfile *profile, QObject *parent = nullptr);

    // lifecycle for background freezing
    qint64 lastActiveMs() const { return m_lastActive; }
    void pokeActive() { m_lastActive = QDateTime::currentMSecsSinceEpoch(); }

    bool isPrivate() const { return m_private; }
    void setPrivate(bool p) { m_private = p; }

    // Apply current fingerprint-protection + cosmetic scripts for url
    void applyPrivacyScripts(const QUrl &url);

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;

signals:
    void securityChanged(bool secure, const QString &host);
    void hostFailedTls(const QString &host);

private:
    qint64 m_lastActive = 0;
    bool m_private = false;
};
