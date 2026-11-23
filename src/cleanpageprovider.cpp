#include "cleanpageprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QPainter>
#include <QPdfDocument>
#include <QRunnable>
#include <QUrl>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <QtMath>
#include <memory>

namespace {

// Cleaned pages are kept on disk, so a page is cleaned once and scrolling
// back to it, or reopening the book, is instant.
QString cacheDirectory() {
    static const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/pages";
    return dir;
}

// Widths are rounded up to steps, so a slightly different window size
// reuses what was cleaned rather than cleaning again.
int bucket(int width) { return std::max(160, (width + 159) / 160 * 160); }

// What a request and the job working on it share. The response may be
// cancelled (scrolled past) or deleted before the job runs.
struct Shared {
    QMutex mutex;
    QQuickImageResponse *response = nullptr;
    std::atomic_bool cancelled = false;
    QImage image;
};

class Response : public QQuickImageResponse {
public:
    explicit Response(std::shared_ptr<Shared> shared) : m_shared(std::move(shared)) {
        m_shared->response = this;
    }
    ~Response() override {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->response = nullptr;
    }
    QQuickTextureFactory *textureFactory() const override {
        QMutexLocker lock(&m_shared->mutex);
        return QQuickTextureFactory::textureFactoryForImage(m_shared->image);
    }
    void cancel() override { m_shared->cancelled = true; }

private:
    std::shared_ptr<Shared> m_shared;
};

class Job : public QRunnable {
public:
    Job(std::shared_ptr<Shared> shared, const QString &id, const QSize &size)
        : m_shared(std::move(shared)), m_id(id), m_size(size) {}

    void run() override {
        // A page scrolled past before its turn is not worth cleaning.
        if (!m_shared->cancelled) {
            const QImage image = produce();
            QMutexLocker lock(&m_shared->mutex);
            m_shared->image = image;
        }
        QMutexLocker lock(&m_shared->mutex);
        if (m_shared->response)
            QMetaObject::invokeMethod(m_shared->response, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

private:
    QImage produce() {
        return CleanPageProvider::cleanedPage(QUrl::fromPercentEncoding(m_id.section('/', 0, -3).toUtf8()),
                                              m_id.section('/', -2, -2).toInt(), m_id.section('/', -1),
                                              m_size.width() > 0 ? m_size.width() : 1200, &m_shared->cancelled);
    }

    std::shared_ptr<Shared> m_shared;
    QString m_id;
    QSize m_size;
};

} // namespace

QImage CleanPageProvider::cleanedPage(const QString &path, int page, const QString &mode, int requestedWidth,
                                      const std::atomic_bool *cancelled) {
        const bool sharp = mode == "sharp";
        const int width = bucket(requestedWidth);

        const QString key = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
        const QString file = QStringLiteral("%1/%2/%3-%4-%5.jpg").arg(cacheDirectory(), key).arg(page).arg(mode).arg(width);
        // The cache is only good while the PDF is unchanged.
        const QFileInfo pdf(path), cached(file);
        if (cached.exists() && cached.lastModified() >= pdf.lastModified()) {
            QImage image(file);
            if (!image.isNull())
                return image;
        }

        // Each worker thread keeps its own open documents.
        thread_local QHash<QString, std::shared_ptr<QPdfDocument>> documents;
        auto &doc = documents[path];
        if (!doc) {
            doc = std::make_shared<QPdfDocument>();
            doc->load(path);
        }
        if (doc->status() != QPdfDocument::Status::Ready || page < 0 || page >= doc->pageCount())
            return {};
        const QSizeF points = doc->pagePointSize(page);
        const QSize size(width, int(points.height() * width / std::max<qreal>(1, points.width())));
        const QImage rendered = doc->render(page, size);
        if (cancelled && *cancelled)
            return {};
        // Transparent pages read as black; put them on white first.
        QImage paper(rendered.size(), QImage::Format_RGB32);
        paper.fill(Qt::white);
        QPainter(&paper).drawImage(0, 0, rendered);
        // Straighten and trim before cleaning, as scan tidiers do.
        paper = CleanPageProvider::flattenBackground(CleanPageProvider::clearBorders(CleanPageProvider::deskew(paper)));
        // Sharpening works at twice the size, then scales back down, which
        // smooths the edges it brings out. Enlarging the rendered page is
        // as good as rendering it twice as large for a scan, at well under
        // half the cost.
        QImage image = sharp ? CleanPageProvider::clean(CleanPageProvider::sharpen(
                                   paper.scaledToWidth(width * 2, Qt::SmoothTransformation)))
                                   .scaledToWidth(width, Qt::SmoothTransformation)
                             : CleanPageProvider::clean(paper);
        QDir().mkpath(QFileInfo(file).path());
        image.save(file, "JPG", 92);
        return image;
    }

namespace {
// Pages cleaned ahead of the reader, one at a time at low priority, so
// they are ready (on disk) by the time they scroll into view.
QThreadPool &aheadPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        p->setThreadPriority(QThread::LowPriority);
        return p;
    }();
    return *pool;
}
QMutex aheadMutex;
QSet<QString> aheadQueued;
} // namespace

