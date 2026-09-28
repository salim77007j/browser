// browser_window.cpp — main window implementation
#include "browser_window.h"
#include "app.h"
#include "kestrel_core.h"
#include "tabstrip.h"
#include "omnibox.h"
#include "web_view.h"
#include "web_page.h"
#include "icons.h"

#include <QTabWidget>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QToolBar>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineCertificateError>
#include <QWebEngineFindTextResult>
#include <QWebEngineNewWindowRequest>
#include <QWebEngineSettings>
#include <QWebEngineDownloadRequest>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QPrinter>
#include <QPrintDialog>
#include <QShortcut>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QMenu>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QDateTime>
#include <QStyle>
#include <QWebEngineHistory>

static KestrelCore *core() {
    KestrelApp *a = KestrelApp::instance();
    return a ? static_cast<KestrelCore *>(a->core()) : nullptr;
}

BrowserWindow::BrowserWindow(QWidget *parent)
    : QMainWindow(parent) {
    setObjectName("RootWindow");
    m_private = KestrelApp::instance()->privateMode();

    setMinimumSize(860, 560);
    resize(1180, 780);
    setWindowIcon(Icons::get("kestralogo", "#4D9FFF", 32));

    // ---- chrome structure: [tab strip row][toolbar][pages][findbar/statusbar] ----
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *stripRow = buildTabStripRow();
    layout->addWidget(stripRow);

    buildToolbar();
    layout->addWidget(m_toolbar);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    // find bar (hidden by default)
    m_findBar = new QWidget(this);
    m_findBar->setObjectName("FindBar");
    QHBoxLayout *fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(10, 6, 10, 6);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find in page"));
    QToolButton *fPrev = new QToolButton(m_findBar);
    QToolButton *fNext = new QToolButton(m_findBar);
    QToolButton *fClose = new QToolButton(m_findBar);
    fPrev->setIcon(Icons::themed("arrowup", 14));
    fNext->setIcon(Icons::themed("forward", 14));
    fClose->setIcon(Icons::themed("close", 14));
    QLabel *fCount = new QLabel(m_findBar);
    fCount->setObjectName("FindCount");
    fl->addWidget(m_findEdit);
    fl->addWidget(fPrev);
    fl->addWidget(fNext);
    fl->addWidget(fCount);
    fl->addWidget(fClose);
    m_findBar->hide();
    layout->addWidget(m_findBar);

    // status bar
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("StatusBar");
    m_statusLabel->setFixedHeight(22);
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel);

    setCentralWidget(central);

    connect(m_strip, &QTabBar::currentChanged, this, [this](int idx) {
        if (idx >= 0 && idx < m_stack->count()) m_stack->setCurrentIndex(idx);
        onCurrentChanged(idx);
    });
    connect(m_strip, &QTabBar::tabCloseRequested, this, &BrowserWindow::onCloseTab);
    connect(m_strip, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (from == to) return;
        QWidget *w = m_stack->widget(from);
        const TabMeta meta = m_meta.at(from);
        m_stack->removeWidget(w);
        m_stack->insertWidget(to, w);
        m_meta.remove(from);
        m_meta.insert(to, meta);
        saveSession();
    });
    connect(m_strip, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int idx = m_strip->tabAt(pos);
        showTabContextMenu(idx, m_strip->mapToGlobal(pos));
    });

    buildShortcuts();
    startFreezeTimer();

    // stats refresh timer (shield badge)
    connect(&m_statsTimer, &QTimer::timeout, this, &BrowserWindow::updateShieldBadge);
    m_statsTimer.start(2000);
    m_statsTimer.start();

    // seed: one tab
    createTab(true);
}

BrowserWindow::~BrowserWindow() = default;

/* ---------------- chrome construction ---------------- */

QWidget *BrowserWindow::buildTabStripRow() {
    QWidget *row = new QWidget(this);
    row->setObjectName("TabStrip");
    QHBoxLayout *l = new QHBoxLayout(row);
    l->setContentsMargins(8, 4, 8, 0);
    l->setSpacing(4);

    if (m_private) {
        QLabel *priv = new QLabel(row);
        priv->setPixmap(Icons::accent("incognito", 15).pixmap(15, 15));
        l->addWidget(priv);
    }

    m_strip = new TabStrip(row);
    m_strip->setContextMenuPolicy(Qt::CustomContextMenu);
    l->addWidget(m_strip, 1);

    m_btnNewTab = new QToolButton(row);
    m_btnNewTab->setIcon(Icons::themed("plus", 16));
    m_btnNewTab->setAutoRaise(true);
    m_btnNewTab->setToolTip(tr("New tab (Ctrl+T)"));
    connect(m_btnNewTab, &QToolButton::clicked, this, [this] { createTab(true); });
    l->addWidget(m_btnNewTab);

    return row;
}

