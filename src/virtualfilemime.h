#ifndef VIRTUALFILEMIME_H
#define VIRTUALFILEMIME_H

#include <QString>

// Mime type under which dropped "virtual" files (Outlook attachments and
// mails) are offered; its data is the extracted paths, one per line (UTF-8).
// See VirtualFileMimeConverter.
namespace VirtualFileMime {
inline const QString MimeType = QStringLiteral("application/x-tokri-virtual-files");
}

#endif // VIRTUALFILEMIME_H
