// main.cpp — entry point: chromium flags from settings, scheme registration, app boot.
#include <QApplication>
#include <QFile>
#include <QCommandLineParser>
#include <QWebEngineUrlScheme>
#include "app.h"
#include "browser_window.h"

int main(int argc, char *argv[]) {
    // Qt::AA_ShareOpenGLContexts must be set before QApplication
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Determine profile dir early (needed to read settings before QApplication)
    const QString profileDir = qEnvironmentVariable("KESTREL_PROFILE_DIR").isEmpty()
        ? QString::fromLocal8Bit(qgetenv("HOME")) + QStringLiteral("/.config/Kestrel")
        : qEnvironmentVariable("KESTREL_PROFILE_DIR");

    // Core init (Rust) — before QApplication so we can set chromium flags
    KestrelApp::initCore(profileDir);

    const QString theme = KestrelApp::setting("theme", "dark");
    const QString accent = KestrelApp::setting("accent", "#4D9FFF");

    // Chromium flags — privacy + performance decisions at engine level.
    QStringList flags;
    if (KestrelApp::setting("webrtc_public_only", "1") == "1")
        flags << "--force-webrtc-ip-handling-policy=disable_non_proxied_udp";
    const QString doh = KestrelApp::setting("doh_provider", "quad9");
    const QString dohMode = KestrelApp::setting("doh_mode", "auto");
    if (dohMode == "secure") {
        flags << "--enable-features=DnsOverHttps" << "--dns-over-https-mode=secure";
        flags << "--dns-over-https-templates=" + KestrelApp::dohTemplate(doh);
    } else if (dohMode == "auto") {
        flags << "--enable-features=DnsOverHttps" << "--dns-over-https-mode=automatic";
        flags << "--dns-over-https-templates=" + KestrelApp::dohTemplate(doh);
    }
    if (KestrelApp::setting("gpu_acceleration", "1") != "1")
        flags << "--disable-gpu";
    flags << "--enable-blink-features=LazyFrameLoading";
    if (!flags.isEmpty())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.join(' ').toLocal8Bit());

    // Register kestrel:// scheme BEFORE any profile exists
    QWebEngineUrlScheme scheme("kestrel");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::FetchApiAllowed |
                    QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Kestrel");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Kestrel");

    KestrelApp::instance()->setTheme(theme, accent);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("urls", "URLs to open.");
    parser.process(app);
    const QStringList args = parser.positionalArguments();

    BrowserWindow *win = KestrelApp::instance()->ensureMainWindow();
    win->show();
    win->restoreSessionIfNeeded();
    for (const QString &a : args)
        win->openOrNavigate(a);

    const int rc = app.exec();

    KestrelApp::instance()->shutdown();   // session save + stats flush + clean-exit flag
    KestrelApp::shutdownCore();
    return rc;
}