void BrowserWindow::buildToolbar() {
    m_toolbar = new QToolBar(this);
    m_toolbar->setObjectName("ToolBar");
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->setContentsMargins(6, 4, 6, 4);

    m_btnBack = new QToolButton(this);
    m_btnBack->setAutoRaise(true);
    m_btnFwd = new QToolButton(this);
    m_btnFwd->setAutoRaise(true);
    m_btnReload = new QToolButton(this);
    m_btnReload->setAutoRaise(true);
    m_btnHome = new QToolButton(this);
    m_btnHome->setAutoRaise(true);

    m_btnBack->setToolTip(tr("Back (Alt+←)"));
    m_btnFwd->setToolTip(tr("Forward (Alt+→)"));
    m_btnReload->setToolTip(tr("Reload (Ctrl+R)"));
    m_btnHome->setToolTip(tr("Home"));
    connect(m_btnBack, &QToolButton::clicked, this, [this] { if (currentView()) currentView()->back(); });
    connect(m_btnFwd, &QToolButton::clicked, this, [this] { if (currentView()) currentView()->forward(); });
    connect(m_btnReload, &QToolButton::clicked, this, [this] {
        if (!currentView()) return;
        if (m_btnReload->property("stop").toBool())
            currentView()->stop();
        else
            currentView()->reload();
    });
    connect(m_btnHome, &QToolButton::clicked, this, [this] {
        openOrNavigate(QStringLiteral("kestrel://ui/newtab"));
    });

    m_toolbar->addWidget(m_btnBack);
    m_toolbar->addWidget(m_btnFwd);
    m_toolbar->addWidget(m_btnReload);
    m_toolbar->addWidget(m_btnHome);
    m_toolbar->addSeparator();

    // omnibox container: [security icon][edit][shield][star]
    QWidget *box = new QWidget(this);
    QHBoxLayout *bl = new QHBoxLayout(box);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(2);

    QToolButton *secIcon = new QToolButton(box);
    secIcon->setAutoRaise(true);
    secIcon->setIcon(Icons::themed("lock", 15));
    secIcon->setToolTip(tr("Site information"));
    connect(secIcon, &QToolButton::clicked, this, [this] {
        if (!currentView()) return;
        const QUrl u = currentView()->url();
        const bool secure = u.scheme() == "https";
        QMessageBox::information(this, tr("Site information"),
            secure ? tr("Connection is secure.\n\n%1\nTLS-encrypted; HTTPS-First mode is active.")
                   : tr("Connection is NOT secure.\n\n%1\nData may be visible to others.")
                       .arg(u.host()));
    });
    bl->addWidget(secIcon);

    m_omnibox = new Omnibox(box);
    connect(m_omnibox, &Omnibox::navigateRequested, this, [this](const QUrl &u) {
        if (currentView()) currentView()->setUrl(u);
    });
    connect(m_omnibox, &Omnibox::searchRequested, this, &BrowserWindow::searchFor);
    bl->addWidget(m_omnibox, 1);

    m_btnShield = new QToolButton(box);
    m_btnShield->setObjectName("ShieldButton");
    m_btnShield->setIcon(Icons::accent("shieldcheck", 17));
    m_btnShield->setToolTip(tr("Privacy shield"));
    connect(m_btnShield, &QToolButton::clicked, this, &BrowserWindow::showShieldMenu);
    bl->addWidget(m_btnShield);

    m_badge = new QLabel(box);
    m_badge->setObjectName("BlockedBadge");
    m_badge->hide();
    bl->addWidget(m_badge);

    m_btnStar = new QToolButton(box);
    m_btnStar->setObjectName("BookmarkStar");
    m_btnStar->setIcon(Icons::themed("star", 16));
    m_btnStar->setToolTip(tr("Bookmark this page (Ctrl+D)"));
    connect(m_btnStar, &QToolButton::clicked, this, [this] { toggleBookmark(); });
    bl->addWidget(m_btnStar);

    m_toolbar->addWidget(box);
    m_toolbar->addSeparator();

    m_btnDownloads = new QToolButton(this);
    m_btnDownloads->setAutoRaise(true);
    m_btnDownloads->setIcon(Icons::themed("download", 17));
    m_btnDownloads->setToolTip(tr("Downloads (Ctrl+J)"));
    connect(m_btnDownloads, &QToolButton::clicked, this, [this] {
        openOrNavigate(QStringLiteral("kestrel://ui/downloads"));
    });
    m_toolbar->addWidget(m_btnDownloads);

    m_btnMenu = new QToolButton(this);
    m_btnMenu->setAutoRaise(true);
    m_btnMenu->setIcon(Icons::themed("menu", 17));
    m_btnMenu->setToolTip(tr("Menu"));
    connect(m_btnMenu, &QToolButton::clicked, this, &BrowserWindow::showMainMenu);
    m_toolbar->addWidget(m_btnMenu);

    connect(KestrelApp::instance(), &KestrelApp::themeChanged, this, [this] {
        m_btnNewTab->setIcon(Icons::themed("plus", 16));
        m_btnShield->setIcon(Icons::accent("shieldcheck", 17));
        m_btnDownloads->setIcon(Icons::themed("download", 17));
        m_btnMenu->setIcon(Icons::themed("menu", 17));
        m_btnStar->setIcon(Icons::themed("star", 16));
        m_btnBack->setIcon(Icons::themed("back", 18));
        m_btnFwd->setIcon(Icons::themed("forward", 18));
        m_btnHome->setIcon(Icons::themed("home", 18));
        updateNavButtons();
    });
}

