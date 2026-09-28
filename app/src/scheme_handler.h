// scheme_handler.h — serves kestrel:// internal pages + embedded logo
#pragma once
#include <QWebEngineUrlSchemeHandler>

class KestrelSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit KestrelSchemeHandler(bool isPrivate, QObject *parent = nullptr);
    void requestStarted(QWebEngineUrlRequestJob *job) override;
private:
    bool m_isPrivate = false;
};
