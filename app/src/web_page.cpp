// web_page.cpp
#include "web_page.h"
#include "app.h"
#include "kestrel_core.h"
#include "web_view.h"
#include "browser_window.h"
#include "request_interceptor.h"

#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineProfile>
#include <QWebEngineCertificateError>
#include <QWebEngineNewWindowRequest>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

/* Fingerprint-protection scriptlet (real, verifiable on test pages).
   mode: 0 off, 1 standard, 2 strict */
QString fpScript(int mode) {
    if (mode <= 0) return QString();
    // Deterministic per-origin noise seeded via Math.random on each call is
    // insufficient for canvas stability; we use random noise per read which is
    // the Brave/Firefox approach for canvas (randomized on each extraction).
    return QStringLiteral(R"JS(
(function(){
  const MODE = %1;
  const origToDataURL = HTMLCanvasElement.prototype.toDataURL;
  const origToBlob = HTMLCanvasElement.prototype.toBlob;
  const origGetImageData = CanvasRenderingContext2D.prototype.getImageData;

  function noiseF() { return (Math.random() - 0.5) * 2; }

  function perturb(ctx, w, h) {
    try {
      if (w > 0 && h > 0 && w * h <= 4096 * 4096) {
        const img = origGetImageData.call(ctx, 0, 0, w, h);
        for (let i = 0; i < img.data.length; i += 4) {
          if (img.data[i+3] !== 0) {
            img.data[i]   = Math.max(0, Math.min(255, img.data[i] + (noiseF() > 0 ? 1 : 0)));
            img.data[i+1] = Math.max(0, Math.min(255, img.data[i+1] + (noiseF() > 0 ? 1 : 0)));
            img.data[i+2] = Math.max(0, Math.min(255, img.data[i+2] + (noiseF() > 0 ? 1 : 0)));
          }
        }
        ctx.putImageData(img, 0, 0);
      }
    } catch (e) {}
  }

  HTMLCanvasElement.prototype.toDataURL = function(...args) {
    perturb(this.getContext('2d'), this.width, this.height);
    return origToDataURL.apply(this, args);
  };
  HTMLCanvasElement.prototype.toBlob = function(cb, ...rest) {
    perturb(this.getContext('2d'), this.width, this.height);
    return origToBlob.call(this, cb, ...rest);
  };
  CanvasRenderingContext2D.prototype.getImageData = function(...args) {
    const img = origGetImageData.apply(this, args);
    try {
      for (let i = 0; i < img.data.length; i += 4) {
        if (img.data[i+3] !== 0) img.data[i] = Math.max(0, Math.min(255, img.data[i] + (noiseF() > 0 ? 1 : 0)));
      }
    } catch (e) {}
    return img;
  };

  // AudioContext fingerprint noise
  try {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (AC) {
      const origGetChannelData = AudioBuffer.prototype.getChannelData;
      AudioBuffer.prototype.getChannelData = function(ch) {
        const data = origGetChannelData.call(this, ch);
        for (let i = 0; i < data.length; i++) data[i] += noiseF() * 1e-7;
        return data;
      };
    }
  } catch (e) {}

  // Hardware surfaces
  try { Object.defineProperty(navigator, 'hardwareConcurrency', {get: () => 4}); } catch(e) {}
  try { Object.defineProperty(navigator, 'deviceMemory', {get: () => 8}); } catch(e) {}
  try { Object.defineProperty(navigator, 'getGamepads', {value: () => [null,null,null,null]}); } catch(e) {}
  try {
    if (navigator.mediaDevices && navigator.mediaDevices.enumerateDevices)
      navigator.mediaDevices.enumerateDevices = () => Promise.resolve([]);
  } catch(e) {}

  // WebGL vendor/renderer masking (standard+)
  const vendor = 'Kestrel', renderer = 'Kestrel Software WebGL';
  try {
    const origGetParameter = WebGLRenderingContext.prototype.getParameter;
    WebGLRenderingContext.prototype.getParameter = function(p) {
      const glv = this.getParameter === origGetParameter ? this : this;
      if (p === 37445) return vendor;           // UNMASKED_VENDOR_WEBGL
      if (p === 37446) return renderer;         // UNMASKED_RENDERER_WEBGL
      if (p === 7936) return vendor;            // VENDOR
      if (p === 7937) return renderer;          // RENDERER
      return origGetParameter.call(this, p);
    };
    if (window.WebGL2RenderingContext) {
      const orig2 = WebGL2RenderingContext.prototype.getParameter;
      WebGL2RenderingContext.prototype.getParameter = function(p) {
        if (p === 37445) return vendor;
        if (p === 37446) return renderer;
        if (p === 7936) return vendor;
        if (p === 7937) return renderer;
        return orig2.call(this, p);
      };
    }
  } catch (e) {}

  if (MODE >= 2) {
    // strict: mask screen metrics to common, disable battery API
    try { Object.defineProperty(screen, 'colorDepth', {get: () => 24}); } catch(e) {}
    try { if (navigator.getBattery) navigator.getBattery = () => Promise.reject(); } catch(e) {}
    try { Object.defineProperty(navigator, 'platform', {get: () => 'Win32'}); } catch(e) {}
  }
})();
)JS").arg(mode);
}