/* ---------------- tab management ---------------- */

KestrelView *BrowserWindow::currentView() const {
    return m_meta.value(m_strip->currentIndex()).view.data();
}

KestrelView *BrowserWindow::createTab(bool focus) {
    QWebEngineProfile *profile = m_private
        ? KestrelApp::instance()->privateProfile()
        : KestrelApp::instance()->profile();
    KestrelView *view = new KestrelView(profile, this);
    view->page()->setPrivate(m_private);
    const int idx = m_stack->addWidget(view);
    m_strip->addTab(tr("New Tab"));
    m_meta.resize(m_meta.size() + 1);
    m_meta[idx] = TabMeta{view, false, false, QString()};

    connect(view, &KestrelView::urlChanged, this, &BrowserWindow::onUrlChanged);
    connect(view->page(), &QWebEnginePage::titleChanged, this, [this, view](const QString &t) {
        const int idx = m_stack->indexOf(view);
        if (idx < 0) return;
        m_strip->setTabText(idx, t.isEmpty() ? tr("New Tab") : t);
        m_strip->setTabToolTip(idx, t);
    });
    connect(view->page(), &QWebEnginePage::iconChanged, this, [this, view](const QIcon &icon) {
        const int idx = m_stack->indexOf(view);
        if (idx >= 0) m_strip->setFavicon(idx, icon);
    });
    connect(view->page(), &QWebEnginePage::loadProgress, this, [this, view](int progress) {
        if (view != currentView()) return;
        if (progress < 100) {
            m_btnReload->setProperty("stop", true);
            m_btnReload->setIcon(Icons::themed("stop", 18));
        } else {
            m_btnReload->setProperty("stop", false);
            m_btnReload->setIcon(Icons::themed("reload", 18));
        }
        updateNavButtons();
    });
    connect(view->page(), &KestrelPage::hostFailedTls, this, [this](const QString &host) {
        updateSecurityUi();
        Q_UNUSED(host);
    });
    connect(view, &KestrelView::zoomChanged, this, [this](double z) {
        Q_UNUSED(z);
        refreshOmnibox();
    });

    view->setUrl(QUrl(QStringLiteral("kestrel://ui/newtab")));
    if (focus) {
        m_strip->setCurrentIndex(idx);
        m_omnibox->setFocus();
    }
    return view;
}

KestrelView *BrowserWindow::createTabWithUrl(const QUrl &url, bool focus) {
    KestrelView *v = createTab(focus);
    v->setUrl(url);
    return v;
}

KestrelView *BrowserWindow::createBackgroundTab() {
    return createTab(false);
}

void BrowserWindow::onCloseTab(int index) {
    if (index < 0 || index >= m_meta.size()) return;
    KestrelView *view = m_meta[index].view.data();
    if (!view) return;
    const QUrl url = view->url();
    if (url.isValid() && url.scheme().startsWith("http"))
        m_closedUrls.append(url);
    m_meta.remove(index);
    QWidget *w = m_stack->widget(index);
    m_strip->removeTab(index);
    m_stack->removeWidget(w);
    w->deleteLater();
    if (m_meta.isEmpty()) {
        close();
        return;
    }
    saveSession();
}

void BrowserWindow::reopenLastClosedTab() {
    if (m_closedUrls.isEmpty()) return;
    createTabWithUrl(m_closedUrls.takeLast());
}

