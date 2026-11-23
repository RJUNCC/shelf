#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <atomic>

// Renders PDF pages cleaned up for reading: the paper of a scan made white
// and its ink dark, and blurry scans sharpened, without changing the page
// itself. Requested as image://cleanpage/<percent-encoded path>/<page>/<mode>
// with mode "clean" or "sharp", at the size asked for.
class CleanPageProvider : public QQuickAsyncImageProvider {
public:
    CleanPageProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // The cleaning, kept apart so tests can try it on any image: the paper
    // colour becomes white (which also removes a yellow tint) and the
    // darkest ink black, stretching what lies between.
    static QImage clean(const QImage &page);

    // How blurry an image is: the variance of its brightness Laplacian.
    // Sharp text scores in the thousands; soft scans in the low hundreds.
    static double sharpness(const QImage &image);

    // Sharpens soft text with an unsharp mask, more for blurrier images,
    // and darkens faint strokes a little. Meant for an image rendered at
    // twice the size it will be shown, then scaled down.
    static QImage sharpen(const QImage &image);

    // A page cleaned (from the disk cache when it is there), at about
    // `width` pixels; stops early once `cancelled` is set.
    static QImage cleanedPage(const QString &path, int page, const QString &mode, int width,
                              const std::atomic_bool *cancelled);

    // Cleans pages first…last in the background, at low priority, into the
    // disk cache, replacing any earlier request.
    static void cleanAhead(const QString &path, int first, int last, const QString &mode, int width);

    // The tilt of a scanned page's text lines, in degrees (within ±3), from
    // the angle at which rows of ink line up best; 0 when it cannot tell.
    static double skewAngle(const QImage &page);
    // The page turned level, on white, at its original size.
    static QImage deskew(const QImage &page);
    // Dark bands along the edges of a scan (the scanner lid, a page's
    // shadow) painted out in the paper's colour.
    static QImage clearBorders(const QImage &page);

    // Evens out the paper: spine shadows, uneven lighting, and yellowing
    // that changes across the page are divided out, by estimating the
    // paper's brightness everywhere with the text filtered away.
    static QImage flattenBackground(const QImage &page);

    // Keeps the on-disk cache of cleaned pages under `limit` bytes,
    // removing the oldest.
    static void trimCache(qint64 limit);

private:
    QThreadPool m_pool;
};
