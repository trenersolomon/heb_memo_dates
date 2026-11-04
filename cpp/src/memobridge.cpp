#include "memobridge.h"
#include "translator.h"

#include <QDebug>
#include <QDir>
#include <QUrl>

#include <cstdio>   // sscanf
#include <cstdlib>  // free
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

// The widget runs inside plasmashell's GUI thread. csv.h's background reader
// thread would be pure overhead for a tiny file, and a synchronous reader
// avoids any thread-lifetime questions inside a long-lived shell process.
#define CSV_IO_NO_THREAD
#include "csv.h"

#include <hdate.h>

namespace {

// Years above this value in the CSV mean "Hebrew date", otherwise Gregorian.
// (Same convention as in the original console program.)
constexpr int kHebrewYearThreshold = 5000;

// Safety cap for the holiday search so a libhdate oddity can never turn the
// loop into an endless one inside plasmashell (which would freeze the desktop).
constexpr int kMaxHolidayLookahead = 400;

// Refresh check period: every 30 minutes (as requested).
constexpr int kTimerIntervalMs = 30 * 60 * 1000;

// ─── RAII wrapper for libhdate objects ────────────────────────────────────────
// new_hdate() allocates with calloc(), so free() is the matching release.
// RAII guarantees no leak even when an exception is thrown mid-way, which the
// original code (manual free) did not guarantee.
struct FreeDeleter {
    void operator()(hdate_struct *p) const { std::free(p); }
};
using HDate = std::unique_ptr<hdate_struct, FreeDeleter>;

HDate makeHDate()
{
    hdate_struct *p = new_hdate();
    if (!p)
        throw std::bad_alloc();
    return HDate(p);
}

// ─── Formatting helpers ───────────────────────────────────────────────────────

QString gregorianText(const hdate_struct *hd)
{
    return QStringLiteral("%1.%2.%3")
        .arg(QString::number(hd->gd_day),
             QString::number(hd->gd_mon),
             QString::number(hd->gd_year));
}

// Month names come from our own table (Translator::monthName), keyed by the
// month NUMBER libhdate uses: 1 = Tishrei ... 12 = Elul, 6 = Adar (plain
// year), 13/14 = Adar I/II (leap year). That makes them follow the widget's
// language instead of the process locale. libhdate's own string is only a
// fallback for a number we have no name for (and it may be nullptr, so it is
// guarded before it reaches fromUtf8()).
// The month number itself is deliberately not shown: it is of no use to a reader.
QString hebrewText(const hdate_struct *hd, const QString &lang)
{
    QString month = Translator::monthName(lang, hd->hd_mon);
    if (month.isEmpty()) {
        const char *fallback = hdate_get_hebrew_month_string(hd->hd_mon, 0);
        if (fallback)
            month = QString::fromUtf8(fallback);
    }
    return QStringLiteral("%1.%2.%3")
        .arg(QString::number(hd->hd_day),
             month,
             QString::number(hd->hd_year));
}

// Holiday name from our own table, with libhdate's string as a fallback for a
// code that the table does not know (e.g. after a libhdate upgrade).
QString holidayName(int holyday, const QString &lang)
{
    QString name = Translator::holidayName(lang, holyday);
    if (name.isEmpty()) {
        const char *fallback = hdate_get_holyday_string(holyday, 0);
        if (fallback)
            name = QString::fromUtf8(fallback);
    }
    return name;
}

// ─── Result entries handed to QML ─────────────────────────────────────────────

// A found event. `countdown` is what QML shows big: the number of days, or the
// word "Today" when the event is today (a "0 days" label would read oddly).
QVariantMap makeEntry(const QString &lang,
                      const QString &name,
                      const QString &gregorian,
                      const QString &hebrew,
                      long days)
{
    QVariantMap m;
    m.insert(QStringLiteral("found"), true);
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("gregorian"), gregorian);
    m.insert(QStringLiteral("hebrew"), hebrew);
    m.insert(QStringLiteral("days"), static_cast<int>(days));
    if (days == 0) {
        m.insert(QStringLiteral("countdown"), Translator::lookup(lang, "dayToday"));
        m.insert(QStringLiteral("unit"), QString());
    } else {
        m.insert(QStringLiteral("countdown"), QString::number(days));
        m.insert(QStringLiteral("unit"), Translator::daysUnit(lang, days));
    }
    return m;
}

