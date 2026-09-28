// omnibox.h — address bar: url/search detection, completer, security icon
#pragma once
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QJsonArray>

class QCompleter;
class QStringListModel;
class QTimer;

class Omnibox : public QLineEdit {
    Q_OBJECT
public:
    explicit Omnibox(QWidget *parent = nullptr);
    void setUrlState(const QUrl &url, bool secure, bool internal);
    void resetToCurrent(const QUrl &url);

signals:
    void navigateRequested(const QUrl &url);
    void searchRequested(const QString &query);

protected:
    void focusInEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private slots:
    void onEdited(const QString &text);
    void networkSuggestions(const QString &text);
    void suggestionsDone();

private:
    void updateModel();
    QCompleter *m_completer;
    QStringListModel *m_model;
    QNetworkAccessManager m_nam;
    QTimer *m_suggestTimer;
    QJsonArray m_netSuggestions;
    QString m_currentUrl;
};
