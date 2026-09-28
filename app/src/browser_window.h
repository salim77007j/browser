// browser_window.h — main window: tab strip, toolbar, omnibox, menus, shortcuts
#pragma once
#include <QMainWindow>
#include <QVector>
class QStackedWidget;
#include <QPointer>
#include <QTimer>

class QWebEnginePage;
class QToolBar;
class QTabWidget;
class TabStrip;
class Omnibox;
class KestrelView;
class KestrelPage;
class QLineEdit;
class QToolButton;
class QLabel;
class FindBar;
class QActionGroup;

class BrowserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget *parent = nullptr);
    ~BrowserWindow() override;

    KestrelView *currentView() const;

    KestrelView *createTab(bool focus = true);              // new tab with NTP
    KestrelView *createTabWithUrl(const QUrl &url, bool focus = true);
    KestrelView *createBackgroundTab();                     // for target=_blank
    void openOrNavigate(const QString &textOrUrl);
    void openInBackground(const QUrl &url);
    void searchFor(const QString &query);
    void newPrivateWindow();
    void reopenLastClosedTab();
    void toggleDevTools(bool open);
    void printCurrent();
    void saveCurrentPage();
    void viewSource();
    void togglePinCurrent();
    void toggleMuteCurrent();
    void saveSession();
    void restoreSessionIfNeeded();
    void applyPrivateStyle();

signals:
    void windowWantsClose();

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void onCurrentChanged(int index);
    void onCloseTab(int index);
    void onUrlChanged(const QUrl &url);
    void updateSecurityUi();
    void updateShieldBadge();
    void showMainMenu();
    void showTabContextMenu(int index, const QPoint &globalPos);
    void showShieldMenu();
    void toggleBookmark();
    void applyFind();

private:
    QWidget *buildChrome();
    QWidget *buildTabStripRow();
    void buildToolbar();
    void buildShortcuts();
    QUrl searchUrl(const QString &query) const;
    void refreshOmnibox();
    void startFreezeTimer();
    void updateNavButtons();

    // tab model parallel to QTabWidget pages
    struct TabMeta {
        QPointer<KestrelView> view;
        bool pinned = false;
        bool muted = false;
        QString group;
    };
    QVector<TabMeta> m_meta;
    QList<QUrl> m_closedUrls;

    QStackedWidget *m_stack = nullptr;
    QToolBar *m_toolbar = nullptr;
    TabStrip *m_strip = nullptr;
    Omnibox *m_omnibox = nullptr;
    QToolButton *m_btnBack, *m_btnFwd, *m_btnReload, *m_btnHome = nullptr;
    QToolButton *m_btnShield = nullptr;
    QToolButton *m_btnStar = nullptr;
    QToolButton *m_btnDownloads = nullptr;
    QToolButton *m_btnMenu = nullptr;
    QToolButton *m_btnNewTab = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QWebEnginePage *m_devToolsPage = nullptr;
    KestrelView *m_devToolsView = nullptr;
    bool m_private = false;
    QTimer m_freezeTimer;
    QTimer m_statsTimer;
};
