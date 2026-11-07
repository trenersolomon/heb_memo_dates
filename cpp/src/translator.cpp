#include "translator.h"

#include <QDebug>

#include <cstddef>
#include <cstring>

namespace {

struct Entry {
    const char *key;
    const char *ru;
    const char *en;
};

// One row per string. %1, %2, ... are filled in by the caller with QString::arg,
// so the argument ORDER must be identical in ru and en.
// Adding a language later = adding one more column (and one case in lookup()).
const Entry kTable[] = {
    // ── widget text (built by MemoBridge) ────────────────────────────────────
    {"today",
     "Сегодня: %1",
     "Today: %1"},
    {"memorialTitle",
     "Ближайшая памятная дата",
     "Nearest memorial date"},
    {"holidayTitle",
     "Ближайший праздник",
     "Nearest holiday"},
    // Shown instead of "0 days" when the event is today.
    {"dayToday",
     "Сегодня",
     "Today"},
    // %1 = path of the memorial dates file, so a new user knows where to add dates.
    {"noMemorials",
     "Нет памятных дат. Добавьте их в файл:\n%1",
     "No memorial dates yet. Add them to the file:\n%1"},
    {"noHoliday",
     "Ближайший праздник не найден",
     "Nearest holiday not found"},

    // ── errors ───────────────────────────────────────────────────────────────
    {"errOpen",
     "Ошибка: не удалось открыть файл %1",
     "Error: cannot open file %1"},
    {"errHeader",
     "Ошибка: в файле отсутствует заголовок",
     "Error: the file has no header line"},
    {"errGeneric",
     "Ошибка: %1",
     "Error: %1"},

    // %1 = number of skipped lines, %2 = their line numbers (e.g. "5, 9").
    {"warnSkipped",
     "Пропущено строк с ошибками: %1 (строки: %2). Проверьте файл, результат может быть неполным.",
     "Skipped lines with errors: %1 (lines: %2). Please check the file; the result may be incomplete."},

    // ── config page ──────────────────────────────────────────────────────────
    {"configFileLabel",
     "Файл с памятными датами:",
     "Memorial dates file:"},
    {"configLanguageLabel",
     "Язык:",
     "Language:"},
    {"fileDialogTitle",
     "Выберите файл с памятными датами",
     "Select the memorial dates file"},
    {"configFilePlaceholder",
     "По умолчанию: Документы/mem_dates_luach.txt",
     "Default: Documents/mem_dates_luach.txt"},

    // ── contents of the file created on first run ───────────────────────────
    // Explanation put at the top of the file in BOTH languages (see
    // createDefaultFileIfMissing, which adds the header line after them).
    // Comment lines start with '#'.
    {"fileComments",
     "# Файл памятных дат для виджета Hebrew Memorial Dates.\n"
     "# Добавляйте по одной записи на строку под строкой заголовка date,name,type.\n"
     "# Строки, начинающиеся с #, являются комментариями.\n"
     "#\n"
     "# date: дата в формате Д.М.Г\n"
     "#   Год до 5000: григорианская дата события (например, рождения). Она один раз\n"
     "#   переводится в еврейскую, и дальше событие повторяется по еврейскому календарю.\n"
     "#   Год больше 5000: еврейская дата (месяцы: 1 = Тишрей ... 12 = Элул,\n"
     "#   6 = Адар, в високосный год 13 и 14 = Адар I и Адар II).\n"
     "# name: имя или название события\n"
     "# type: вид события (например: день рождения, йорцайт)\n"
     "# Строки с ошибками пропускаются, виджет покажет их номера.\n"
     "#\n"
     "# Пример (чтобы записать его в файл, удалите # в начале строки):\n"
     "# 15.3.1950,Иван Иванов,день рождения\n",
     "# Memorial dates file for the Hebrew Memorial Dates widget.\n"
     "# Add one entry per line below the header line date,name,type.\n"
     "# Lines starting with # are comments.\n"
     "#\n"
     "# date: a date written as D.M.Y\n"
     "#   Year up to 5000: the Gregorian date of the event (for example a birth). It is\n"
     "#   converted to the Hebrew date once, and the event then repeats by the Hebrew calendar.\n"
     "#   Year above 5000: a Hebrew date (months: 1 = Tishrei ... 12 = Elul,\n"
     "#   6 = Adar, in a leap year 13 and 14 = Adar I and Adar II).\n"
     "# name: a name or title of the event\n"
     "# type: the kind of event (for example: birthday, yahrzeit)\n"
     "# Lines with errors are skipped; the widget shows their numbers.\n"
     "#\n"
     "# Example (remove the # at the start of the line to make it a real entry):\n"
     "# 15.3.1950,John Smith,birthday\n"},
};

struct Names {
    const char *ru;
    const char *en;
};

// Index = libhdate month number. libhdate numbers months by NAME, not by
// position: 1 = Tishrei ... 12 = Elul, 6 = Adar (non-leap year), and in a leap
// year 13 = Adar I, 14 = Adar II. Index 0 is unused.
// The English column repeats libhdate's own spelling (checked with
// hdate_get_hebrew_month_string() under LC_ALL=C).
const Names kMonths[] = {
    {nullptr, nullptr},
    {"Тишрей", "Tishrei"},
    {"Хешван", "Cheshvan"},
    {"Кислев", "Kislev"},
    {"Тевет", "Tevet"},
    {"Шват", "Sh'vat"},
    {"Адар", "Adar"},
    {"Нисан", "Nisan"},
    {"Ияр", "Iyyar"},
    {"Сиван", "Sivan"},
    {"Тамуз", "Tammuz"},
    {"Ав", "Av"},
    {"Элул", "Elul"},
    {"Адар I", "Adar I"},
    {"Адар II", "Adar II"},
};

// Index = value returned by hdate_get_holyday() in the installed libhdate
// (codes 1..37 on the author's machine; they differ between libhdate
// versions, so re-check this table after upgrading the library).
// The English column repeats libhdate's own spelling.
const Names kHolidays[] = {
    {nullptr, nullptr},
    {"Рош а-Шана, 1-й день", "Rosh Hashana I"},                 // 1
    {"Рош а-Шана, 2-й день", "Rosh Hashana II"},                // 2
    {"Пост Гедальи", "Tzom Gedaliah"},                          // 3
    {"Йом-Кипур", "Yom Kippur"},                                // 4
    {"Суккот", "Sukkot"},                                       // 5
    {"Холь а-моэд Суккот", "Hol hamoed Sukkot"},                // 6
    {"Хошана раба", "Hoshana raba"},                            // 7
    {"Симхат-Тора", "Simchat Torah"},                           // 8
    {"Ханука", "Chanukah"},                                     // 9
    {"Пост 10 тевета", "Asara B'Tevet"},                        // 10
    {"Ту би-Шват", "Tu B'Shvat"},                               // 11
    {"Пост Эстер", "Ta'anit Esther"},                           // 12
    {"Пурим", "Purim"},                                         // 13
    {"Шушан-Пурим", "Shushan Purim"},                           // 14
    {"Песах", "Pesach"},                                        // 15
    {"Холь а-моэд Песах", "Hol hamoed Pesach"},                 // 16
    {"Йом а-Ацмаут", "Yom HaAtzma'ut"},                         // 17
    {"Лаг ба-Омер", "Lag B'Omer"},                              // 18
    {"Эрев Шавуот", "Erev Shavuot"},                            // 19
    {"Шавуот", "Shavuot"},                                      // 20
    {"Пост 17 тамуза", "Tzom Tammuz"},                          // 21
    {"Тиша бе-Ав", "Tish'a B'Av"},                              // 22
    {"Ту бе-Ав", "Tu B'Av"},                                    // 23
    {"Йом а-Шоа", "Yom HaShoah"},                               // 24
    {"Йом а-Зикарон", "Yom HaZikaron"},                         // 25
    {"Йом Йерушалаим", "Yom Yerushalayim"},                     // 26
    {"Шмини Ацерет", "Shmini Atzeret"},                         // 27
    {"Песах, 7-й день", "Pesach VII"},                          // 28
    {"Песах, 8-й день", "Pesach VIII"},                         // 29
    {"Шавуот, 2-й день", "Shavuot II"},                         // 30
    {"Суккот, 2-й день", "Sukkot II"},                          // 31
    {"Песах, 2-й день", "Pesach II"},                           // 32
    {"День семьи", "Family Day"},                               // 33
    {"День памяти павших, место захоронения которых неизвестно",
     "Memorial day for fallen whose place of burial is unknown"}, // 34
    {"День памяти Ицхака Рабина", "Yitzhak Rabin memorial day"}, // 35
    {"День Зеева Жаботинского", "Zeev Zhabotinsky day"},        // 36
    {"Эрев Йом-Кипур", "Erev Yom Kippur"},                      // 37
};

// Picks the name for `index` or returns an empty string when it is out of range.
template <std::size_t N>
QString pickName(const Names (&table)[N], int index, bool english)
{
    if (index < 1 || index >= static_cast<int>(N))
        return QString();
    return QString::fromUtf8(english ? table[index].en : table[index].ru);
}

} // namespace