void BrowserWindow::onCurrentChanged(int index) {
    Q_UNUSED(index);
    refreshOmnibox();
    updateSecurityUi();
    updateShieldBadge();
    updateNavButtons();
    saveSession();
}

void BrowserWindow::onUrlChanged(const QUrl &url) {
    KestrelView *v = qobject_cast<KestrelView *>(sender());
    if (!v) return;
    const int idx = m_stack->indexOf(v);
    if (idx < 0) return;
    if (idx == m_strip->currentIndex()) refreshOmnibox();
    updateSecurityUi();
    saveSession();
}

void BrowserWindow::refreshOmnibox() {
    KestrelView *v = currentView();
    if (!v) return;
    const QUrl u = v->url();
    const bool secure = u.scheme() == "https";
    m_omnibox->setUrlState(u, secure, u.scheme() == "kestrel");
    const bool bookmarked = core() &&
        kestrel_is_bookmarked(core(), u.toString(QUrl::RemoveFragment).toUtf8().constData());
    m_btnStar->setIcon(bookmarked ? Icons::accent("starfilled", 17) : Icons::themed("star", 17));
}

void BrowserWindow::updateSecurityUi() {
    KestrelView *v = currentView();
    if (!v) return;
    const QUrl u = v->url();
    const bool secure = u.scheme() == "https" || u.scheme() == "kestrel";
    if (!secure && u.scheme() == "http" && !u.host().isEmpty())
        m_omnibox->setProperty("insecure", true);
    else
        m_omnibox->setProperty("insecure", false);
    m_omnibox->style()->unpolish(m_omnibox);
    m_omnibox->style()->polish(m_omnibox);
}

void BrowserWindow::updateShieldBadge() {
    KestrelCore *c = core();
    if (!c) return;
    const uint64_t n = kestrel_session_block_total(c);
    if (n > 0) {
        m_badge->show();
        m_badge->setText(n > 999 ? QStringLiteral("999+") : QString::number(n));
        m_badge->setToolTip(tr("%1 trackers & ads blocked this session").arg(n));
    } else {
        m_badge->hide();
    }
}

void BrowserWindow::updateNavButtons() {
    KestrelView *v = currentView();
    if (!v) return;
    m_btnBack->setIcon(Icons::themed("back", 18));
    m_btnFwd->setIcon(Icons::themed("forward", 18));
    m_btnHome->setIcon(Icons::themed("home", 18));
    m_btnBack->setEnabled(v->history()->canGoBack());
    m_btnFwd->setEnabled(v->history()->canGoForward());
}

/* ---------------- navigation helpers ---------------- */

QUrl BrowserWindow::searchUrl(const QString &query) const {
    const QString engine = KestrelApp::instance()->getSetting("search_engine", "duckduckgo");
    QString url;
    if (engine == "google") url = "https://www.google.com/search?q=%1";
    else if (engine == "bing") url = "https://www.bing.com/search?q=%1";
    else if (engine == "brave") url = "https://search.brave.com/search?q=%1";
    else url = "https://duckduckgo.com/?q=%1";
    return QUrl(url.arg(QString::fromUtf8(QUrl::toPercentEncoding(query))));
}

void BrowserWindow::openOrNavigate(const QString &textOrUrl) {
    if (textOrUrl.isEmpty()) return;
    QUrl u = QUrl::fromUserInput(textOrUrl);
    if (textOrUrl.startsWith("kestrel://")) u = QUrl(textOrUrl);
    const bool looksUrl = textOrUrl.startsWith("http") || textOrUrl.startsWith("kestrel://") ||
                          textOrUrl.contains('.') || textOrUrl.startsWith("localhost");
    if (!looksUrl) return searchFor(textOrUrl);
    if (u.isValid()) {
        if (currentView()) currentView()->setUrl(u);
        else createTabWithUrl(u);
    }
}

void BrowserWindow::openInBackground(const QUrl &url) {
    KestrelView *v = createBackgroundTab();
    if (v) v->setUrl(url);
}

void BrowserWindow::searchFor(const QString &query) {
    if (currentView()) currentView()->setUrl(searchUrl(query));
    else createTabWithUrl(searchUrl(query));
}

void BrowserWindow::newPrivateWindow() {
    BrowserWindow *w = new BrowserWindow();
    w->m_private = true;
    w->applyPrivateStyle();
    w->show();
    // Note: private profile pages — rebuild tabs with off-the-record profile
    // (first tab created in ctor uses the default profile; replace it)
    const int idx = w->m_strip->currentIndex();
    w->onCloseTab(idx);
    w->createTab(true);
}

