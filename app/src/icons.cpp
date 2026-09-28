// icons.cpp — original minimal stroke icons (Kestrel icon set, MIT).
#include "icons.h"
#include "app.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QHash>

namespace Icons {

struct Def { const char *body; };
static const QHash<QString, QString> &defs() {
    static const QHash<QString, QString> d = {
        // navigation
        {"back", "<path d='M19 12H5'/><path d='m12 19-7-7 7-7'/>"},
        {"forward", "<path d='M5 12h14'/><path d='m12 5 7 7-7 7'/>"},
        {"reload", "<path d='M21 12a9 9 0 1 1-2.64-6.36L21 8'/><path d='M21 3v5h-5'/>"},
        {"stop", "<path d='M18 6 6 18'/><path d='m6 6 12 12'/>"},
        {"home", "<path d='m3 10 9-7 9 7v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/><path d='M9 21v-7h6v7'/>"},
        // tabs
        {"plus", "<path d='M12 5v14'/><path d='M5 12h14'/>"},
        {"close", "<path d='M18 6 6 18'/><path d='m6 6 12 12'/>"},
        {"menu", "<circle cx='12' cy='5' r='1.6'/><circle cx='12' cy='12' r='1.6'/><circle cx='12' cy='19' r='1.6'/>"},
        {"incognito", "<path d='M2 12h20'/><circle cx='7.5' cy='16.5' r='2.5'/><circle cx='16.5' cy='16.5' r='2.5'/><path d='M5 12 7.5 5h9L19 12'/>"},
        // privacy
        {"shield", "<path d='M12 22s8-3.5 8-10V5l-8-3-8 3v7c0 6.5 8 10 8 10z'/>"},
        {"shieldcheck", "<path d='M12 22s8-3.5 8-10V5l-8-3-8 3v7c0 6.5 8 10 8 10z'/><path d='m9 11.5 2 2 4-4.5'/>"},
        {"star", "<path d='m12 3 2.7 5.7 6.3.9-4.5 4.4 1 6.2-5.5-3-5.5 3 1-6.2L3 9.6l6.3-.9z'/>"},
        {"starfilled", "<path d='m12 3 2.7 5.7 6.3.9-4.5 4.4 1 6.2-5.5-3-5.5 3 1-6.2L3 9.6l6.3-.9z' fill='FILL' stroke='FILL'/>"},
        // tools
        {"download", "<path d='M12 3v12'/><path d='m7 11 5 5 5-5'/><path d='M5 21h14'/>"},
        {"history", "<circle cx='12' cy='12' r='9'/><path d='M12 7v5l3.5 2'/>"},
        {"settings", "<circle cx='12' cy='12' r='3'/><path d='M19.4 15a1.7 1.7 0 0 0 .34 1.87l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.7 1.7 0 0 0-1.87-.34 1.7 1.7 0 0 0-1 1.55V21a2 2 0 1 1-4 0v-.09a1.7 1.7 0 0 0-1-1.55 1.7 1.7 0 0 0-1.87.34l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.7 1.7 0 0 0 .34-1.87 1.7 1.7 0 0 0-1.55-1H3a2 2 0 1 1 0-4h.09a1.7 1.7 0 0 0 1.55-1 1.7 1.7 0 0 0-.34-1.87l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.7 1.7 0 0 0 1.87.34h0a1.7 1.7 0 0 0 1-1.55V3a2 2 0 1 1 4 0v.09a1.7 1.7 0 0 0 1 1.55h0a1.7 1.7 0 0 0 1.87-.34l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.7 1.7 0 0 0-.34 1.87v0a1.7 1.7 0 0 0 1.55 1H21a2 2 0 1 1 0 4h-.09a1.7 1.7 0 0 0-1.55 1z'/>"},
        {"search", "<circle cx='11' cy='11' r='7'/><path d='m21 21-4.3-4.3'/>"},
        {"lock", "<rect x='5' y='11' width='14' height='9' rx='2'/><path d='M8 11V7a4 4 0 0 1 8 0v4'/>"},
        {"insecure", "<rect x='5' y='11' width='14' height='9' rx='2'/><path d='M8 11V7a4 4 0 0 1 7.5-1.8'/>"},
        {"globe", "<circle cx='12' cy='12' r='9'/><path d='M3 12h18'/><path d='M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z'/>"},
        {"trash", "<path d='M3 6h18'/><path d='M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2'/><path d='M6 6v13a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V6'/><path d='M10 11v6'/><path d='M14 11v6'/>"},
        {"pin", "<path d='M12 17v5'/><path d='M9 10.8V6a3 3 0 0 1 6 0v4.8l2.4 3.2a1 1 0 0 1-.8 1.6H7.4a1 1 0 0 1-.8-1.6z'/>"},
        {"mute", "<path d='M11 5 6 9H3v6h3l5 4z' fill='FILL'/><path d='m22 9-6 6'/><path d='m16 9 6 6'/>"},
        {"volume", "<path d='M11 5 6 9H3v6h3l5 4z' fill='FILL'/><path d='M15.5 8.5a5 5 0 0 1 0 7'/><path d='M18.5 5.5a9 9 0 0 1 0 13'/>"},
        {"code", "<path d='m16 18 6-6-6-6'/><path d='m8 6-6 6 6 6'/>"},
        {"print", "<path d='M6 9V3h12v6'/><rect x='3' y='9' width='18' height='8' rx='2'/><path d='M6 14h12v7H6z'/>"},
        {"save", "<path d='M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z'/><path d='M17 21v-8H7v8'/><path d='M7 3v5h8'/>"},
        {"external", "<path d='M15 3h6v6'/><path d='M10 14 21 3'/><path d='M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6'/>"},
        {"folder", "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"},
        {"file", "<path d='M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z'/><path d='M14 2v6h6'/>"},
        {"zoomin", "<circle cx='11' cy='11' r='7'/><path d='m21 21-4.3-4.3'/><path d='M11 8v6'/><path d='M8 11h6'/>"},
        {"zoomout", "<circle cx='11' cy='11' r='7'/><path d='m21 21-4.3-4.3'/><path d='M8 11h6'/>"},
        {"users", "<circle cx='9' cy='8' r='3.5'/><path d='M2.5 20a6.5 6.5 0 0 1 13 0'/><path d='M16 4.8a3.5 3.5 0 0 1 0 6.4'/><path d='M17.8 14a6.5 6.5 0 0 1 3.7 6'/>"},
        {"camera", "<rect x='3' y='7' width='18' height='13' rx='2'/><circle cx='12' cy='13.5' r='3.5'/><path d='M9 7l1.2-3h3.6L15 7'/>"},
        {"bell", "<path d='M6 9a6 6 0 0 1 12 0c0 5 2 6 2 6H4s2-1 2-6'/><path d='M10 20a2 2 0 0 0 4 0'/>"},
        {"clipboard", "<rect x='5' y='4' width='14' height='17' rx='2'/><path d='M9 4a2 2 0 0 1 6 0' />"},
        {"location", "<path d='M12 21s-7-6.2-7-11a7 7 0 0 1 14 0c0 4.8-7 11-7 11z'/><circle cx='12' cy='10' r='2.5'/>"},
        {"duplicate", "<rect x='8' y='8' width='13' height='13' rx='2'/><path d='M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3'/>"},
        {"restore", "<path d='M3 12a9 9 0 1 0 2.64-6.36L3 8'/><path d='M3 3v5h5'/><path d='M12 7v5l3.5 2'/>"},
        {"fullscreen", "<path d='M8 3H5a2 2 0 0 0-2 2v3'/><path d='M16 3h3a2 2 0 0 1 2 2v3'/><path d='M8 21H5a2 2 0 0 1-2-2v-3'/><path d='M16 21h3a2 2 0 0 0 2-2v-3'/>"},
        {"eyedropper", "<path d='m2 22 1-1h3l9-9'/><path d='M3 21v-3l9-9'/><path d='m15 6 3 3-9 9-3-3z'/><path d='m14 4 6 6 2-2a2.8 2.8 0 0 0-4-4z'/>"},
        {"group", "<circle cx='8' cy='8' r='4'/><circle cx='17' cy='10' r='3'/><path d='M2 20a6 6 0 0 1 12 0'/><path d='M14.5 20a4.5 4.5 0 0 1 7.5-3.4'/>"},
        {"rocket", "<path d='M5 13c-1.5 1.5-2 5-2 5s3.5-.5 5-2'/><path d='M12 15l-3-3c.6-3.9 3-8 9-9 1 0 2 1 2 2-1 6-5.1 8.4-9 9z'/><path d='M9 12H5.5L4 9.5C5.5 8 7.5 7.5 9 7.5'/><path d='M12 15v3.5L14.5 20c1.5-1.5 2-3.5 2-5'/>"},
        {"copy", "<rect x='9' y='9' width='12' height='12' rx='2'/><path d='M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1'/>"},
        {"check", "<path d='M20 6 9 17l-5-5'/>"},
        {"arrowup", "<path d='M12 19V5'/><path d='m5 12 7-7 7 7'/>"},
        {"kestralogo", "<path d='M4 3l9 9'/><path d='M20 4c-5 1-9.5 4-11.5 9.5L20 20c-1-6 .5-11 0-16z' fill='FILL'/>"},
    };
    return d;
}

QIcon get(const QString &name, const QString &color, int size) {
    const QString body = defs().value(name);
    if (body.isEmpty())
        return QIcon();
    const QString fill = color;
    QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%1' viewBox='0 0 24 24'"
        " fill='none' stroke='%2' stroke-width='1.9' stroke-linecap='round' stroke-linejoin='round'>%3</svg>")
        .arg(size * 2).arg(color, body);
    svg.replace("FILL", fill);
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pm(size * 2, size * 2);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size * 2, size * 2));
    return QIcon(pm);
}

QIcon themed(const QString &name, int size) {
    KestrelApp *a = KestrelApp::instance();
    const QString c = (a && a->isDark()) ? QStringLiteral("#C7CCDB") : QStringLiteral("#4A5060");
    return get(name, c, size);
}

QIcon accent(const QString &name, int size) {
    KestrelApp *a = KestrelApp::instance();
    return get(name, a ? a->accent() : QStringLiteral("#4D9FFF"), size);
}

} // namespace Icons