void CleanPageProvider::cleanAhead(const QString &path, int first, int last, const QString &mode, int width) {
    if (mode.isEmpty() || path.isEmpty())
        return;
    // Only the newest request matters: what is still waiting from an older
    // position is dropped.
    aheadPool().clear();
    {
        QMutexLocker lock(&aheadMutex);
        aheadQueued.clear();
    }
    for (int page = std::max(0, first); page <= last; ++page) {
        const QString key = QStringLiteral("%1/%2/%3/%4").arg(path).arg(page).arg(mode).arg(bucket(width));
        {
            QMutexLocker lock(&aheadMutex);
            if (aheadQueued.contains(key))
                continue;
            aheadQueued.insert(key);
        }
        aheadPool().start([path, page, mode, width, key]() {
            CleanPageProvider::cleanedPage(path, page, mode, width, nullptr);
            QMutexLocker lock(&aheadMutex);
            aheadQueued.remove(key);
        });
    }
}

CleanPageProvider::CleanPageProvider() {
    // Half the cores: quick enough to keep up with scrolling, and leaves
    // the rest for rendering and the interface.
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 1, 4));
    trimCache(400ll * 1024 * 1024);
}

QQuickImageResponse *CleanPageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    auto shared = std::make_shared<Shared>();
    auto *response = new Response(shared);
    m_pool.start(new Job(shared, id, requestedSize));
    return response;
}

void CleanPageProvider::trimCache(qint64 limit) {
    // Oldest pages go first once the cache passes the limit.
    QDirIterator it(cacheDirectory(), {"*.jpg"}, QDir::Files, QDirIterator::Subdirectories);
    QList<QFileInfo> files;
    qint64 total = 0;
    while (it.hasNext()) {
        it.next();
        files << it.fileInfo();
        total += it.fileInfo().size();
    }
    if (total <= limit)
        return;
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (const QFileInfo &f : std::as_const(files)) {
        if (total <= limit * 3 / 4)
            break;
        total -= f.size();
        QFile::remove(f.filePath());
    }
}