Translator::Translator(QObject *parent)
    : QObject(parent)
    , m_language(QStringLiteral("ru"))
{
}

QString Translator::normalizeLanguage(const QString &language)
{
    // Tolerate "en_US", "EN", " en " etc. so a hand-edited config still works.
    if (language.trimmed().toLower().startsWith(QLatin1String("en")))
        return QStringLiteral("en");
    return QStringLiteral("ru");
}

void Translator::setLanguage(const QString &language)
{
    const QString normalized = normalizeLanguage(language);
    if (m_language == normalized)
        return;
    m_language = normalized;
    emit languageChanged();
}

QString Translator::lookup(const QString &language, const char *key)
{
    const bool english = normalizeLanguage(language) == QLatin1String("en");
    for (const Entry &e : kTable) {
        if (std::strcmp(e.key, key) == 0)
            return QString::fromUtf8(english ? e.en : e.ru);
    }
    qWarning() << "Translator: unknown string key" << key;
    return QString::fromUtf8(key);
}

QString Translator::monthName(const QString &language, int month)
{
    return pickName(kMonths, month, normalizeLanguage(language) == QLatin1String("en"));
}

QString Translator::holidayName(const QString &language, int holyday)
{
    return pickName(kHolidays, holyday, normalizeLanguage(language) == QLatin1String("en"));
}

QString Translator::daysUnit(const QString &language, long days)
{
    if (normalizeLanguage(language) == QLatin1String("en"))
        return QString::fromUtf8(days == 1 ? "day" : "days");

    // Russian plural rules: 1 день, 2-4 дня, 5-20 дней, 21 день, 22 дня ...
    // The 11-14 exception is why the last two digits are checked separately.
    const long n = days < 0 ? -days : days;
    const long mod10 = n % 10;
    const long mod100 = n % 100;
    if (mod10 == 1 && mod100 != 11)
        return QString::fromUtf8("день");
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
        return QString::fromUtf8("дня");
    return QString::fromUtf8("дней");
}

QVariantMap Translator::strings() const
{
    const bool english = m_language == QLatin1String("en");
    QVariantMap map;
    for (const Entry &e : kTable)
        map.insert(QString::fromUtf8(e.key), QString::fromUtf8(english ? e.en : e.ru));
    return map;
}