void BrowserWindow::applyPrivateStyle() {
    setWindowTitle(tr("Kestrel — Private Browsing"));
}

/* ---------------- bookmark toggle ---------------- */

void BrowserWindow::toggleBookmark() {
    KestrelView *v = currentView();
    KestrelCore *c = core();
    if (!v || !c) return;
    const QUrl u = v->url();
    if (!u.isValid() || u.scheme() != "http" && u.scheme() != "https") return;
    const std::string urlStr = u.toString(QUrl::RemoveFragment).toUtf8().toStdString();
    const std::string titleStr = (v->title().isEmpty() ? u.host() : v->title()).toUtf8().toStdString();
    kestrel_bookmark_toggle(c, urlStr.c_str(), titleStr.c_str());
    refreshOmnibox();
}

/* ---------------- menus ---------------- */

void BrowserWindow::showMainMenu() {
    QMenu menu(this);
    menu.addAction(Icons::themed("plus"), tr("New tab"), this, [this] { createTab(true); });
    menu.addAction(Icons::themed("incognito"), tr("New private window"), this, [this] { newPrivateWindow(); });
    menu.addSeparator();
    menu.addAction(Icons::themed("history"), tr("History"), this, [this] { openOrNavigate("kestrel://ui/history"); });
    menu.addAction(Icons::themed("star"), tr("Bookmarks"), this, [this] { openOrNavigate("kestrel://ui/bookmarks"); });
    menu.addAction(Icons::themed("download"), tr("Downloads"), this, [this] { openOrNavigate("kestrel://ui/downloads"); });
    menu.addSeparator();
    menu.addAction(Icons::themed("shield"), tr("Privacy dashboard"), this, [this] { openOrNavigate("kestrel://ui/privacy"); });
    menu.addAction(Icons::themed("zoomin"), tr("Zoom in"), this, [this] {
        if (currentView()) currentView()->setZoomFactor(qBound(0.25, currentView()->zoomFactor() + 0.1, 5.0));
    });
    menu.addAction(Icons::themed("zoomout"), tr("Zoom out"), this, [this] {
        if (currentView()) currentView()->setZoomFactor(qBound(0.25, currentView()->zoomFactor() - 0.1, 5.0));
    });
    menu.addSeparator();
    menu.addAction(Icons::themed("print"), tr("Print…"), this, &BrowserWindow::printCurrent);
    menu.addAction(Icons::themed("save"), tr("Save page as…"), this, &BrowserWindow::saveCurrentPage);
    menu.addAction(Icons::themed("code"), tr("Developer tools"), this, [this] { toggleDevTools(true); });
    menu.addSeparator();
    menu.addAction(Icons::themed("settings"), tr("Settings"), this, [this] { openOrNavigate("kestrel://ui/settings"); });
    menu.addAction(tr("About Kestrel"), this, [this] { openOrNavigate("kestrel://ui/settings"); });
    menu.exec(m_btnMenu->mapToGlobal(QPoint(0, m_btnMenu->height() + 4)));
}

void BrowserWindow::showShieldMenu() {
    QMenu menu(this);
    KestrelCore *c = core();
    const uint64_t n = c ? kestrel_session_block_total(c) : 0;
    QLabel *head = new QLabel(tr("  %1 blocked this session").arg(n));
    QFont f = head->font();
    f.setBold(true);
    head->setFont(f);
    QWidgetAction *wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(head);
    menu.addAction(wa);
    menu.addSeparator();
    QAction *ads = menu.addAction(tr("Block ads"), this, [] {
        KestrelApp::instance()->setSetting("block_ads",
            KestrelApp::instance()->getSetting("block_ads", "1") == "1" ? "0" : "1");
    });
    ads->setCheckable(true);
    ads->setChecked(KestrelApp::instance()->getSetting("block_ads", "1") == "1");
    QAction *track = menu.addAction(tr("Block trackers"), this, [] {
        KestrelApp::instance()->setSetting("block_trackers",
            KestrelApp::instance()->getSetting("block_trackers", "1") == "1" ? "0" : "1");
    });
    track->setCheckable(true);
    track->setChecked(KestrelApp::instance()->getSetting("block_trackers", "1") == "1");
    QAction *https = menu.addAction(tr("HTTPS-First"), this, [] {
        KestrelApp::instance()->setSetting("https_first",
            KestrelApp::instance()->getSetting("https_first", "1") == "1" ? "0" : "1");
    });
    https->setCheckable(true);
    https->setChecked(KestrelApp::instance()->getSetting("https_first", "1") == "1");
    menu.addSeparator();
    menu.addAction(tr("Open privacy dashboard…"), this, [this] { openOrNavigate("kestrel://ui/privacy"); });
    menu.exec(m_btnShield->mapToGlobal(QPoint(0, m_btnShield->height() + 4)));
}

