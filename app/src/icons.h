// icons.h — original stroke-icon set, theme-aware recoloring.
#pragma once
#include <QIcon>
#include <QString>

namespace Icons {
// name examples: back forward reload stop home plus close menu shield shieldcheck
// star starfilled download history settings search lock insecure globe trash pin
// mute volume incognito code print save zoomin zoomout external folder file
QIcon get(const QString &name, const QString &color, int size = 18);
// Convenience: uses current theme text color
QIcon themed(const QString &name, int size = 18);
// accent-colored
QIcon accent(const QString &name, int size = 18);
} // namespace Icons
