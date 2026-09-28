// bridge.cpp
#include "bridge.h"
#include "app.h"
#include "kestrel_core.h"
#include "browser_window.h"
#include "downloads.h"

#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>

static KestrelCore *core() {
    KestrelApp *a = KestrelApp::instance();
    return a ? static_cast<KestrelCore *>(a->core()) : nullptr;
}

KestrelBridge::KestrelBridge(QObject *parent) : QObject(parent) {
    connect(KestrelApp::instance(), &KestrelApp::themeChanged, this, &KestrelBridge::themeChanged);
}

QVariant KestrelBridge::dispatch(const QString &method, const QVariantList &args) {
    const QString m = method;
    const auto arg = [&args](int i) -> QVariant { return i < args.size() ? args.at(i) : QVariant(); };

    if (m == "getSettings")       return getSettings();
    if (m == "getStats")          return getStats();
    if (m == "getFilterLists")    return getFilterLists();
    if (m == "getHistory")        return getHistory(arg(0).toString());
    if (m == "getBookmarks")      return getBookmarks();
    if (m == "getDownloads")      return getDownloads();
    if (m == "getPermissions")    return getPermissions();
    if (m == "getSpeedDial")      return getSpeedDial();

    if (m == "setSetting")             setSetting(arg(0).toString(), arg(1).toString());
    else if (m == "setFilterListEnabled") setFilterListEnabled(arg(0).toString(), arg(1).toBool());
    else if (m == "setCustomRules")    setCustomRules(arg(0).toString());
    else if (m == "clearBrowsingData") clearBrowsingData(arg(0).toBool(), arg(1).toBool(), arg(2).toBool(), arg(3).toBool());
    else if (m == "deleteHistoryItem") deleteHistoryItem(arg(0).toString());
    else if (m == "clearHistory")      clearHistory();
    else if (m == "removeBookmark")    removeBookmark(arg(0).toString());
    else if (m == "renameBookmark")    renameBookmark(arg(0).toString(), arg(1).toString());
    else if (m == "removeDownload")    removeDownload(arg(0).toString());
    else if (m == "clearDownloads")    clearDownloads();
    else if (m == "openDownload")      openDownload(arg(0).toString());
    else if (m == "setPermission")     setPermission(arg(0).toString(), arg(1).toString(), arg(2).toString());
    else if (m == "clearPermission")   clearPermission(arg(0).toString(), arg(1).toString());
    else if (m == "addSpeedDial")      addSpeedDial(arg(0).toString(), arg(1).toString());
    else if (m == "removeSpeedDial")   removeSpeedDial(arg(0).toString());
    else if (m == "navigate")          navigate(arg(0).toString());
    else if (m == "search")            search(arg(0).toString());
    else if (m == "newTab")            newTab(arg(0).toString());
    else if (m == "openPrivateWindow") openPrivateWindow();
    else return QVariantMap{{"error", "unknown method"}};

    return QVariantMap{{"ok", true}};
}

QString KestrelBridge::theme() const { return KestrelApp::instance()->theme(); }
QString KestrelBridge::accent() const { return KestrelApp::instance()->accent(); }

QVariantMap KestrelBridge::getSettings() { return KestrelApp::instance()->allSettings(); }

void KestrelBridge::setSetting(const QString &key, const QString &value) {
    KestrelApp::instance()->setSetting(key, value);
    emit statsChanged();
}

QVariantMap KestrelBridge::getStats() {
    QVariantMap out;
    KestrelCore *c = core();
    if (!c) return out;
    out.insert("session", qint64(kestrel_session_block_total(c)));
    char *v = kestrel_stats_json(c);
    if (v) {
        const QJsonObject o = QJsonDocument::fromJson(QByteArray(v)).object();
        kestrel_string_free(v);
        out.insert("total", o.value("total").toVariant());
        const QJsonObject hosts = o.value("byHost").toObject();
        QVariantMap byHost;
        for (auto it = hosts.begin(); it != hosts.end(); ++it)
            byHost.insert(it.key(), it.value().toObject().value("count").toVariant());
        out.insert("byHost", byHost);
    }
    return out;
}

QVariantMap KestrelBridge::getFilterLists() {
    QVariantMap out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_filter_lists_json(c);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        QVariantList lists;
        for (const auto &e : arr) {
            const QJsonObject o = e.toObject();
            QVariantMap m;
            m.insert("id", o.value("id").toString());
            m.insert("name", o.value("name").toString());
            m.insert("description", o.value("description").toString());
            m.insert("enabled", o.value("enabled").toBool());
            lists.append(m);
        }
        out.insert("lists", lists);
    }
    out.insert("customRules", KestrelApp::instance()->getSetting("custom_rules", ""));
    return out;
}

