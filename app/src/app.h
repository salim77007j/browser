// app.h — application singleton: Rust core handle, settings, theme, resources.
#pragma once
#include <QObject>
#include <QHash>
#include <QUrl>

class BrowserWindow;
class QWebEngineProfile;
class QWebEngineSettings;
class Downloads;

class KestrelApp : public QObject {
    Q_OBJECT
public:
    static KestrelApp *instance();

    // Static early-boot helpers (before QApplication exists)
    static void initCore(const QString &profileDir);
    static void shutdownCore();
    static QString setting(const QString &key, const QString &fallback);
    static QString dohTemplate(const QString &id);
    static QString qPlatform();

    explicit KestrelApp(QObject *parent = nullptr);
    ~KestrelApp() override;

    KestrelApp(const KestrelApp &) = delete;
    KestrelApp &operator=(const KestrelApp &) = delete;

    QWebEngineProfile *profile() const { return m_profile; }          // persistent
    QWebEngineProfile *privateProfile() const { return m_private; }   // off-the-record
    Downloads *downloads() const { return m_downloads; }
    BrowserWindow *mainWindow() const { return m_mainWindow; }

    QString profileDir() const;
    void *core() const { return m_core; }   // opaque KestrelCore*

    // Settings API (thread-safe via Rust store)
    void setSetting(const QString &key, const QString &value);
    QString getSetting(const QString &key, const QString &fallback) const;
    QVariantMap allSettings() const;

    void setTheme(const QString &theme, const QString &accent);
    QString theme() const { return m_theme; }
    QString accent() const { return m_accent; }
    bool isDark() const { return m_theme == "dark"; }

    void setPrivateMode(bool on) { m_privateMode = on; }
    bool privateMode() const { return m_privateMode; }

    void reloadFilters();

    // bundled filter list paths
    QStringList filterListPaths() const;
    static constexpr const char *FilterListIds[3] = {"easylist", "easyprivacy", "annoyances"};

    void shutdown();  // save session, flush stats, mark clean exit

    // lazily create the main window (first call constructs)
    BrowserWindow *ensureMainWindow();

signals:
    void themeChanged();
    void settingsChanged(const QString &key, const QString &value);

private:
    void *m_core = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    QWebEngineProfile *m_private = nullptr;
    Downloads *m_downloads = nullptr;
    BrowserWindow *m_mainWindow = nullptr;
    QString m_theme = "dark";
    QString m_accent = "#4D9FFF";
    bool m_privateMode = false;
    QString m_profileDir;

    static KestrelApp *s_instance;
};
