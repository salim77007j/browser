// web_view.cpp
#include "web_view.h"
#include "web_page.h"
#include "browser_window.h"
#include "app.h"
#include "kestrel_core.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QWheelEvent>
#include <QApplication>
#include <QClipboard>
#include <QStyle>
#include <QWebEngineContextMenuRequest>

namespace {
QByteArray hostOf(const QUrl &url) { return url.host().toUtf8(); }
}

KestrelView::KestrelView(QWebEngineProfile *profile, QWidget *parent)
    : QWebEngineView(parent) {
    setPage(new KestrelPage(profile, this));
    connect(this, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
        if (!u.host().isEmpty() && KestrelApp::instance()->core()) {
            const double z = kestrel_zoom_get(static_cast<KestrelCore *>(KestrelApp::instance()->core()),
                                              hostOf(u).constData());
            if (!qFuzzyCompare(z, 1.0) && qFuzzyCompare(this->zoomFactor(), 1.0))
                setZoomFactor(z);
        }
        emit zoomChanged(zoomFactor());
    });
}

KestrelPage *KestrelView::page() const {
    return static_cast<KestrelPage *>(QWebEngineView::page());
}

void KestrelView::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        double z = zoomFactor();
        z += e->angleDelta().y() > 0 ? 0.1 : -0.1;
        z = qBound(0.25, z, 5.0);
        setZoomFactor(z);
        emit zoomChanged(z);
        const QUrl u = url();
        if (!u.host().isEmpty() && KestrelApp::instance()->core())
            kestrel_zoom_set(static_cast<KestrelCore *>(KestrelApp::instance()->core()),
                             hostOf(u).constData(), z);
        e->accept();
        return;
    }
    QWebEngineView::wheelEvent(e);
}

void KestrelView::contextMenuEvent(QContextMenuEvent *e) {
    QMenu menu(this);
    const QWebEngineContextMenuRequest *req = lastContextMenuRequest();
    const QUrl link = req ? req->linkUrl() : QUrl();

    if (!link.isEmpty()) {
        menu.addAction(tr("Open link in new tab"), this, [this, link] {
            KestrelApp::instance()->ensureMainWindow()->openInBackground(link);
        });
        menu.addAction(tr("Copy link address"), this, [link] {
            QApplication::clipboard()->setText(link.toString());
        });
        menu.addSeparator();
    }
    if (req && req->selectedText().isEmpty() && page()->url().isValid()) {
        menu.addAction(page()->action(QWebEnginePage::Back));
        menu.addAction(page()->action(QWebEnginePage::Forward));
        menu.addAction(page()->action(QWebEnginePage::Reload));
        menu.addSeparator();
    }
    if (req && !req->selectedText().isEmpty()) {
        menu.addAction(tr("Copy"), this, [this] {
            QApplication::clipboard()->setText(page()->selectedText());
        });
        menu.addAction(tr("Search selection"), this, [this] {
            const QString sel = page()->selectedText();
            KestrelApp::instance()->ensureMainWindow()->searchFor(sel);
        });
        menu.addSeparator();
    }
    menu.addAction(tr("Save page as…"), this, [this] {
        KestrelApp::instance()->ensureMainWindow()->saveCurrentPage();
    });
    menu.addAction(tr("Print…"), this, [this] {
        KestrelApp::instance()->ensureMainWindow()->printCurrent();
    });
    menu.addSeparator();
    menu.addAction(tr("View page source"), this, [this] {
        KestrelApp::instance()->ensureMainWindow()->viewSource();
    });
    menu.addAction(tr("Inspect element"), this, [this] {
        KestrelApp::instance()->ensureMainWindow()->toggleDevTools(true);
    });
    menu.exec(e->globalPos());
}
