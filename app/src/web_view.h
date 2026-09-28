// web_view.h — QWebEngineView with context menu + zoom handling
#pragma once
#include <QWebEngineView>

class KestrelPage;

class KestrelView : public QWebEngineView {
    Q_OBJECT
public:
    explicit KestrelView(QWebEngineProfile *profile, QWidget *parent = nullptr);
    KestrelPage *page() const;

signals:
    void zoomChanged(double level);

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
};