namespace {
// Light text on a dark page: the background towards near-black, the text
// towards white, each channel from its own colour so tints stay.
QImage cleanDark(QImage image, const QList<int> &histogram, const QList<qint64> &sum, int background, int text) {
    auto colourAt = [&](int from, int to) {
        double r = 0, g = 0, b = 0, n = 0;
        for (int i = from; i <= to; ++i) {
            r += sum[i * 3];
            g += sum[i * 3 + 1];
            b += sum[i * 3 + 2];
            n += histogram[i];
        }
        n = std::max(1.0, n);
        return std::array<double, 3>{r / n, g / n, b / n};
    };
    const auto low = colourAt(std::max(0, background - 6), std::min(255, background + 6));
    const auto high = colourAt(text, 255);
    uchar lut[3][256];
    for (int c = 0; c < 3; ++c) {
        const double span = std::max(20.0, high[c] - low[c]);
        // A smooth step rather than a straight stretch: shades near the
        // background (its mottling, JPEG blocks) all become background, and
        // only the text is stretched towards white.
        for (int v = 0; v < 256; ++v) {
            const double t = std::clamp((v - low[c]) / span, 0.0, 1.0);
            const double u = std::clamp((t - 0.22) / (0.85 - 0.22), 0.0, 1.0);
            const double step = u * u * (3 - 2 * u);
            lut[c][v] = uchar(26.0 + step * (250.0 - 26.0));
        }
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            line[x] = qRgb(lut[0][qRed(p)], lut[1][qGreen(p)], lut[2][qBlue(p)]);
        }
    }
    return image;
}
} // namespace

QImage CleanPageProvider::clean(const QImage &input) {
    QImage image = input.convertToFormat(QImage::Format_RGB32);
    if (image.isNull())
        return image;
    // Brightness histogram, sampled; the paper is the bright bulk of the
    // page, the ink the darkest few percent.
    QList<int> histogram(256, 0);
    QList<qint64> sum(3 * 256, 0);   // colour sums per brightness, for the paper's tint
    const int step = std::max(1, std::min(image.width(), image.height()) / 400);
    qint64 samples = 0;
    for (int y = 0; y < image.height(); y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            const QRgb p = line[x];
            const int l = qGray(p);
            ++histogram[l];
            sum[l * 3] += qRed(p);
            sum[l * 3 + 1] += qGreen(p);
            sum[l * 3 + 2] += qBlue(p);
            ++samples;
        }
    }
    auto percentile = [&](double fraction) {
        qint64 seen = 0;
        for (int i = 0; i < 256; ++i) {
            seen += histogram[i];
            if (seen >= samples * fraction)
                return i;
        }
        return 255;
    };
    // A colourful page (a cover, a photo) is not a scan to clean: leave it.
    {
        double saturation = 0;
        qint64 n = 0;
        for (int y = 0; y < image.height(); y += step * 2) {
            const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); x += step * 2, ++n) {
                const QRgb p = line[x];
                const int hi = std::max({qRed(p), qGreen(p), qBlue(p)}), lo = std::min({qRed(p), qGreen(p), qBlue(p)});
                saturation += hi > 0 ? double(hi - lo) / hi : 0;
            }
        }
        if (n > 0 && saturation / n > 0.22)
            return image;
    }
    const int ink = percentile(0.02);
    const int paperLevel = percentile(0.60);
    const int brightest = percentile(0.985);
    // A dark page with light text (a chapter opener, say): deepen the
    // background and brighten the text instead.
    if (paperLevel < 128 && brightest - paperLevel > 50 && brightest - paperLevel > (paperLevel - ink) * 1.5)
        return cleanDark(image, histogram, sum, paperLevel, brightest);
    if (paperLevel - ink < 40)
        return image;   // a photo or a blank page: leave it alone
    // The paper's colour: the average of everything at least as bright as
    // the paper level.
    double r = 0, g = 0, b = 0, n = 0;
    for (int i = paperLevel; i < 256; ++i) {
        r += sum[i * 3];
        g += sum[i * 3 + 1];
        b += sum[i * 3 + 2];
        n += histogram[i];
    }
    const double paper[3] = {std::max(1.0, r / n), std::max(1.0, g / n), std::max(1.0, b / n)};
    // Each channel: its ink level to 0, its paper colour to 255, so tinted
    // paper turns white and colour diagrams keep their hues.
    uchar lut[3][256];
    for (int c = 0; c < 3; ++c) {
        const double low = std::min<double>(ink, paper[c] - 20);
        for (int v = 0; v < 256; ++v)
            lut[c][v] = uchar(std::clamp((v - low) * 255.0 / std::max(1.0, paper[c] - low), 0.0, 255.0));
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            line[x] = qRgb(lut[0][qRed(p)], lut[1][qGreen(p)], lut[2][qBlue(p)]);
        }
    }
    return image;
}