// "Nothing to show" - QML displays `message` instead of the event layout.
QVariantMap makeMissing(const QString &lang, const char *messageKey)
{
    QVariantMap m;
    m.insert(QStringLiteral("found"), false);
    m.insert(QStringLiteral("message"), Translator::lookup(lang, messageKey));
    return m;
}

// ─── Date parsing ─────────────────────────────────────────────────────────────

// Sets `hd` to the nearest occurrence (today or in the future) of the date
// written in the CSV as "D.M.Y".
void parseNextOccurrence(const std::string &dateStr,
                         hdate_struct *hd,
                         const hdate_struct *today)
{
    int day = 0, month = 0, year = 0;
    // This message only goes to the log (the row is skipped), so it stays
    // in English and is not part of the translation table.
    if (std::sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3)
        throw std::runtime_error("Invalid date format: " + dateStr);

    if (year > kHebrewYearThreshold) {
        // Hebrew anniversary: try this Hebrew year, then the next one.
        hdate_set_hdate(hd, day, month, today->hd_year);
        if (today->hd_jd > hd->hd_jd)
            hdate_set_hdate(hd, day, month, today->hd_year + 1);
    } else {
        // Gregorian anniversary: try this Gregorian year, then the next one.
        // NOTE: the original code moved the *Hebrew* year forward here, which
        // made Gregorian dates drift by up to a month. Staying in the same
        // calendar is what "the next occurrence of this date" really means.
        hdate_set_gdate(hd, day, month, today->gd_year);
        if (today->hd_jd > hd->hd_jd)
            hdate_set_gdate(hd, day, month, today->gd_year + 1);
    }
}

// ─── Nearest memorial date ────────────────────────────────────────────────────

QVariantMap findNearestMemorial(const hdate_struct *today,
                                const QString &path,
                                const QString &lang)
{
    using CsvReader = io::CSVReader<3,
                                    io::trim_chars<' '>,
                                    io::no_quote_escape<','>,
                                    io::throw_on_overflow,
                                    io::single_line_comment<'#'>>;

    CsvReader in(path.toStdString());
    in.read_header(io::ignore_extra_column, "date", "name", "type");

    HDate nearest = makeHDate();
    HDate current = makeHDate();
    std::string dateStr, name, type, nearestName, nearestType;
    bool found = false;

    while (in.read_row(dateStr, name, type)) {
        try {
            parseNextOccurrence(dateStr, current.get(), today);

            // Strict "<" keeps the first row on ties, like the original.
            if (!found || current->hd_jd < nearest->hd_jd) {
                // Copy via the Julian day: it is the single source of truth
                // and avoids re-normalising Hebrew month numbers.
                hdate_set_jd(nearest.get(), current->hd_jd);
                nearestName = name;
                nearestType = type;
                found = true;
            }
        } catch (const std::exception &e) {
            // A bad date in one row must not hide all the other rows.
            qWarning() << "luach line" << in.get_file_line() << ":" << e.what();
        }
    }

    // The original code printed garbage here for an empty file because
    // `nearest` was never initialised; handle it explicitly.
    if (!found)
        return makeMissing(lang, "noMemorials");

    // The CSV "type" column (kind of memorial) goes before the name, as in the
    // original console output.
    const QString title = (QString::fromUtf8(nearestType.c_str())
                           + QLatin1Char(' ')
                           + QString::fromUtf8(nearestName.c_str())).trimmed();

    return makeEntry(lang,
                     title,
                     gregorianText(nearest.get()),
                     hebrewText(nearest.get(), lang),
                     nearest->hd_jd - today->hd_jd);
}

// ─── Nearest holiday ──────────────────────────────────────────────────────────