void KestrelBridge::setFilterListEnabled(const QString &id, bool enabled) {
    KestrelCore *c = core();
    if (!c) return;
    kestrel_filter_list_set_enabled(c, id.toUtf8().constData(), enabled ? 1 : 0);
    KestrelApp::instance()->reloadFilters();
}

void KestrelBridge::setCustomRules(const QString &rules) {
    KestrelApp::instance()->setSetting("custom_rules", rules);
}

void KestrelBridge::clearBrowsingData(bool history, bool downloads, bool stats, bool cookies) {
    KestrelCore *c = core();
    if (!c) return;
    if (history) kestrel_history_clear(c);
    if (downloads) kestrel_downloads_clear(c);
    if (stats) kestrel_stats_clear(c);
    if (cookies) {
        KestrelApp::instance()->profile()->cookieStore()->deleteAllCookies();
    }
    emit statsChanged();
}

QVariantList KestrelBridge::getHistory(const QString &search) {
    QVariantList out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_history_query(c, search.toUtf8().constData(), 200);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        for (const auto &e : arr) out.append(e.toVariant());
    }
    return out;
}

void KestrelBridge::deleteHistoryItem(const QString &url) {
    KestrelCore *c = core();
    if (c) kestrel_history_delete(c, url.toUtf8().constData());
}

void KestrelBridge::clearHistory() {
    KestrelCore *c = core();
    if (c) kestrel_history_clear(c);
}

QVariantList KestrelBridge::getBookmarks() {
    QVariantList out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_bookmarks_json(c);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        for (const auto &e : arr) out.append(e.toVariant());
    }
    return out;
}

void KestrelBridge::removeBookmark(const QString &url) {
    KestrelCore *c = core();
    if (c) kestrel_bookmark_remove(c, url.toUtf8().constData());
}

void KestrelBridge::renameBookmark(const QString &url, const QString &title) {
    KestrelCore *c = core();
    if (c) kestrel_bookmark_rename(c, url.toUtf8().constData(), title.toUtf8().constData());
}

QVariantList KestrelBridge::getDownloads() {
    QVariantList out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_downloads_json(c, 100);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        for (const auto &e : arr) out.append(e.toVariant());
    }
    return out;
}

void KestrelBridge::removeDownload(const QString &id) {
    KestrelCore *c = core();
    if (c) kestrel_download_remove(c, id.toUtf8().constData());
    emit downloadsChanged();
}

void KestrelBridge::clearDownloads() {
    KestrelCore *c = core();
    if (c) kestrel_downloads_clear(c);
    emit downloadsChanged();
}

void KestrelBridge::openDownload(const QString &id) {
    KestrelApp::instance()->downloads()->openById(id);
}

QVariantList KestrelBridge::getPermissions() {
    QVariantList out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_permissions_json(c);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        for (const auto &e : arr) out.append(e.toVariant());
    }
    return out;
}

void KestrelBridge::setPermission(const QString &host, const QString &feature, const QString &value) {
    KestrelCore *c = core();
    if (c) kestrel_permission_set(c, host.toUtf8().constData(), feature.toUtf8().constData(), value.toUtf8().constData());
}

void KestrelBridge::clearPermission(const QString &host, const QString &feature) {
    KestrelCore *c = core();
    if (c) kestrel_permission_clear(c, host.toUtf8().constData(), feature.toUtf8().constData());
}

QVariantList KestrelBridge::getSpeedDial() {
    QVariantList out;
    KestrelCore *c = core();
    if (!c) return out;
    char *v = kestrel_speeddial_json(c);
    if (v) {
        const QJsonArray arr = QJsonDocument::fromJson(QByteArray(v)).array();
        kestrel_string_free(v);
        for (const auto &e : arr) out.append(e.toVariant());
    }
    return out;
}

void KestrelBridge::addSpeedDial(const QString &url, const QString &title) {
    KestrelCore *c = core();
    if (c) kestrel_speeddial_add(c, url.toUtf8().constData(), title.toUtf8().constData());
}

void KestrelBridge::removeSpeedDial(const QString &url) {
    KestrelCore *c = core();
    if (c) kestrel_speeddial_remove(c, url.toUtf8().constData());
}

void KestrelBridge::navigate(const QString &url) {
    if (auto *w = KestrelApp::instance()->mainWindow())
        w->openOrNavigate(url);
}

void KestrelBridge::search(const QString &query) {
    if (auto *w = KestrelApp::instance()->mainWindow())
        w->searchFor(query);
}

void KestrelBridge::newTab(const QString &url) {
    KestrelApp::instance()->ensureMainWindow();
    if (auto *w = KestrelApp::instance()->mainWindow()) {
        w->createTab();
        if (!url.isEmpty()) w->openOrNavigate(url);
    }
}

void KestrelBridge::openPrivateWindow() {
    if (auto *w = KestrelApp::instance()->mainWindow())
        w->newPrivateWindow();
}