QWebEngineScript makeScript(const QString &name, const QString &src,
                            QWebEngineScript::InjectionPoint point, bool mainWorld = true) {
    QWebEngineScript s;
    s.setName(name);
    s.setSourceCode(src);
    s.setInjectionPoint(point);
    s.setWorldId(mainWorld ? QWebEngineScript::MainWorld : QWebEngineScript::ApplicationWorld);
    s.setRunsOnSubFrames(true);
    return s;
}

} // namespace

KestrelPage::KestrelPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent) {
    m_private = profile->isOffTheRecord();

    // Certificate errors: harden — reject invalid certs, remember host for
    // HTTPS-First fallback. (Qt6 exposes this as a signal.)
    connect(this, &QWebEnginePage::certificateError, this,
            [this](QWebEngineCertificateError error) {
        const QString host = error.url().host();
        if (!host.isEmpty()) {
            RequestInterceptor::markTlsFailure(host);
            emit hostFailedTls(host);
        }
        error.rejectCertificate();
    });

    // target=_blank / window.open → new tab in same window
    connect(this, &QWebEnginePage::newWindowRequested, this,
            [](QWebEngineNewWindowRequest &request) {
        BrowserWindow *w = KestrelApp::instance()->ensureMainWindow();
        const QUrl url = request.requestedUrl();
        if (request.destination() == QWebEngineNewWindowRequest::InNewBackgroundTab)
            w->openInBackground(url);
        else
            w->createTabWithUrl(url);
    });

    connect(this, &QWebEnginePage::urlChanged, this, [this](const QUrl &u) {
        pokeActive();
        applyPrivacyScripts(u);
        if (!u.host().isEmpty())
            emit securityChanged(u.scheme() == "https" || u.scheme() == "kestrel", u.host());
    });
    connect(this, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        Q_UNUSED(ok);
        pokeActive();
    });
    // Record history for http(s) main frame (never in private mode)
    connect(this, &QWebEnginePage::titleChanged, this, [this](const QString &title) {
        const QUrl u = url();
        if (!m_private && (u.scheme() == "http" || u.scheme() == "https") && !u.host().isEmpty() && KestrelApp::instance()->core()) {
            kestrel_history_add(static_cast<KestrelCore *>(KestrelApp::instance()->core()),
                                u.toString(QUrl::RemoveFragment).toUtf8().constData(),
                                title.toUtf8().constData(),
                                u.host().toUtf8().constData());
        }
    });
}

void KestrelPage::applyPrivacyScripts(const QUrl &url) {
    if (url.scheme() != "http" && url.scheme() != "https") return;

    // 1) Fingerprint protection
    const int mode = KestrelApp::instance()->getSetting("fingerprint_mode", "1").toInt();
    const QString fpName = QStringLiteral("__kestrel_fp__");
    // remove previous fp script, re-add if active
    for (const QWebEngineScript &s : scripts().toList()) {
        if (s.name() == fpName) scripts().remove(s);
    }
    if (mode > 0) {
        QWebEngineScript s = makeScript(fpName, fpScript(mode), QWebEngineScript::DocumentCreation);
        scripts().insert(s);
    }
}

bool KestrelPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) {
    Q_UNUSED(type);
    if (isMainFrame && (url.scheme() == "mailto" || url.scheme() == "tel")) {
        return false;  // no external handlers hard-wired: avoid surprise launches
    }
    return true;
}


