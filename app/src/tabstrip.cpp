// tabstrip.cpp — custom painted tab bar implementing the Kestrel design.
#include "tabstrip.h"
#include "app.h"
#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QStyleOptionTab>
#include <QHelpEvent>

TabStrip::TabStrip(QWidget *parent) : QTabBar(parent) {
    setMovable(true);
    setExpanding(false);
    setTabsClosable(false);   // custom-drawn close buttons; handled in mousePress
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideRight);
    setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    setDrawBase(false);
    setDocumentMode(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void TabStrip::ensureData(int index) { (void)m_data[index]; }

QPixmap TabStrip::compositeFavicon(const TabData &d) const {
    const int sz = 16;
    QPixmap pm(sz + 2, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QIcon icon = d.favicon;
    if (icon.isNull())
        icon = Icons::themed("globe", 16);
    const QPixmap base = icon.pixmap(sz, sz);
    p.drawPixmap(0, 0, base);
    if (d.muted) {
        const QPixmap mute = Icons::themed("mute", 10).pixmap(10, 10);
        p.drawPixmap(sz + 2 - 10, sz - 10, mute);
    }
    return pm;
}

void TabStrip::setPinned(int index, bool pinned) {
    ensureData(index);
    m_data[index].pinned = pinned;
    update();
}

bool TabStrip::isPinned(int index) const { return m_data.value(index).pinned; }

void TabStrip::setMuted(int index, bool muted) {
    ensureData(index);
    m_data[index].muted = muted;
    update();
}

bool TabStrip::isMuted(int index) const { return m_data.value(index).muted; }

void TabStrip::setGroup(int index, const QString &color) {
    ensureData(index);
    m_data[index].group = color;
    update();
}

QString TabStrip::group(int index) const { return m_data.value(index).group; }

void TabStrip::setFavicon(int index, const QIcon &icon) {
    ensureData(index);
    m_data[index].favicon = icon;
    update();
}

QIcon TabStrip::favicon(int index) const { return m_data.value(index).favicon; }

QSize TabStrip::tabSizeHint(int index) const {
    if (m_data.value(index).pinned)
        return QSize(38, 34);
    const int count = qMax(1, this->count());
    const int avail = width() - 80; // leave room for + button
    int w = qBound(120, avail / count, 220);
    return QSize(w, 34);
}

QSize TabStrip::minimumSizeHint() const {
    return QSize(120, 38);
}

void TabStrip::paintEvent(QPaintEvent *e) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm(font());

    for (int i = 0; i < count(); ++i) {
        const TabData d = m_data.value(i);
        const QRect r = tabRect(i);
        const bool active = i == currentIndex();

        // background
        QColor bg;
        if (active) bg = palette().color(QPalette::Window).lightness() < 128
                           ? QColor(0x26, 0x29, 0x33) : QColor(0xFF, 0xFF, 0xFF);
        else if (i == m_hovered)
            bg = palette().color(QPalette::Window).lightness() < 128
                     ? QColor(0xFF, 0xFF, 0xFF, 14) : QColor(0x00, 0x00, 0x00, 14);
        if (bg.isValid()) {
            QPainterPath path;
            path.addRoundedRect(r.adjusted(2, 3, -2, 0), 8, 8);
            p.fillPath(path, bg);
            if (active) {
                // accent underline
                QRect line = r.adjusted(6, 0, -6, 0);
                line.setTop(r.bottom() - 2);
                line.setHeight(2);
                QPainterPath lp;
                lp.addRoundedRect(line, 1.5, 1.5);
                p.fillPath(lp, QColor(KestrelApp::instance()->accent()));
            }
        }

        // group color bar
        if (!d.group.isEmpty()) {
            QRect bar = r.adjusted(2, 5, -r.width() + 5, -7);
            QPainterPath gp;
            gp.addRoundedRect(bar, 1.5, 1.5);
            p.fillPath(gp, QColor(d.group));
        }

        // favicon
        const QPixmap fav = compositeFavicon(d);
        QRect favRect = r.adjusted(d.pinned ? (r.width() - 16) / 2 : 10, 0, 0, 0);
        favRect.setWidth(16);
        favRect.moveTop((r.height() - 16) / 2 + 1);
        p.drawPixmap(favRect.topLeft(), fav);

        // title
        if (!d.pinned) {
            const int textLeft = r.left() + 32;
            const int textRight = r.right() - 24;
            QRect textRect(textLeft, r.top(), textRight - textLeft, r.height());
            p.setPen(active ? palette().color(QPalette::WindowText)
                            : palette().color(QPalette::PlaceholderText));
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(tabText(i), Qt::ElideRight, textRect.width()));
        }

        // close button (not for pinned)
        if (!d.pinned) {
            const QRect cRect(r.right() - 20, (r.height() - 14) / 2 + 2, 14, 14);
            if (i == m_hovered || active) {
                if (cRect.contains(mapFromGlobal(QCursor::pos()))) {
                    QPainterPath cp;
                    cp.addRoundedRect(cRect, 7, 7);
                    p.fillPath(cp, QColor(KestrelApp::instance()->isDark()
                        ? QColor(0xFF,0xFF,0xFF,26) : QColor(0x00,0x00,0x00,20)));
                }
                Icons::themed("close", 12).paint(&p, cRect);
            }
        }
    }
    Q_UNUSED(e);
}

void TabStrip::mouseMoveEvent(QMouseEvent *e) {
    const int idx = tabAt(e->pos());
    if (idx != m_hovered) {
        m_hovered = idx;
        update();
    }
    QTabBar::mouseMoveEvent(e);
}

void TabStrip::leaveEvent(QEvent *e) {
    m_hovered = -1;
    update();
    QTabBar::leaveEvent(e);
}

void TabStrip::mouseDoubleClickEvent(QMouseEvent *e) {
    const int idx = tabAt(e->pos());
    if (idx < 0) {
        emit newTabRequested();
        return;
    }
    QTabBar::mouseDoubleClickEvent(e);
}

void TabStrip::mousePressEvent(QMouseEvent *e) {
    const int idx = tabAt(e->pos());
    if (idx >= 0) {
        const QRect r = tabRect(idx);
        const QRect cRect(r.right() - 20, (r.height() - 14) / 2 + 2, 14, 14);
        if (!m_data.value(idx).pinned && cRect.contains(e->pos()) && e->button() == Qt::LeftButton) {
            emit tabCloseRequested(idx);
            return;
        }
        if (e->button() == Qt::MiddleButton) {
            emit tabCloseRequested(idx);
            return;
        }
        if (e->button() == Qt::LeftButton) {
            setCurrentIndex(idx);   // emits currentChanged
            return;
        }
    } else if (e->button() == Qt::MiddleButton) {
        return;
    }
    QTabBar::mousePressEvent(e);
}