void BrowserWindow::showTabContextMenu(int index, const QPoint &globalPos) {
    QMenu menu(this);
    if (index >= 0) {
        menu.addAction(tr("New tab to the right"), this, [this] { createTab(true); });
        menu.addSeparator();
        menu.addAction(tr("Pin tab"), this, [this, index] {
            const bool pin = !m_strip->isPinned(index);
            m_strip->setPinned(index, pin);
            m_meta[index].pinned = pin;
            saveSession();
        });
        menu.addAction(m_strip->isMuted(index) ? tr("Unmute tab") : tr("Mute tab"), this, [this, index] {
            const bool mute = !m_strip->isMuted(index);
            m_strip->setMuted(index, mute);
            m_meta[index].muted = mute;
            if (m_meta[index].view) m_meta[index].view->page()->setAudioMuted(mute);
            saveSession();
        });
        menu.addSeparator();
        QMenu *groupMenu = menu.addMenu(tr("Tab group"));
        const QStringList colors = {"", "#FF5C8A", "#FFB454", "#3DD68C", "#4D9FFF", "#7C5CFF"};
        groupMenu->addAction(tr("None"), this, [this, index, colors] {
            m_strip->setGroup(index, "");
            m_meta[index].group = "";
            saveSession();
        });
        for (int i = 1; i < colors.size(); ++i) {
            QPixmap pm(12, 12);
            pm.fill(QColor(colors[i]));
            groupMenu->addAction(QIcon(pm), tr("Group %1").arg(i), this, [this, index, colors, i] {
                m_strip->setGroup(index, colors[i]);
                m_meta[index].group = colors[i];
                saveSession();
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Duplicate tab"), this, [this, index] {
            if (m_meta[index].view) createTabWithUrl(m_meta[index].view->url());
        });
        menu.addAction(tr("Close other tabs"), this, [this, index] {
            for (int i = m_meta.size() - 1; i >= 0; --i)
                if (i != index) onCloseTab(i);
        });
        menu.addAction(tr("Close tab"), this, [this, index] { onCloseTab(index); });
    } else {
        menu.addAction(tr("New tab"), this, [this] { createTab(true); });
        menu.addAction(tr("Reopen closed tab"), this, &BrowserWindow::reopenLastClosedTab);
    }
    menu.exec(globalPos);
}

/* ---------------- tools ---------------- */

void BrowserWindow::toggleDevTools(bool open) {
    Q_UNUSED(open);
    KestrelView *v = currentView();
    if (!v) return;
    if (!m_devToolsPage) {
        m_devToolsPage = new QWebEnginePage(KestrelApp::instance()->profile(), this);
        v->page()->setDevToolsPage(m_devToolsPage);
        m_devToolsView = new KestrelView(KestrelApp::instance()->profile(), this);
        m_devToolsView->setPage(m_devToolsPage);
        centralWidget()->layout()->addWidget(m_devToolsView);
        m_devToolsView->setFixedHeight(300);
        m_devToolsView->show();
    } else {
        v->page()->setDevToolsPage(nullptr);
        m_devToolsView->hide();
        m_devToolsView->deleteLater();
        m_devToolsView = nullptr;
        m_devToolsPage->deleteLater();
        m_devToolsPage = nullptr;
    }
}

void BrowserWindow::printCurrent() {
    KestrelView *v = currentView();
    if (!v) return;
    QPrinter printer;
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted)
        v->print(&printer);
}

void BrowserWindow::saveCurrentPage() {
    KestrelView *v = currentView();
    if (!v) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save page"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + v->title() + ".mhtml",
        tr("Web page (*.mhtml)"));
    if (!path.isEmpty())
        v->page()->save(path, QWebEngineDownloadRequest::SavePageFormat::MimeHtmlSaveFormat);
}

void BrowserWindow::viewSource() {
    KestrelView *v = currentView();
    if (!v) return;
    KestrelView *src = createTab(false);
    src->setHtml(tr("<pre>Loading source…</pre>"));
    v->page()->toHtml([src](const QString &html) {
        src->setHtml("<pre style=\"white-space:pre-wrap;font-size:12px\">" +
                     html.toHtmlEscaped() + "</pre>");
    });
}

