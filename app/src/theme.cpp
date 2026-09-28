// theme.cpp — the Kestrel Design System as Qt stylesheets.
#include "theme.h"

Theme::Palette Theme::palette(const QString &theme, const QString &accent) {
    Palette p;
    if (theme == "light") {
        p.window     = "#F4F5F8";
        p.surface    = "#FFFFFF";
        p.elevated   = "#FFFFFF";
        p.border     = "#E2E5EC";
        p.text       = "#1A1D24";
        p.muted      = "#5F6675";
        p.accent     = accent;
        p.accentSoft = accent + "22";
        p.danger     = "#D8404F";
        p.success    = "#1F9D66";
        p.warning    = "#B57B1E";
        p.tooltipBg  = "#23252C";
        p.tooltipText= "#E9EBF2";
        p.menuShadow = "rgba(0,0,0,0.18)";
        p.scrollbar  = "#C9CDD6";
    } else {
        p.window     = "#17181C";
        p.surface    = "#1E2026";
        p.elevated   = "#262933";
        p.border     = "#313543";
        p.text       = "#E9EBF2";
        p.muted      = "#9BA3B7";
        p.accent     = accent;
        p.accentSoft = accent + "24";
        p.danger     = "#FF5C6C";
        p.success    = "#3DD68C";
        p.warning    = "#FFB454";
        p.tooltipBg  = "#262933";
        p.tooltipText= "#E9EBF2";
        p.menuShadow = "rgba(0,0,0,0.55)";
        p.scrollbar  = "#3A3E4C";
    }
    return p;
}

QString Theme::buildStyleSheet(const QString &theme, const QString &accent) {
    const Palette p = palette(theme, accent);
    const QString font = QStringLiteral(
        "\"Noto Sans\",\"Segoe UI Variable\",\"Segoe UI\",\"SF Pro Text\",system-ui,sans-serif");

    QString qss = R"(
* { outline: none; }
QWidget {
    background: %WINDOW%; color: %TEXT%;
    font-family: %FONT%; font-size: 13px;
}
QMainWindow, #RootWindow { background: %WINDOW%; }

/* ---------- Tab strip ---------- */
#TabStrip {
    background: %WINDOW%;
    border-bottom: 1px solid %BORDER%;
}
#TabStrip QScrollArea { border: none; background: transparent; }
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    padding: 0px;
    border: none;
    min-height: 34px;
}
QTabBar { background: transparent; }

/* ---------- Toolbar ---------- */
#ToolBar { background: %SURFACE%; border: none; border-bottom: 1px solid %BORDER%; }
#ToolBar QToolButton {
    background: transparent; border: none; border-radius: 8px;
    padding: 5px; margin: 3px 1px;
}
#ToolBar QToolButton:hover { background: %ACCENT_SOFT%; }
#ToolBar QToolButton:pressed { background: %ACCENT_SOFT%; }
#ToolBar QToolButton:disabled { color: %MUTED%; }

/* ---------- Omnibox ---------- */
#Omnibox {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 18px;
    padding: 4px 12px;
    color: %TEXT%;
    selection-background-color: %ACCENT%;
    font-size: 13px;
}
#Omnibox:focus { border: 1px solid %ACCENT%; background: %WINDOW%; }

#ShieldButton, #BookmarkStar {
    border-radius: 8px; padding: 4px; background: transparent; border: none;
}
#ShieldButton:hover, #BookmarkStar:hover { background: %ACCENT_SOFT%; }
#BlockedBadge {
    background: %ACCENT%; color: #FFFFFF;
    border-radius: 7px; padding: 1px 5px; font-size: 10px; font-weight: 600;
}

/* ---------- Menus ---------- */
QMenu {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 6px;
}
QMenu::item {
    padding: 7px 24px 7px 12px;
    border-radius: 7px;
    background: transparent;
    color: %TEXT%;
}
QMenu::item:selected { background: %ACCENT_SOFT%; }
QMenu::item:disabled { color: %MUTED%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 5px 8px; }
QMenu::right-arrow { width: 8px; height: 8px; }

/* ---------- Find bar ---------- */
#FindBar { background: %SURFACE%; border: 1px solid %BORDER%; border-radius: 10px; }
#FindBar QLineEdit { background: transparent; border: none; color: %TEXT%; min-width: 160px; }

/* ---------- Scrollbars ---------- */
QScrollBar:vertical { background: transparent; width: 11px; margin: 2px; }
QScrollBar::handle:vertical { background: %SCROLLBAR%; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %MUTED%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 2px; }
QScrollBar::handle:horizontal { background: %SCROLLBAR%; border-radius: 4px; min-width: 30px; }

/* ---------- Tooltips ---------- */
QToolTip {
    background: %TOOLTIP_BG%; color: %TOOLTIP_TEXT%;
    border: 1px solid %BORDER%; border-radius: 6px; padding: 5px 8px; font-size: 12px;
}

/* ---------- Dialogs / inputs ---------- */
QDialog, QMessageBox { background: %SURFACE%; }
QLineEdit, QSpinBox, QDoubleSpinBox {
    background: %ELEVATED%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 5px 8px; color: %TEXT%;
}
QLineEdit:focus { border-color: %ACCENT%; }
QPushButton {
    background: %ELEVATED%; color: %TEXT%;
    border: 1px solid %BORDER%; border-radius: 8px; padding: 6px 14px;
}
QPushButton:hover { border-color: %ACCENT%; color: %ACCENT%; }
QPushButton#PrimaryButton { background: %ACCENT%; color: #FFFFFF; border: none; font-weight: 600; }
QPushButton#PrimaryButton:hover { background: %ACCENT%; }
QComboBox { background: %ELEVATED%; border: 1px solid %BORDER%; border-radius: 8px; padding: 4px 8px; }
QComboBox QAbstractItemView { background: %ELEVATED%; border: 1px solid %BORDER%; selection-background-color: %ACCENT_SOFT%; }

/* ---------- Completer popup ---------- */
#OmniboxCompleter, QListView#OmniboxCompleter {
    background: %ELEVATED%; border: 1px solid %BORDER%; border-radius: 10px; padding: 4px;
}
#OmniboxCompleter::item { padding: 6px 8px; border-radius: 7px; color: %TEXT%; }
#OmniboxCompleter::item:selected { background: %ACCENT_SOFT%; }

/* ---------- Status bar ---------- */
#StatusBar { background: %SURFACE%; color: %MUTED%; border-top: 1px solid %BORDER%; }
)";
    qss.replace("%WINDOW%", p.window).replace("%SURFACE%", p.surface)
       .replace("%ELEVATED%", p.elevated).replace("%BORDER%", p.border)
       .replace("%TEXT%", p.text).replace("%MUTED%", p.muted)
       .replace("%ACCENT%", p.accent).replace("%ACCENT_SOFT%", p.accentSoft)
       .replace("%DANGER%", p.danger).replace("%SUCCESS%", p.success)
       .replace("%WARNING%", p.warning).replace("%TOOLTIP_BG%", p.tooltipBg)
       .replace("%TOOLTIP_TEXT%", p.tooltipText).replace("%SCROLLBAR%", p.scrollbar)
       .replace("%FONT%", font);
    return qss;
}
