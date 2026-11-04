#ifndef MEMOBRIDGE_H
#define MEMOBRIDGE_H

#include <QDate>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/**
 * MemoBridge - exposes the "nearest memorial date / nearest holiday" logic
 * (formerly a console program) to QML as a regular QObject.
 *
 * Why a QObject instead of a free function: QML needs properties with change
 * notifications so that Labels re-render automatically when the data is
 * recalculated, and we do not want any JavaScript logic in the widget.
 */
class MemoBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Path to the CSV file with memorial dates. Writable so QML (or the config
    // page) can override it without recompiling.
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)

    // Interface language of the generated texts: "ru" or "en".
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    // "Сегодня: 10.Хешван.5787" - one line, ready to show.
    Q_PROPERTY(QString todayText READ todayText NOTIFY dataChanged)

    // The two events are exposed as maps so QML can lay out each part
    // (countdown, name, dates) and style it separately, without parsing text.
    // Keys:
    //   found      bool    - false when there is nothing to show
    //   message    string  - explanation when found == false
    //   name       string  - event name (for memorials: "<type> <name>")
    //   gregorian  string  - e.g. "12.11.2026"
    //   hebrew     string  - e.g. "3.Кислев.5787"
    //   days       int     - days until the event (0 = today)
    //   countdown  string  - the big text: "12", or "Сегодня" when days == 0
    //   unit       string  - "дней"/"дня"/"день" (empty when days == 0)
    // A QVariantMap is used instead of a dozen separate properties because the
    // two events always have the same shape and always change together.
    Q_PROPERTY(QVariantMap memorial READ memorial NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap holiday  READ holiday  NOTIFY dataChanged)

    // Non-empty when something went wrong (missing file, bad header, ...).
    // We report errors through a property because an exception must never
    // propagate into the Qt event loop / QML engine.
    Q_PROPERTY(QString errorText READ errorText NOTIFY dataChanged)

public:
    explicit MemoBridge(QObject *parent = nullptr);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);

    QString language() const { return m_language; }
    void setLanguage(const QString &language);

    QString todayText() const { return m_todayText; }
    QVariantMap memorial() const { return m_memorial; }
    QVariantMap holiday() const { return m_holiday; }
    QString errorText() const { return m_errorText; }

    // Forces an immediate recalculation (e.g. from a "Refresh" button).
    Q_INVOKABLE void refresh();

signals:
    void filePathChanged();
    void languageChanged();
    void dataChanged();

private slots:
    // Called by the timer; recalculates only if the calendar day has changed.
    void refreshIfDateChanged();

private:
    QString m_filePath;
    QString m_language;
    QString m_todayText;
    QVariantMap m_memorial;
    QVariantMap m_holiday;
    QString m_errorText;

    // The day for which the cached texts were computed. Comparing against it
    // lets a cheap periodic timer detect the midnight rollover.
    QDate  m_lastRefreshDate;
    QTimer m_timer;
};

#endif // MEMOBRIDGE_H