void BrowserWindow::applyFind() {
    KestrelView *v = currentView();
    if (!v) return;
    const bool backward = false;
    QWebEnginePage::FindFlags flags = backward ? QWebEnginePage::FindBackward : QWebEnginePage::FindFlag(0);
    v->page()->findText(m_findEdit->text(), flags,
        [this](const QWebEngineFindTextResult &r) {
            QLabel *c = m_findBar->findChild<QLabel *>("FindCount");
            if (c)
                c->setText(r.numberOfMatches() > 0
                    ? QStringLiteral("%1/%2").arg(r.activeMatch()).arg(r.numberOfMatches())
                    : (m_findEdit->text().isEmpty() ? QString() : tr("no match")));
        });
}

/* ---------------- shortcuts ---------------- */

void BrowserWindow::buildShortcuts() {
    auto sc = [this](const QKeySequence &k, auto fn) {
        QShortcut *s = new QShortcut(k, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    sc(QKeySequence("Ctrl+T"), [this] { createTab(true); });
    sc(QKeySequence("Ctrl+W"), [this] { onCloseTab(m_strip->currentIndex()); });
    sc(QKeySequence("Ctrl+Shift+T"), [this] { reopenLastClosedTab(); });
    sc(QKeySequence("Ctrl+Tab"), [this] {
        if (m_strip->count()) m_strip->setCurrentIndex((m_strip->currentIndex() + 1) % m_strip->count());
    });
    sc(QKeySequence("Ctrl+Shift+Tab"), [this] {
        if (m_strip->count()) m_strip->setCurrentIndex((m_strip->currentIndex() - 1 + m_strip->count()) % m_strip->count());
    });
    for (int i = 1; i <= 8; ++i)
        sc(QKeySequence(QString("Ctrl+%1").arg(i)), [this, i] {
            if (m_strip->count() >= i) m_strip->setCurrentIndex(i - 1);
        });
    sc(QKeySequence("Ctrl+9"), [this] { if (m_strip->count()) m_strip->setCurrentIndex(m_strip->count() - 1); });
    sc(QKeySequence("Ctrl+L"), [this] { m_omnibox->setFocus(); m_omnibox->selectAll(); });
    sc(QKeySequence("Ctrl+F"), [this] { m_findBar->show(); m_findEdit->setFocus(); m_findEdit->selectAll(); });
    sc(QKeySequence("Ctrl+R"), [this] { if (currentView()) currentView()->reload(); });
    sc(QKeySequence("F5"), [this] { if (currentView()) currentView()->reload(); });
    sc(QKeySequence("Ctrl+Shift+R"), [this] {
        if (currentView()) currentView()->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
    });
    sc(QKeySequence("Alt+Left"), [this] { if (currentView()) currentView()->back(); });
    sc(QKeySequence("Alt+Right"), [this] { if (currentView()) currentView()->forward(); });
    sc(QKeySequence("Alt+Home"), [this] { openOrNavigate("kestrel://ui/newtab"); });
    sc(QKeySequence("Ctrl+D"), [this] { toggleBookmark(); });
    sc(QKeySequence("Ctrl+H"), [this] { openOrNavigate("kestrel://ui/history"); });
    sc(QKeySequence("Ctrl+J"), [this] { openOrNavigate("kestrel://ui/downloads"); });
    sc(QKeySequence("Ctrl+Shift+Del"), [this] { openOrNavigate("kestrel://ui/privacy"); });
    sc(QKeySequence("Ctrl+Shift+N"), [this] { newPrivateWindow(); });
    sc(QKeySequence("Ctrl+P"), [this] { printCurrent(); });
    sc(QKeySequence("Ctrl+S"), [this] { saveCurrentPage(); });
    sc(QKeySequence("F11"), [this] {
        if (isFullScreen()) showNormal();
        else showFullScreen();
    });
    sc(QKeySequence("Ctrl++"), [this] {
        if (currentView()) currentView()->setZoomFactor(qBound(0.25, currentView()->zoomFactor() + 0.1, 5.0));
    });
    sc(QKeySequence("Ctrl+="), [this] {
        if (currentView()) currentView()->setZoomFactor(qBound(0.25, currentView()->zoomFactor() + 0.1, 5.0));
    });
    sc(QKeySequence("Ctrl+-"), [this] {
        if (currentView()) currentView()->setZoomFactor(qBound(0.25, currentView()->zoomFactor() - 0.1, 5.0));
    });
    sc(QKeySequence("Ctrl+0"), [this] {
        if (currentView()) currentView()->setZoomFactor(1.0);
    });
    sc(QKeySequence("F12"), [this] { toggleDevTools(true); });
    sc(QKeySequence("Ctrl+U"), [this] { viewSource(); });
    sc(QKeySequence("Escape"), [this] {
        if (m_findBar->isVisible()) {
            m_findBar->hide();
            if (currentView()) currentView()->page()->findText("");
        }
        m_omnibox->setFocus();
    });

    // find bar live search
    connect(m_findEdit, &QLineEdit::textChanged, this, &BrowserWindow::applyFind);
    // find prev/next/close
    QShortcut *fNext = new QShortcut(QKeySequence("Return"), m_findEdit);
    connect(fNext, &QShortcut::activated, this, &BrowserWindow::applyFind);
}

/* ---------------- session & freezing ---------------- */

void BrowserWindow::saveSession() {
    KestrelCore *c = core();
    if (!c) return;
    QJsonArray tabs;
    for (const TabMeta &m : m_meta) {
        if (!m.view) continue;
        QJsonObject t;
        t.insert("url", m.view->url().toString());
        t.insert("title", m.view->title());
        t.insert("pinned", m.pinned);
        t.insert("muted", m.muted);
        t.insert("group", m.group);
        tabs.append(t);
    }
    QJsonObject o;
    o.insert("tabs", tabs);
    o.insert("current", m_strip->currentIndex());
    o.insert("geometry", QString::fromUtf8(saveGeometry().toBase64()));
    kestrel_session_save(c, QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
}

void BrowserWindow::restoreSessionIfNeeded() {
    KestrelCore *c = core();
    if (!c) return;
    const QString mode = KestrelApp::instance()->getSetting("session_restore", "always");
    const QString clean = KestrelApp::instance()->getSetting("clean_exit", "1");
    char *raw = kestrel_session_load(c);
    if (!raw) return;
    const QByteArray data(raw);
    kestrel_string_free(raw);
    const QJsonObject o = QJsonDocument::fromJson(data).object();

    if (mode == "never") return;
    if (mode == "ask" && clean != "1") {
        const auto r = QMessageBox::question(this, tr("Kestrel didn't shut down correctly"),
            tr("Restore your previous session?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (r != QMessageBox::Yes) return;
    }
    const QJsonArray tabs = o.value("tabs").toArray();
    bool first = true;
    for (const auto &t : tabs) {
        const QJsonObject to = t.toObject();
        const QUrl url = to.value("url").toObject().value("url").toVariant().toUrl().isValid()
            ? QUrl(to.value("url").toString()) : QUrl();
        if (url.scheme() != "http" && url.scheme() != "https" && url.scheme() != "kestrel")
            continue;
        KestrelView *v = first ? currentView() : createTab(false);
        first = false;
        if (!v) continue;
        v->setUrl(url);
        const int idx = m_stack->indexOf(v);
        m_strip->setPinned(idx, to.value("pinned").toBool());
        m_meta[idx].pinned = to.value("pinned").toBool();
        const bool muted = to.value("muted").toBool();
        if (muted && v) v->page()->setAudioMuted(true);
        m_strip->setMuted(idx, muted);
        m_meta[idx].muted = muted;
        const QString group = to.value("group").toString();
        if (!group.isEmpty()) { m_strip->setGroup(idx, group); m_meta[idx].group = group; }
    }
}

void BrowserWindow::startFreezeTimer() {
    connect(&m_freezeTimer, &QTimer::timeout, this, [this] {
        if (KestrelApp::instance()->getSetting("freeze_background_tabs", "1") != "1") return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int current = m_strip->currentIndex();
        for (int i = 0; i < m_meta.size(); ++i) {
            if (i == current || !m_meta[i].view) continue;
            KestrelPage *p = m_meta[i].view->page();
            if (!p) continue;
            if (p->recentlyAudible()) { p->pokeActive(); continue; }
            if (now - p->lastActiveMs() > 10 * 60 * 1000 &&
                p->lifecycleState() == QWebEnginePage::LifecycleState::Active) {
                if (p->recommendedState() == QWebEnginePage::LifecycleState::Frozen)
                    p->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
            }
        }
    });
    m_freezeTimer.start(60000);
}

/* ---------------- window events ---------------- */

void BrowserWindow::closeEvent(QCloseEvent *e) {
    saveSession();
    e->accept();
}

void BrowserWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    m_strip->updateGeometry();
    m_strip->update();
}

bool BrowserWindow::eventFilter(QObject *obj, QEvent *ev) {
    return QMainWindow::eventFilter(obj, ev);
}