namespace {

// Brightness as a float plane, for filtering.
QList<float> grayPlane(const QImage &image) {
    QList<float> out(qsizetype(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            out[qsizetype(y) * image.width() + x] = float(qGray(line[x]));
    }
    return out;
}

// One pass of a box blur, along rows then columns; three passes approach a
// Gaussian.
void boxBlur(QList<float> &plane, int width, int height, int radius) {
    QList<float> tmp(plane.size());
    const float scale = 1.0f / float(2 * radius + 1);
    for (int y = 0; y < height; ++y) {
        const float *row = plane.constData() + qsizetype(y) * width;
        float sum = 0;
        for (int x = -radius; x <= radius; ++x)
            sum += row[std::clamp(x, 0, width - 1)];
        for (int x = 0; x < width; ++x) {
            tmp[qsizetype(y) * width + x] = sum * scale;
            sum += row[std::min(x + radius + 1, width - 1)] - row[std::max(x - radius, 0)];
        }
    }
    for (int x = 0; x < width; ++x) {
        float sum = 0;
        for (int y = -radius; y <= radius; ++y)
            sum += tmp[qsizetype(std::clamp(y, 0, height - 1)) * width + x];
        for (int y = 0; y < height; ++y) {
            plane[qsizetype(y) * width + x] = sum * scale;
            sum += tmp[qsizetype(std::min(y + radius + 1, height - 1)) * width + x]
                 - tmp[qsizetype(std::max(y - radius, 0)) * width + x];
        }
    }
}

} // namespace

double CleanPageProvider::sharpness(const QImage &input) {
    const QImage image = input.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 3 || h < 3)
        return 0;
    const QList<float> g = grayPlane(image);
    double sum = 0, sumSquares = 0;
    qint64 n = 0;
    const int step = std::max(1, std::min(w, h) / 600);
    for (int y = 1; y < h - 1; y += step) {
        for (int x = 1; x < w - 1; x += step) {
            const qsizetype i = qsizetype(y) * w + x;
            const double l = 4 * g[i] - g[i - 1] - g[i + 1] - g[i - w] - g[i + w];
            sum += l;
            sumSquares += l * l;
            ++n;
        }
    }
    const double mean = sum / n;
    return sumSquares / n - mean * mean;
}

QImage CleanPageProvider::sharpen(const QImage &input) {
    QImage image = input.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 8 || h < 8)
        return image;
    // Blurrier pages get more: crisp ones barely change, so a page that
    // did not need it gets no halos.
    const double score = sharpness(image);
    const double amount = std::clamp((900.0 - score) / 600.0, 0.15, 1.3);
    // The blur to subtract scales with the image (it was rendered at twice
    // the shown size, so soft edges are a few pixels wide).
    // Very soft scans spread an edge over several pixels; take more of it.
    const int radius = score < 100 ? std::max(2, std::min(w, h) / 350) : std::max(1, std::min(w, h) / 900);
    // Thicken strokes in the direction of the text: darker on light pages,
    // lighter on dark ones.
    double mean = 0;
    for (int y = 0; y < h; y += 4) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; x += 4)
            mean += qGray(line[x]);
    }
    mean /= double((h + 3) / 4) * ((w + 3) / 4);
    const double gamma = mean >= 128 ? 1.0 + 0.25 * amount : 1.0 / (1.0 + 0.25 * amount);
    // The curve as a table: pow() per pixel was most of the time spent.
    uchar curve[256];
    for (int v = 0; v < 256; ++v)
        curve[v] = uchar(std::pow(v / 255.0, gamma) * 255.0 + 0.5);
    QList<float> blurred = grayPlane(image);
    for (int pass = 0; pass < 3; ++pass)
        boxBlur(blurred, w, h, radius);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const float detail = float(qGray(p)) - blurred[qsizetype(y) * w + x];
            // Tiny differences are speckle and JPEG noise, not edges: leave
            // them, fading in towards real edges.
            const float magnitude = std::abs(detail);
            const float core = magnitude < 4 ? 0 : magnitude < 10 ? (magnitude - 4) / 6 : 1;
            const float d = float(amount) * detail * core;
            // A slight darkening of mid tones thickens faint strokes.
            auto channel = [&](int v) { return int(curve[int(std::clamp(v + d, 0.0f, 255.0f))]); };
            line[x] = qRgb(channel(qRed(p)), channel(qGreen(p)), channel(qBlue(p)));
        }
    }
    return image;
}

