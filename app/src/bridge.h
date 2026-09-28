// bridge.h — QWebChannel object exposed as `kestrel` on all internal pages.
#pragma once
#include <QObject>
#include <QVariantMap>

class KestrelApp;
class BrowserWindow;

class KestrelBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY themeChanged)

public:
    explicit KestrelBridge(QObject *parent = nullptr);
    QString theme() const;
    QString accent() const;

public slots:
    /* generic dispatcher used by the fetch bridge */
    QVariant dispatch(const QString &method, const QVariantList &args);

    /* generic settings */
    QVariantMap getSettings();
    void setSetting(const QString &key, const QString &value);

    /* privacy */
    QVariantMap getStats();               // session total + persisted totals + byHost
    QVariantMap getFilterLists();
    void setFilterListEnabled(const QString &id, bool enabled);
    void setCustomRules(const QString &rules);
    void clearBrowsingData(bool history, bool downloads, bool stats, bool cookies);

    /* history */
    QVariantList getHistory(const QString &search);
    void deleteHistoryItem(const QString &url);
    void clearHistory();

    /* bookmarks */
    QVariantList getBookmarks();
    void removeBookmark(const QString &url);
    void renameBookmark(const QString &url, const QString &title);

    /* downloads */
    QVariantList getDownloads();
    void removeDownload(const QString &id);
    void clearDownloads();
    void openDownload(const QString &id);

    /* permissions */
    QVariantList getPermissions();
    void setPermission(const QString &host, const QString &feature, const QString &value);
    void clearPermission(const QString &host, const QString &feature);

    /* speed dial */
    QVariantList getSpeedDial();
    void addSpeedDial(const QString &url, const QString &title);
    void removeSpeedDial(const QString &url);

    /* navigation from internal pages */
    void navigate(const QString &url);
    void search(const QString &query);
    void newTab(const QString &url);
    void openPrivateWindow();

signals:
    void themeChanged();
    void statsChanged();
    void downloadsChanged();
};
