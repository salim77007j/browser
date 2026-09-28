// tabstrip.h — Chrome-style custom tab bar: pinned, mute, groups, drag reorder
#pragma once
#include <QTabBar>
#include <QHash>
#include <QPixmap>

class TabStrip : public QTabBar {
    Q_OBJECT
public:
    explicit TabStrip(QWidget *parent = nullptr);

    void setPinned(int index, bool pinned);
    bool isPinned(int index) const;
    void setMuted(int index, bool muted);
    bool isMuted(int index) const;
    void setGroup(int index, const QString &color);
    QString group(int index) const;
    void setFavicon(int index, const QIcon &icon);
    QIcon favicon(int index) const;
    // width policy
    QSize tabSizeHint(int index) const override;

signals:
    void newTabRequested();
    void emptyAreaDoubleClicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    QSize minimumSizeHint() const override;

private:
    struct TabData {
        bool pinned = false;
        bool muted = false;
        QString group;
        QIcon favicon;
    };
    QHash<int, TabData> m_data;
    int m_hovered = -1;

    void ensureData(int index);
    QPixmap compositeFavicon(const TabData &d) const;
};
