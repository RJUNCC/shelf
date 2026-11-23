#pragma once

#include "filepicker.h"

#include <QVariantMap>

// Opens the desktop's own file chooser through xdg-desktop-portal
// (org.freedesktop.portal.FileChooser), so the dialog matches the rest of
// Omarchy and works inside sandboxes.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;
    void pickBooks(const QString &title) override;

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    QString m_requestPath;
};
