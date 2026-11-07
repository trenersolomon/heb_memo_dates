#include "translator.h"

#include <QDebug>

#include <cstddef>
#include <cstring>

namespace {

enum class Lang { Ru, En, He };

Lang langOf(const QString &language)
{
    const QString code = Translator::normalizeLanguage(language);
    if (code == QLatin1String("en"))
        return Lang::En;
    if (code == QLatin1String("he"))
        return Lang::He;
    return Lang::Ru;
}

struct Entry {
    const char *key;
    const char *ru;
    const char *en;
    const char *he;
};

struct Names {
    const char *ru;
    const char *en;
    const char *he;
};

// The text of `row` (an Entry or a Names) in language `lang`.
template <class Row>
const char *column(const Row &row, Lang lang)
{
    switch (lang) {
    case Lang::En: return row.en;
    case Lang::He: return row.he;
    case Lang::Ru: break;
    }
    return row.ru;
}

// One row per string. %1, %2, ... are filled in by the caller with QString::arg,
// so the argument ORDER must be identical in all languages.
// Adding a language later = adding one more column (and one case in langOf()).
// Source files are UTF-8, so Hebrew letters are written directly.
const Entry kTable[] = {
    // ── widget text (built by MemoBridge) ────────────────────────────────────
    {"today",
     "Сегодня: %1",
     "Today: %1",
     "היום: %1"},
    {"memorialTitle",
     "Ближайшая памятная дата",
     "Nearest memorial date",
     "תאריך הזיכרון הקרוב"},
    {"holidayTitle",
     "Ближайший праздник",
     "Nearest holiday",
     "החג הקרוב"},
    // Shown instead of "0 days" when the event is today.
    {"dayToday",
     "Сегодня",
     "Today",
     "היום"},
    // %1 = path of the memorial dates file, so a new user knows where to add dates.
    {"noMemorials",
     "Нет памятных дат. Добавьте их в файл:\n%1",
     "No memorial dates yet. Add them to the file:\n%1",
     "אין עדיין תאריכים. הוסיפו אותם לקובץ:\n%1"},
    {"noHoliday",
     "Ближайший праздник не найден",
     "Nearest holiday not found",
     "לא נמצא חג קרוב"},

    // ── errors ───────────────────────────────────────────────────────────────
    {"errOpen",
     "Ошибка: не удалось открыть файл %1",
     "Error: cannot open file %1",
     "שגיאה: לא ניתן לפתוח את הקובץ %1"},
    {"errHeader",
     "Ошибка: в файле отсутствует заголовок",
     "Error: the file has no header line",
     "שגיאה: חסרה שורת כותרת בקובץ"},
    {"errGeneric",
     "Ошибка: %1",
     "Error: %1",
     "שגיאה: %1"},

    // %1 = number of skipped lines, %2 = their line numbers (e.g. "5, 9").
    {"warnSkipped",
     "Пропущено строк с ошибками: %1 (строки: %2). Проверьте файл, результат может быть неполным.",
     "Skipped lines with errors: %1 (lines: %2). Please check the file; the result may be incomplete.",
     "דולגו שורות עם שגיאות: %1 (שורות: %2). בדקו את הקובץ, ייתכן שהתוצאה חלקית."},

    // ── config page ──────────────────────────────────────────────────────────
    {"configFileLabel",
     "Файл с памятными датами:",
     "Memorial dates file:",
     "קובץ תאריכי זיכרון:"},
    {"configLanguageLabel",
     "Язык:",
     "Language:",
     "שפה:"},
    {"fileDialogTitle",
     "Выберите файл с памятными датами",
     "Select the memorial dates file",
     "בחרו קובץ תאריכי זיכרון"},
    {"configFilePlaceholder",
     "По умолчанию: Документы/mem_dates_luach.txt",
     "Default: Documents/mem_dates_luach.txt",
     "ברירת מחדל: מסמכים/mem_dates_luach.txt"},

    // ── explanation put at the top of the file created on first run ─────────
    // Written in ALL languages (see createDefaultFileIfMissing, which adds the
    // header line after them). Comment lines start with '#'.
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
     "# 15.3.1950,John Smith,birthday\n",
     "# קובץ תאריכי זיכרון עבור הווידג'ט Hebrew Memorial Dates.\n"
     "# הוסיפו רשומה אחת בכל שורה, מתחת לשורת הכותרת date,name,type.\n"
     "# שורות שמתחילות ב-# הן הערות.\n"
     "#\n"
     "# date: תאריך בפורמט D.M.Y (יום.חודש.שנה)\n"
     "#   שנה עד 5000: התאריך הלועזי של האירוע (למשל יום הולדת). הוא מומר פעם אחת\n"
     "#   לתאריך עברי, ומאז האירוע חוזר לפי הלוח העברי.\n"
     "#   שנה מעל 5000: תאריך עברי (חודשים: 1 = תשרי ... 12 = אלול,\n"
     "#   6 = אדר, בשנה מעוברת 13 ו-14 = אדר א׳ ואדר ב׳).\n"
     "# name: שם או תיאור האירוע\n"
     "# type: סוג האירוע (למשל: יום הולדת, יארצייט)\n"
     "# שורות עם שגיאות מדולגות, והווידג'ט יציג את מספריהן.\n"
     "#\n"
     "# דוגמה (כדי להפוך אותה לרשומה אמיתית, מחקו את ה-# בתחילת השורה):\n"
     "# 15.3.1950,ישראל ישראלי,יום הולדת\n"},
};