double CleanPageProvider::skewAngle(const QImage &input) {
    // Work shrunk: a page about 500 pixels wide is plenty to see lines.
    const QImage shrunk = input.convertToFormat(QImage::Format_Grayscale8)
                              .scaledToWidth(std::min(500, input.width()), Qt::SmoothTransformation);
    const int w = shrunk.width(), h = shrunk.height();
    if (w < 50 || h < 50)
        return 0;
    // Ink: clearly darker than the page's typical brightness.
    long long total = 0;
    for (int y = 0; y < h; ++y) {
        const uchar *line = shrunk.constScanLine(y);
        for (int x = 0; x < w; ++x)
            total += line[x];
    }
    const int threshold = int(total / (qint64(w) * h) * 0.65);
    QList<QPoint> ink;
    for (int y = 0; y < h; ++y) {
        const uchar *line = shrunk.constScanLine(y);
        for (int x = int(w * 0.05); x < int(w * 0.95); ++x)
            if (line[x] < threshold)
                ink << QPoint(x, y);
    }
    if (ink.size() < 300 || ink.size() > w * h / 3)
        return 0;   // nearly blank, or mostly dark (a photo): leave it
    // Rows of text line up sharpest at the right angle: the projection of
    // ink onto rows then has the most contrast (largest sum of squares).
    auto score = [&](double degrees) {
        const double slope = std::tan(qDegreesToRadians(degrees));
        QList<int> rows(h * 2, 0);
        for (const QPoint &p : std::as_const(ink)) {
            const int r = int(p.y() - p.x() * slope) + h / 2;
            if (r >= 0 && r < rows.size())
                ++rows[r];
        }
        double sum = 0;
        for (int n : std::as_const(rows))
            sum += double(n) * n;
        return sum;
    };
    double best = 0, bestScore = score(0);
    for (double a = -3.0; a <= 3.0001; a += 0.25) {
        const double sc = score(a);
        if (sc > bestScore) {
            bestScore = sc;
            best = a;
        }
    }
    // Refine around the best coarse angle.
    const double coarse = best;
    for (double a = coarse - 0.2; a <= coarse + 0.2001; a += 0.05) {
        const double sc = score(a);
        if (sc > bestScore) {
            bestScore = sc;
            best = a;
        }
    }
    return best;
}