QVariantMap findNearestHoliday(const hdate_struct *today, const QString &lang)
{
    HDate current = makeHDate();
    hdate_set_jd(current.get(), today->hd_jd);

    for (int i = 0; i < kMaxHolidayLookahead; ++i) {
        // Second argument 0 = Israel schedule (not diaspora), as before.
        const int holyday = hdate_get_holyday(current.get(), 0);
        if (holyday) {
            return makeEntry(lang,
                             holidayName(holyday, lang),
                             gregorianText(current.get()),
                             hebrewText(current.get(), lang),
                             current->hd_jd - today->hd_jd);
        }
        hdate_set_jd(current.get(), current->hd_jd + 1);
    }
    return makeMissing(lang, "noHoliday");
}

// ─── Whole computation ────────────────────────────────────────────────────────

struct Snapshot {
    QString today, error;
    QVariantMap memorial, holiday;
};

// Turns whatever the user typed (or a file dialog returned) into a plain
// local path. Users naturally write "~/luach.txt", and a QML FileDialog hands
// back "file:///home/...". Accepting both here keeps the QML side free of any
// string-munging JavaScript.
QString normalizePath(QString raw)
{
    raw = raw.trimmed();
    if (raw.startsWith(QLatin1String("file:")))
        return QUrl(raw).toLocalFile();
    if (raw == QLatin1String("~"))
        return QDir::homePath();
    if (raw.startsWith(QLatin1String("~/")))
        return QDir::homePath() + raw.mid(1);
    return raw;
}

// Pure function of (file, date, language): takes the date as a parameter so
// the whole snapshot is consistent even if midnight passes while we compute.
Snapshot buildSnapshot(const QString &rawPath, const QDate &date, const QString &lang)
{
    Snapshot s;
    const QString path = normalizePath(rawPath);

    // Fresh widget with no configured file yet: say so instead of trying to
    // open "" (whose error message would be confusing).
    if (path.isEmpty()) {
        s.error = Translator::lookup(lang, "errNoPath");
        return s;
    }

    try {
        HDate today = makeHDate();
        hdate_set_gdate(today.get(), date.day(), date.month(), date.year());

        s.today = Translator::lookup(lang, "today").arg(hebrewText(today.get(), lang));
        s.memorial = findNearestMemorial(today.get(), path, lang);
        s.holiday = findNearestHoliday(today.get(), lang);
    } catch (const io::error::can_not_open_file &) {
        s.error = Translator::lookup(lang, "errOpen").arg(path);
    } catch (const io::error::header_missing &) {
        s.error = Translator::lookup(lang, "errHeader");
    } catch (const std::exception &e) {
        s.error = Translator::lookup(lang, "errGeneric").arg(QString::fromUtf8(e.what()));
    }
    return s;
}

} // namespace

// ─── MemoBridge ───────────────────────────────────────────────────────────────

MemoBridge::MemoBridge(QObject *parent)
    : QObject(parent)
    // No default path on purpose: the single source of truth is the widget's
    // configuration (its default lives in main.xml), which QML binds to
    // `filePath`. A second default here would silently disagree with it.
    // The language default only matters for the first instant before QML
    // applies the configured value.
    , m_language(QStringLiteral("ru"))
{
    m_timer.setInterval(kTimerIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &MemoBridge::refreshIfDateChanged);
    m_timer.start();

    refresh();
}

void MemoBridge::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged();
    refresh(); // new source -> data is stale
}

void MemoBridge::setLanguage(const QString &language)
{
    const QString normalized = Translator::normalizeLanguage(language);
    if (m_language == normalized)
        return;
    m_language = normalized;
    emit languageChanged();
    refresh(); // all generated texts depend on the language
}

void MemoBridge::refreshIfDateChanged()
{
    // Cheap check every 30 minutes; the expensive CSV scan only runs when the
    // calendar day actually changed (also covers resume from suspend).
    if (QDate::currentDate() != m_lastRefreshDate)
        refresh();
}

void MemoBridge::refresh()
{
    const QDate today = QDate::currentDate();
    const Snapshot s = buildSnapshot(m_filePath, today, m_language);

    m_lastRefreshDate = today;
    m_todayText = s.today;
    m_memorial = s.memorial;
    m_holiday = s.holiday;
    m_errorText = s.error;

    // One signal for everything: the parts always change together.
    emit dataChanged();
}