// Index = libhdate month number. libhdate numbers months by NAME, not by
// position: 1 = Tishrei ... 12 = Elul, 6 = Adar (non-leap year), and in a leap
// year 13 = Adar I, 14 = Adar II. Index 0 is unused.
// The English column repeats libhdate's own spelling (checked with
// hdate_get_hebrew_month_string() under LC_ALL=C).
const Names kMonths[] = {
    {nullptr, nullptr, nullptr},
    {"Тишрей", "Tishrei", "תשרי"},
    {"Хешван", "Cheshvan", "חשוון"},
    {"Кислев", "Kislev", "כסלו"},
    {"Тевет", "Tevet", "טבת"},
    {"Шват", "Sh'vat", "שבט"},
    {"Адар", "Adar", "אדר"},
    {"Нисан", "Nisan", "ניסן"},
    {"Ияр", "Iyyar", "אייר"},
    {"Сиван", "Sivan", "סיוון"},
    {"Тамуз", "Tammuz", "תמוז"},
    {"Ав", "Av", "אב"},
    {"Элул", "Elul", "אלול"},
    {"Адар I", "Adar I", "אדר א׳"},
    {"Адар II", "Adar II", "אדר ב׳"},
};

// Index = value returned by hdate_get_holyday() in the installed libhdate
// (codes 1..37 on the author's machine; they differ between libhdate
// versions, so re-check this table after upgrading the library).
// The English column repeats libhdate's own spelling.
const Names kHolidays[] = {
    {nullptr, nullptr, nullptr},
    {"Рош а-Шана, 1-й день", "Rosh Hashana I", "ראש השנה (יום א׳)"},                  // 1
    {"Рош а-Шана, 2-й день", "Rosh Hashana II", "ראש השנה (יום ב׳)"},                 // 2
    {"Пост Гедальи", "Tzom Gedaliah", "צום גדליה"},                                   // 3
    {"Йом-Кипур", "Yom Kippur", "יום כיפור"},                                         // 4
    {"Суккот", "Sukkot", "סוכות"},                                                    // 5
    {"Холь а-моэд Суккот", "Hol hamoed Sukkot", "חול המועד סוכות"},                   // 6
    {"Хошана раба", "Hoshana raba", "הושענא רבה"},                                    // 7
    {"Симхат-Тора", "Simchat Torah", "שמחת תורה"},                                    // 8
    {"Ханука", "Chanukah", "חנוכה"},                                                  // 9
    {"Пост 10 тевета", "Asara B'Tevet", "עשרה בטבת"},                                 // 10
    {"Ту би-Шват", "Tu B'Shvat", "ט״ו בשבט"},                                         // 11
    {"Пост Эстер", "Ta'anit Esther", "תענית אסתר"},                                   // 12
    {"Пурим", "Purim", "פורים"},                                                      // 13
    {"Шушан-Пурим", "Shushan Purim", "שושן פורים"},                                   // 14
    {"Песах", "Pesach", "פסח"},                                                       // 15
    {"Холь а-моэд Песах", "Hol hamoed Pesach", "חול המועד פסח"},                      // 16
    {"Йом а-Ацмаут", "Yom HaAtzma'ut", "יום העצמאות"},                                // 17
    {"Лаг ба-Омер", "Lag B'Omer", "ל״ג בעומר"},                                       // 18
    {"Эрев Шавуот", "Erev Shavuot", "ערב שבועות"},                                    // 19
    {"Шавуот", "Shavuot", "שבועות"},                                                  // 20
    {"Пост 17 тамуза", "Tzom Tammuz", "צום י״ז בתמוז"},                               // 21
    {"Тиша бе-Ав", "Tish'a B'Av", "תשעה באב"},                                        // 22
    {"Ту бе-Ав", "Tu B'Av", "ט״ו באב"},                                               // 23
    {"Йом а-Шоа", "Yom HaShoah", "יום השואה"},                                        // 24
    {"Йом а-Зикарон", "Yom HaZikaron", "יום הזיכרון"},                                // 25
    {"Йом Йерушалаим", "Yom Yerushalayim", "יום ירושלים"},                            // 26
    {"Шмини Ацерет", "Shmini Atzeret", "שמיני עצרת"},                                 // 27
    {"Песах, 7-й день", "Pesach VII", "שביעי של פסח"},                                // 28
    {"Песах, 8-й день", "Pesach VIII", "אחרון של פסח"},                               // 29
    {"Шавуот, 2-й день", "Shavuot II", "שבועות (יום ב׳)"},                            // 30
    {"Суккот, 2-й день", "Sukkot II", "סוכות (יום ב׳)"},                              // 31
    {"Песах, 2-й день", "Pesach II", "פסח (יום ב׳)"},                                 // 32
    {"День семьи", "Family Day", "יום המשפחה"},                                       // 33
    {"День памяти павших, место захоронения которых неизвестно",
     "Memorial day for fallen whose place of burial is unknown",
     "יום זיכרון לחללים שמקום קבורתם לא נודע"},                                       // 34
    {"День памяти Ицхака Рабина", "Yitzhak Rabin memorial day", "יום הזיכרון ליצחק רבין"}, // 35
    {"День Зеева Жаботинского", "Zeev Zhabotinsky day", "יום ז׳בוטינסקי"},            // 36
    {"Эрев Йом-Кипур", "Erev Yom Kippur", "ערב יום כיפור"},                           // 37
};

