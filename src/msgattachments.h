#ifndef MSGATTACHMENTS_H
#define MSGATTACHMENTS_H

#include <QString>
#include <QStringList>

// Reads an Outlook .msg file and saves its real attachments into a folder.
// Inline pictures (e-mail signatures, images shown in the message text) and
// hidden attachments are skipped; attached e-mails are saved as .msg.
namespace MsgAttachments {

struct Result {
    QStringList written;  // paths of the files saved
    int skipped = 0;      // inline/hidden items left out
    bool ok = false;      // the file could be read as a .msg
};

Result extract(const QString &msgPath, const QString &targetDir);

}

#endif // MSGATTACHMENTS_H
