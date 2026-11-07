#include "memobridge.h"
#include "translator.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
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
// `arg` fills the %1 of the message when it has one (e.g. the file path).
QVariantMap makeMissing(const QString &lang, const char *messageKey,
                        const QString &arg = QString())
{
    QString text = Translator::lookup(lang, messageKey);
    if (!arg.isEmpty())
        text = text.arg(arg);

    QVariantMap m;
    m.insert(QStringLiteral("found"), false);
    m.insert(QStringLiteral("message"), text);
    return m;
}

// A problem with the data file. It is shown in place of the memorial block,
// in the error color, while the rest of the widget keeps working.
QVariantMap makeProblem(const QString &lang, const char *messageKey,
                        const QString &arg = QString())
{
    QVariantMap m = makeMissing(lang, messageKey, arg);
    m.insert(QStringLiteral("isError"), true);
    return m;
}

// "N lines were skipped (lines 5, 9)". At most a few line numbers are listed,
// so the message stays short even for a badly damaged file; the full details
// go to the log. Returns an empty string when nothing was skipped.
QString skippedLinesWarning(const QString &lang, const QList<unsigned> &badLines)
{
    if (badLines.isEmpty())
        return QString();

    constexpr int kMaxListed = 5;
    QStringList listed;
    for (int i = 0; i < badLines.size() && i < kMaxListed; ++i)
        listed << QString::number(badLines[i]);
    QString lines = listed.join(QStringLiteral(", "));
    if (badLines.size() > kMaxListed)
        lines += QStringLiteral(", ...");

    return Translator::lookup(lang, "warnSkipped")
        .arg(QString::number(badLines.size()), lines);
}

// ─── Date parsing ─────────────────────────────────────────────────────────────

// Hebrew leap year (13 months): years 3, 6, 8, 11, 14, 17 and 19 of the
// 19-year cycle.
bool isHebrewLeapYear(int year)
{
    return (7 * year + 1) % 19 < 7;
}

// libhdate numbers months by name: 1 = Tishrei ... 12 = Elul, 6 = Adar in a
// plain year, and 13 / 14 = Adar I / Adar II in a leap year. So the month
// number of a date written in `fromYear` cannot simply be reused in another
// year when Adar is involved. The usual rule, used here:
//   - Adar of a plain year   -> Adar II in a leap year
//   - Adar I / Adar II       -> Adar in a plain year (kept as is in a leap year)
// Every other month keeps its number. Asking libhdate for a month that does
// not exist in the target year is avoided on purpose, because its behaviour
// there is not documented.
int monthInYear(int month, int fromYear, int toYear)
{
    // Defensive: if libhdate ever reports Adar I of a leap year as 6.
    if (month == 6 && isHebrewLeapYear(fromYear))
        month = 13;

    const bool toLeap = isHebrewLeapYear(toYear);
    if (month == 6)
        return toLeap ? 14 : 6;
    if (month == 13 || month == 14)
        return toLeap ? month : 6;
    return month;
}

// Sets `hd` to the nearest occurrence (today or in the future) of the date
// written in the CSV as "D.M.Y". Every entry repeats on its HEBREW date,
// whichever calendar it was written in.
void parseNextOccurrence(const std::string &dateStr,
                         hdate_struct *hd,
                         const hdate_struct *today)
{
    int day = 0, month = 0, year = 0;
    // This message only goes to the log (the row is skipped), so it stays
    // in English and is not part of the translation table.
    if (std::sscanf(dateStr.c_str(), "%d.%d.%d", &day, &month, &year) != 3)
        throw std::runtime_error("Invalid date format: " + dateStr);

    // Reject impossible values (31.2.1990, 32.1.1990, month 15 ...) instead of
    // letting libhdate turn them into some other date without a word.
    const bool valid = (year > kHebrewYearThreshold)
        ? (month >= 1 && month <= 14 && day >= 1 && day <= 30)
        : QDate(year, month, day).isValid();
    if (!valid)
        throw std::runtime_error("Date out of range: " + dateStr);

    // Find the Hebrew date that repeats every year.
    int hDay = day, hMonth = month, hYear = year;
    if (year <= kHebrewYearThreshold) {
        // A Gregorian date in the file is the ORIGINAL date (a birth, a death).
        // The anniversary follows the Hebrew calendar, so convert it once to
        // its Hebrew date and repeat that one. Repeating the Gregorian date
        // would put a birthday on a different Hebrew day every year.
        hdate_set_gdate(hd, day, month, year);
        hDay = hd->hd_day;
        hMonth = hd->hd_mon;
        hYear = hd->hd_year;
    }

    // Try this Hebrew year first, then the next one if the date already passed.
    int targetYear = today->hd_year;
    hdate_set_hdate(hd, hDay, monthInYear(hMonth, hYear, targetYear), targetYear);
    if (today->hd_jd > hd->hd_jd) {
        ++targetYear;
        hdate_set_hdate(hd, hDay, monthInYear(hMonth, hYear, targetYear), targetYear);
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

    // Lines that could not be used. They are skipped, counted and reported to
    // the user, but they never stop the remaining lines from being read.
    QList<unsigned> badLines;

    for (;;) {
        try {
            if (!in.read_row(dateStr, name, type))
                break;
        } catch (const io::error::line_length_limit_exceeded &) {
            // The reader cannot step past such a line, so retrying would loop
            // forever: give up on the whole file.
            throw;
        } catch (const std::exception &e) {
            // A line with too few / too many columns. The reader has already
            // moved on to the next line, so it is safe to carry on.
            badLines.append(in.get_file_line());
            qWarning() << "luach line" << in.get_file_line() << ":" << e.what();
            continue;
        }

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
            badLines.append(in.get_file_line());
            qWarning() << "luach line" << in.get_file_line() << ":" << e.what();
        }
    }

    const QString warning = skippedLinesWarning(lang, badLines);

    // The original code printed garbage here for an empty file because
    // `nearest` was never initialised; handle it explicitly.
    if (!found) {
        QVariantMap missing = makeMissing(lang, "noMemorials", path);
        if (!warning.isEmpty())
            missing.insert(QStringLiteral("warning"), warning);
        return missing;
    }

    // The CSV "type" column (kind of memorial) goes before the name, as in the
    // original console output.
    const QString title = (QString::fromUtf8(nearestType.c_str())
                           + QLatin1Char(' ')
                           + QString::fromUtf8(nearestName.c_str())).trimmed();

    QVariantMap entry = makeEntry(lang,
                                  title,
                                  gregorianText(nearest.get()),
                                  hebrewText(nearest.get(), lang),
                                  nearest->hd_jd - today->hd_jd);
    // If some lines were skipped, the nearest date shown may not be the true
    // nearest one, so the warning travels with the result.
    if (!warning.isEmpty())
        entry.insert(QStringLiteral("warning"), warning);
    return entry;
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

// Where the file lives when the user has not chosen a path: the Documents
// folder (found through QStandardPaths, so it is "Документы" or "Documents"
// or whatever the user's system calls it). It is deliberately a visible,
// ordinary folder: the file is meant to be edited by hand by non-technical
// users. If the system reports no Documents folder, fall back to the home
// directory.
QString defaultFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QDir(dir).filePath(QStringLiteral("mem_dates_luach.txt"));
}

