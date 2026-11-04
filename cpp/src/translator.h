#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/**
 * Translator - the single table of user-visible strings (ru / en).
 *
 * Why our own table instead of KDE's i18n(): i18n() follows the *system*
 * locale, but this widget has its own "language" setting. Keeping the table
 * in C++ also lets MemoBridge (which builds the widget text) and the QML
 * config page share exactly the same strings, with no JavaScript logic.
 *
 * QML usage:   text: tr.strings.configFileLabel
 * The `strings` map has a NOTIFY signal, so every such binding re-evaluates
 * the moment `language` changes (live preview in the config page).
 */
class Translator : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantMap strings READ strings NOTIFY languageChanged)

public:
    explicit Translator(QObject *parent = nullptr);

    QString language() const { return m_language; }
    void setLanguage(const QString &language);

    // All strings of the current language, key -> text, for QML bindings.
    QVariantMap strings() const;

    // Text for `key` in `language`. Unknown language falls back to Russian
    // (the original language of the project); an unknown key returns the key
    // itself so a typo is visible on screen instead of silently empty.
    static QString lookup(const QString &language, const char *key);

    // Maps "en", "en_US", "EN", "" ... onto one of the supported codes.
    static QString normalizeLanguage(const QString &language);

    // Names of Hebrew months and holidays, keyed by the NUMBERS libhdate uses
    // (hdate_struct::hd_mon and the value of hdate_get_holyday()). We keep our
    // own tables because libhdate's strings follow the process locale, not the
    // widget's language setting. Returns an empty string for a number we have
    // no name for, so the caller can fall back to libhdate's own string.
    static QString monthName(const QString &language, int month);
    static QString holidayName(const QString &language, int holyday);

    // The word for the unit after a number of days, with correct plural form
    // ("1 день", "3 дня", "5 дней" / "1 day", "5 days").
    static QString daysUnit(const QString &language, long days);

signals:
    void languageChanged();

private:
    QString m_language;
};

#endif // TRANSLATOR_H
