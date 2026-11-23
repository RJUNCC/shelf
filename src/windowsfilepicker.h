#pragma once

#include "filepicker.h"

// Asks for books with Windows' own Open dialog (IFileOpenDialog), so Shelf
// needs no widgets on Windows either.
class WindowsFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;
    void pickBooks(const QString &title) override;
};