QImage CleanPageProvider::deskew(const QImage &page) {
    const double angle = skewAngle(page);
    // Below a third of a degree, turning would only soften the text.
    if (std::abs(angle) < 0.3)
        return page;
    QImage out(page.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // Lines sloping down to the right (a positive angle) turn back up.
    painter.translate(page.width() / 2.0, page.height() / 2.0);
    painter.rotate(-angle);
    painter.translate(-page.width() / 2.0, -page.height() / 2.0);
    painter.drawImage(0, 0, page);
    painter.end();
    return out;
}

QImage CleanPageProvider::clearBorders(const QImage &input) {
    QImage image = input.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 50 || h < 50)
        return image;
    auto columnMean = [&](int x) {
        long long sum = 0;
        for (int y = 0; y < h; y += 4)
            sum += qGray(image.pixel(x, y));
        return double(sum) / ((h + 3) / 4);
    };
    auto rowMean = [&](int y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        long long sum = 0;
        for (int x = 0; x < w; x += 4)
            sum += qGray(line[x]);
        return double(sum) / ((w + 3) / 4);
    };
    // The paper: how bright the brightest tenth of the page's middle is
    // (text and pictures are darker).
    QList<int> middle;
    for (int y = h / 5; y < h * 4 / 5; y += 6) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = w / 5; x < w * 4 / 5; x += 6)
            middle << qGray(line[x]);
    }
    std::sort(middle.begin(), middle.end());
    const double paper = middle.isEmpty() ? 0 : middle[middle.size() * 9 / 10];
    if (paper < 128)
        return image;   // a dark page has no paper edges to find
    // A band counts as border while it is much darker than the paper, up
    // to 8% in from each edge; text columns are never that dark on average.
    const double dark = paper * 0.45;
    int left = 0, right = 0, top = 0, bottom = 0;
    while (left < w * 0.08 && columnMean(left) < dark) ++left;
    while (right < w * 0.08 && columnMean(w - 1 - right) < dark) ++right;
    while (top < h * 0.08 && rowMean(top) < dark) ++top;
    while (bottom < h * 0.08 && rowMean(h - 1 - bottom) < dark) ++bottom;
    if (left + right + top + bottom == 0)
        return image;
    // Paint out a little past the band, where its shadow fades.
    const QColor colour = QColor::fromRgb(qRgb(int(paper), int(paper), int(paper)));
    auto pad = [](int n) { return n > 0 ? n + 3 : 0; };
    QPainter painter(&image);
    if (left) painter.fillRect(0, 0, pad(left), h, colour);
    if (right) painter.fillRect(w - pad(right), 0, pad(right), h, colour);
    if (top) painter.fillRect(0, 0, w, pad(top), colour);
    if (bottom) painter.fillRect(0, h - pad(bottom), w, pad(bottom), colour);
    return image;
}

QImage CleanPageProvider::flattenBackground(const QImage &input) {
    QImage image = input.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 64 || h < 64)
        return image;
    // A shrunk copy, about 1/12 the size: each pixel then covers a few
    // letters. Taking the brightest of each neighbourhood (twice) removes
    // the text and leaves the paper; a blur smooths it.
    const int sw = std::max(8, w / 12), sh = std::max(8, h / 12);
    QImage shrunk = image.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // A dark page (light text) has no paper to flatten.
    long long total = 0;
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x)
            total += qGray(shrunk.pixel(x, y));
    if (total / (qint64(sw) * sh) < 110)
        return image;
    for (int pass = 0; pass < 2; ++pass) {
        QImage next = shrunk;
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                int r = 0, g = 0, b = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const QRgb p = shrunk.pixel(std::clamp(x + dx, 0, sw - 1), std::clamp(y + dy, 0, sh - 1));
                        r = std::max(r, qRed(p));
                        g = std::max(g, qGreen(p));
                        b = std::max(b, qBlue(p));
                    }
                next.setPixel(x, y, qRgb(r, g, b));
            }
        }
        shrunk = next;
    }
    const QImage background = shrunk.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // The open paper's brightness, to tell a shadow (somewhat darker) from
    // a solid black box or photo (far darker), which must stay as it is.
    QList<int> levels;
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x)
            levels << qGray(shrunk.pixel(x, y));
    std::sort(levels.begin(), levels.end());
    const double open = std::max(1, levels[levels.size() * 9 / 10]);
    // Divide out the paper, keeping its overall brightness: where the paper
    // is shaded, the page brightens to match the rest; content far darker
    // than paper is left alone, fading between the two.
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *paper = reinterpret_cast<const QRgb *>(background.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x], q = paper[x];
            const double t = std::clamp((qGray(q) / open - 0.35) / (0.6 - 0.35), 0.0, 1.0);
            const double weight = t * t * (3 - 2 * t);
            auto even = [&](int v, int bg) {
                const double flat = std::min(255.0, v * 250.0 / std::max(40, bg));
                return int(v + (flat - v) * weight);
            };
            line[x] = qRgb(even(qRed(p), qRed(q)), even(qGreen(p), qGreen(q)), even(qBlue(p), qBlue(q)));
        }
    }
    return image;
}
