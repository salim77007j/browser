// internal_pages.h — HTML generators for kestrel:// pages
#pragma once
#include <QString>
#include <QUrl>

namespace InternalPages {
// path like "kestrel://newtab" — returns full HTML
QString render(const QUrl &url, bool isPrivate);

// shared helpers
QString baseCss();
QString qwebchannelJs();
} // namespace InternalPages