// Picks the name for `index` or returns an empty string when it is out of range.
template <std::size_t N>
QString pickName(const Names (&table)[N], int index, Lang lang)
{
    if (index < 1 || index >= static_cast<int>(N))
        return QString();
    return QString::fromUtf8(column(table[index], lang));
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
    // "iw" is the old ISO code for Hebrew that some systems still use.
    const QString code = language.trimmed().toLower();
    if (code.startsWith(QLatin1String("en")))
        return QStringLiteral("en");
    if (code.startsWith(QLatin1String("he")) || code.startsWith(QLatin1String("iw")))
        return QStringLiteral("he");
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
    const Lang lang = langOf(language);
    for (const Entry &e : kTable) {
        if (std::strcmp(e.key, key) == 0)
            return QString::fromUtf8(column(e, lang));
    }
    qWarning() << "Translator: unknown string key" << key;
    return QString::fromUtf8(key);
}

QString Translator::monthName(const QString &language, int month)
{
    return pickName(kMonths, month, langOf(language));
}

QString Translator::holidayName(const QString &language, int holyday)
{
    return pickName(kHolidays, holyday, langOf(language));
}

QString Translator::daysUnit(const QString &language, long days)
{
    const long n = days < 0 ? -days : days;

    switch (langOf(language)) {
    case Lang::En:
        return QString::fromUtf8(n == 1 ? "day" : "days");

    case Lang::He:
        // 1 יום, 2-10 ימים, 11 and more again "יום" ("30 יום").
        // The number is shown separately, so the dual form "יומיים" is not
        // used for 2: "2 יומיים" would repeat the number.
        if (n >= 2 && n <= 10)
            return QString::fromUtf8("ימים");
        return QString::fromUtf8("יום");

    case Lang::Ru:
        break;
    }

    // Russian plural rules: 1 день, 2-4 дня, 5-20 дней, 21 день, 22 дня ...
    // The 11-14 exception is why the last two digits are checked separately.
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
    const Lang lang = langOf(m_language);
    QVariantMap map;
    for (const Entry &e : kTable)
        map.insert(QString::fromUtf8(e.key), QString::fromUtf8(column(e, lang)));
    return map;
}
