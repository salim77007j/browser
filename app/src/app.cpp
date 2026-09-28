// app.cpp
#include "app.h"
#include "kestrel_core.h"
#include "browser_window.h"
#include "downloads.h"
#include "request_interceptor.h"
#include "scheme_handler.h"
#include "theme.h"

#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>

KestrelApp *KestrelApp::s_instance = nullptr;
static void *g_core = nullptr;          // alive before QApplication
static QString g_profileDir;
constexpr const char *KestrelApp::FilterListIds[3];

KestrelApp *KestrelApp::instance() {
    if (!s_instance) s_instance = new KestrelApp(qApp);
    return s_instance;
}

void KestrelApp::initCore(const QString &profileDir) {
    QDir().mkpath(profileDir);
    g_profileDir = profileDir;
    if (!g_core)
        g_core = kestrel_core_new(profileDir.toUtf8().constData());
}

void KestrelApp::shutdownCore() {
    if (g_core) {
        kestrel_core_free(static_cast<KestrelCore *>(g_core));
        g_core = nullptr;
    }
}

QString KestrelApp::dohTemplate(const QString &id) {
    if (id == "cloudflare") return QStringLiteral("https://cloudflare-dns.com/dns-query");
    if (id == "google") return QStringLiteral("https://dns.google/dns-query");
    if (id == "adguard") return QStringLiteral("https://dns.adguard-dns.com/dns-query");
    if (id == "nextdns") return QStringLiteral("https://dns.nextdns.io");
    return QStringLiteral("https://dns.quad9.net/dns-query");
}

QString KestrelApp::setting(const QString &key, const QString &fallback) {
    if (!g_core) return fallback;
    char *v = kestrel_setting_get(static_cast<KestrelCore *>(g_core), key.toUtf8().constData());
    if (!v) return fallback;
    QString out = QString::fromUtf8(v);
    kestrel_string_free(v);
    return out;
}

KestrelApp::KestrelApp(QObject *parent) : QObject(parent) {
    m_core = g_core;
    m_profileDir = g_profileDir.isEmpty()
        ? QString::fromLocal8Bit(qgetenv("HOME")) + QStringLiteral("/.config/Kestrel")
        : g_profileDir;

    // Persistent profile
    m_profile = new QWebEngineProfile(QStringLiteral("kestrel"), this);
    m_profile->setHttpUserAgent(QStringLiteral(
        "Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, like Gecko) Kestrel/1.0.0 Chrome/132.0.0.0 Safari/537.36")
        .arg(qPlatform()));

    // Privacy defaults at engine level
    QWebEngineSettings *s = m_profile->settings();
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    s->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    s->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);

    // Request interception (ads/trackers, HTTPS-first, referer/param cleaning)
    m_profile->setUrlRequestInterceptor(new RequestInterceptor(this));

    // kestrel:// scheme handler
    m_profile->installUrlSchemeHandler(QByteArrayLiteral("kestrel"), new KestrelSchemeHandler(false, this));

    // Downloads
    m_downloads = new Downloads(this);
    connect(m_profile, &QWebEngineProfile::downloadRequested, m_downloads, &Downloads::handleRequested);

    // Off-the-record profile for private windows
    m_private = new QWebEngineProfile(this);
    m_private->installUrlSchemeHandler(QByteArrayLiteral("kestrel"), new KestrelSchemeHandler(true, this));
    m_private->setUrlRequestInterceptor(new RequestInterceptor(this));
    QWebEngineSettings *ps = m_private->settings();
    ps->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    ps->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);

    // Seed speed dial on first run
    if (getSetting("speeddial_seeded", "0") != "1") {
        const QList<QPair<QString, QString>> defaults = {
            {"https://duckduckgo.com", "DuckDuckGo"},
            {"https://www.wikipedia.org", "Wikipedia"},
            {"https://github.com", "GitHub"},
            {"https://news.ycombinator.com", "Hacker News"},
            {"https://www.reddit.com", "Reddit"},
            {"https://www.youtube.com", "YouTube"},
        };
        if (m_core) {
            for (const auto &d : defaults)
                kestrel_speeddial_add(static_cast<KestrelCore *>(m_core),
                                      d.first.toUtf8().constData(), d.second.toUtf8().constData());
            setSetting("speeddial_seeded", "1");
        }
    }

    // Load filter engine in background of startup
    if (qEnvironmentVariableIsEmpty("KESTREL_NO_FILTERS"))
        reloadFilters();
}

