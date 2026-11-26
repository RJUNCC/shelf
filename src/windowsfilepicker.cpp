#include "windowsfilepicker.h"

#include <QDir>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#include <windows.h>
#include <shobjidl.h>

namespace {
// Releases a COM pointer when it goes out of scope.
template <typename T> struct ComPtr {
    T *p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T *operator->() const { return p; }
};
} // namespace

void WindowsFilePicker::pickBooks(const QString &title) {
    // The dialog runs its own message loop; start it after this call returns
    // so QML isn't in the middle of handling a click.
    QTimer::singleShot(0, this, [this, title]() {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        QStringList paths;
        QString failure;
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog.p)))) {
            failure = tr("Windows could not show the Open dialog.");
        } else {
            const COMDLG_FILTERSPEC books[] = {{L"Books (*.epub, *.pdf)", L"*.epub;*.pdf"},
                                               {L"EPUB", L"*.epub"}, {L"PDF", L"*.pdf"}};
            const COMDLG_FILTERSPEC decks[] = {{L"Slides (*.pdf, *.pptx)", L"*.pdf;*.pptx"},
                                               {L"PDF", L"*.pdf"}, {L"PowerPoint", L"*.pptx"}};
            const COMDLG_FILTERSPEC documents[] = {
                {L"Documents (*.pdf, *.docx, *.odt, *.html, *.txt, *.md)", L"*.pdf;*.docx;*.odt;*.html;*.htm;*.txt;*.md"}};
            const COMDLG_FILTERSPEC backups[] = {{L"Shelf backups (*.shelfbackup)", L"*.shelfbackup"}};
            if (kind == "documents")
                dialog->SetFileTypes(1, documents);
            else if (kind == "backup")
                dialog->SetFileTypes(1, backups);
            else if (kind != "folder")
                dialog->SetFileTypes(3, kind == "slides" ? decks : books);
            DWORD options = 0;
            dialog->GetOptions(&options);
            options |= FOS_FORCEFILESYSTEM;
            if (kind == "folder")
                options |= FOS_PICKFOLDERS;
            else
                options |= FOS_FILEMUSTEXIST | (kind == "books" || kind == "slides" ? FOS_ALLOWMULTISELECT : 0);
            dialog->SetOptions(options);
            dialog->SetTitle(reinterpret_cast<LPCWSTR>(title.utf16()));
            HWND owner = nullptr;
            if (QWindow *window = QGuiApplication::focusWindow())
                owner = reinterpret_cast<HWND>(window->winId());
            if (SUCCEEDED(dialog->Show(owner))) {
                ComPtr<IShellItemArray> items;
                if (SUCCEEDED(dialog->GetResults(&items.p))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i) {
                        ComPtr<IShellItem> item;
                        PWSTR name = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, &item.p))
                            && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                            paths << QDir::fromNativeSeparators(QString::fromWCharArray(name));
                            CoTaskMemFree(name);
                        }
                    }
                }
            }
            // Cancelling is not a failure: nothing is added.
        }
        if (SUCCEEDED(init))
            CoUninitialize();
        if (!failure.isEmpty())
            emit failed(failure);
        else if (!paths.isEmpty())
            emit picked(paths);
    });
}