// The one place that decides which file is used: the configured path, or the
// default location when the setting is empty.
QString effectivePath(const QString &configured)
{
    const QString path = normalizePath(configured);
    return path.isEmpty() ? defaultFilePath() : path;
}

// Creates the starter file on first run, so a new user gets a short hint in
// the widget instead of a "cannot open file" error.
// Only ever done for the DEFAULT location: a path the user typed themselves
// is never created behind their back (a typo there should be reported), and
// an existing file is never touched. NewOnly makes "create unless it already
// exists" a single atomic step.
void createDefaultFileIfMissing(const QString &path)
{
    if (path != defaultFilePath() || QFileInfo::exists(path))
        return;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text)) {
        // E.g. a read-only Documents folder: the normal "cannot open file"
        // message will tell the user, so this only goes to the log.
        qWarning() << "Could not create" << path << ":" << file.errorString();
        return;
    }
    // The file belongs to the user, not to the interface language, so it
    // explains the format in English AND Russian. The header line must stay
    // exactly "date,name,type": the reader looks for those column names.
    const QString content =
        Translator::lookup(QStringLiteral("en"), "fileComments")
        + QLatin1String("#\n# ----------------------------------------------------------------\n#\n")
        + Translator::lookup(QStringLiteral("ru"), "fileComments")
        + QLatin1String("date,name,type\n");
    file.write(content.toUtf8());
}

// Pure function of (file, date, language): takes the date as a parameter so
// the whole snapshot is consistent even if midnight passes while we compute.
Snapshot buildSnapshot(const QString &path, const QDate &date, const QString &lang)
{
    Snapshot s;

    try {
        HDate today = makeHDate();
        hdate_set_gdate(today.get(), date.day(), date.month(), date.year());

        // Today's date and the nearest holiday come from the calendar alone,
        // so they are computed first and never depend on the memorial file.
        s.today = Translator::lookup(lang, "today").arg(hebrewText(today.get(), lang));
        s.holiday = findNearestHoliday(today.get(), lang);

        // Only the memorial part needs the file. A problem with it is reported
        // inside the memorial block and must not hide the rest of the widget.
        try {
            s.memorial = findNearestMemorial(today.get(), path, lang);
        } catch (const io::error::can_not_open_file &) {
            s.memorial = makeProblem(lang, "errOpen", path);
        } catch (const io::error::header_missing &) {
            s.memorial = makeProblem(lang, "errHeader");
        } catch (const std::exception &e) {
            s.memorial = makeProblem(lang, "errGeneric", QString::fromUtf8(e.what()));
        }
    } catch (const std::exception &e) {
        // Failure of the calendar part itself (e.g. out of memory).
        s.error = Translator::lookup(lang, "errGeneric").arg(QString::fromUtf8(e.what()));
    }
    return s;
}

} // namespace

// ─── MemoBridge ───────────────────────────────────────────────────────────────

MemoBridge::MemoBridge(QObject *parent)
    : QObject(parent)
    // filePath stays empty here and QML binds it to the widget setting. An
    // empty path means "use the default file" (<Documents>/mem_dates_luach.txt),
    // which is worked out in one place only: effectivePath().
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

    // An empty setting means "use the default file". Make sure that file
    // exists, so a first-time user sees the widget's hint rather than an error.
    const QString path = effectivePath(m_filePath);
    createDefaultFileIfMissing(path);

    const Snapshot s = buildSnapshot(path, today, m_language);

    m_lastRefreshDate = today;
    m_todayText = s.today;
    m_memorial = s.memorial;
    m_holiday = s.holiday;
    m_errorText = s.error;

    // One signal for everything: the parts always change together.
    emit dataChanged();
}