KestrelApp::~KestrelApp() = default;

QString KestrelApp::profileDir() const { return m_profileDir; }

QString KestrelApp::qPlatform() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows NT 10.0; Win64; x64");
#elif defined(Q_OS_MAC)
    return QStringLiteral("Macintosh; Intel Mac OS X 10_15_7");
#else
    return QStringLiteral("X11; Linux x86_64");
#endif
}

void KestrelApp::setSetting(const QString &key, const QString &value) {
    if (!m_core) return;
    kestrel_setting_set(static_cast<KestrelCore *>(m_core), key.toUtf8().constData(), value.toUtf8().constData());
    emit settingsChanged(key, value);
    if (key == "block_ads" || key == "block_trackers" || key == "block_annoyances" || key == "custom_rules")
        reloadFilters();
}

QString KestrelApp::getSetting(const QString &key, const QString &fallback) const {
    if (!m_core) return fallback;
    char *v = kestrel_setting_get(static_cast<KestrelCore *>(m_core), key.toUtf8().constData());
    if (!v) return fallback;
    QString out = QString::fromUtf8(v);
    kestrel_string_free(v);
    return out;
}

QVariantMap KestrelApp::allSettings() const {
    QVariantMap map;
    if (!m_core) return map;
    char *v = kestrel_settings_all(static_cast<KestrelCore *>(m_core));
    if (v) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(v), &err);
        kestrel_string_free(v);
        if (!doc.isNull() && doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                map.insert(it.key(), it.value().toVariant());
        }
    }
    return map;
}

void KestrelApp::setTheme(const QString &theme, const QString &accent) {
    m_theme = theme;
    m_accent = accent;
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setStyleSheet(Theme::buildStyleSheet(theme, accent));
    emit themeChanged();
}

void KestrelApp::reloadFilters() {
    if (!m_core) return;
    // Rust core reads from the filesystem: materialize bundled qrc lists to disk.
    static const char *ids[3] = {"easylist", "easyprivacy", "annoyances"};
    static const char *files[3] = {":/resources/filters/easylist.txt",
                                   ":/resources/filters/easyprivacy.txt",
                                   ":/resources/filters/annoyances.txt"};
    const QString filterDir = m_profileDir + QStringLiteral("/filters");
    QDir().mkpath(filterDir);
    QJsonArray lists;
    for (int i = 0; i < 3; ++i) {
        const bool enabled = getSetting(ids[i], "1") == "1";
        if (!enabled) continue;
        const QString dest = filterDir + QStringLiteral("/") + ids[i] + ".txt";
        QFile src(files[i]);
        if (!QFile::exists(dest) && src.open(QIODevice::ReadOnly)) {
            QFile out(dest);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                out.write(src.readAll());
        }
        if (QFile::exists(dest))
            lists.append(QJsonObject{{"id", ids[i]}, {"path", dest}});
    }
    QJsonObject payload{{"lists", lists}, {"custom", getSetting("custom_rules", "")}};
    const int ok = kestrel_reload_filters(static_cast<KestrelCore *>(m_core),
                           QJsonDocument(payload).toJson(QJsonDocument::Compact).constData());
    fprintf(stderr, "[kestrel] filter engine reload: %d (%d lists)\n", ok, lists.size());
}

QStringList KestrelApp::filterListPaths() const {
    return {":/resources/filters/easylist.txt", ":/resources/filters/easyprivacy.txt",
            ":/resources/filters/annoyances.txt"};
}

BrowserWindow *KestrelApp::ensureMainWindow() {
    if (!m_mainWindow) {
        m_mainWindow = new BrowserWindow();
        connect(m_mainWindow, &QObject::destroyed, this, [this]() { m_mainWindow = nullptr; });
    }
    return m_mainWindow;
}

void KestrelApp::shutdown() {
    if (!m_core) return;
    if (m_mainWindow) m_mainWindow->saveSession();
    kestrel_stats_flush(static_cast<KestrelCore *>(m_core));
    setSetting("clean_exit", "1");
}
