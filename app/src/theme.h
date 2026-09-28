// theme.h — QSS generation for the Kestrel design system (dark/light + accent).
#pragma once
#include <QString>
#include <QHash>

namespace Theme {
struct Palette {
    QString window, surface, elevated, border, text, muted, accent, accentSoft,
            danger, success, warning, tooltipBg, tooltipText, menuShadow, scrollbar;
};
Palette palette(const QString &theme, const QString &accent);
QString buildStyleSheet(const QString &theme, const QString &accent);
} // namespace Theme
