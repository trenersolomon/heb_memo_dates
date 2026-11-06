# Hebrew Memorial Dates

A KDE Plasma 6 widget that shows the **nearest memorial date** from your own
list and the **nearest Jewish holiday**, with a countdown in days and both the
Gregorian and the Hebrew date.

The date logic is written in C++ (libhdate) and exposed to QML as a small
plugin, so the QML side contains no calculation code.

## Features

- Nearest memorial date from a plain CSV file (Gregorian or Hebrew dates)
- Nearest Jewish holiday (Israel schedule)
- Today's Hebrew date
- Refreshes when the calendar day changes (checked every 30 minutes)
- Interface language: Russian or English (widget text, month and holiday names,
  and the settings page)
- Follows the Plasma theme; the background can be set to default, translucent
  or off from the widget's context menu

## Dependencies

Tested on Arch / CachyOS with KDE Plasma 6.

- Qt 6 (Core, Qml), CMake, a C++20 compiler
- [libhdate](https://github.com/lieff/libhdate) with its development headers
  (the package name differs between distributions)
- `plasma-sdk` for `plasmoidviewer` (only needed for development)

## Build and run

```bash
make cpp-build      # builds the QML plugin into package/contents/ui/cppbridge
make watch          # runs the widget in plasmoidviewer
```

To install it as a real widget:

```bash
kpackagetool6 -t Plasma/Applet -i package    # use -u instead of -i to upgrade
```

Then add "Hebrew Memorial Dates" from the widget list. Settings made in
`plasmoidviewer` are not kept between runs; install the widget to test
persistence.

## Configuration

Right-click the widget and open its settings:

- **Memorial dates file**: path to your CSV file (`~` and `file://` paths are
  accepted)
- **Language**: Russian or English

## Data file format

A CSV file with a `date,name,type` header. See
[`example/luach.example.txt`](example/luach.example.txt).

```csv
date,name,type
15.3.1950,Sample Person,Birthday
10.8.5750,Third Sample,Yahrzeit
```

- `date` is `D.M.Y`. A year up to 5000 is a Gregorian date, a year above 5000
  a Hebrew date. Either way the entry repeats every year on its **Hebrew**
  date: a Gregorian date is the original date (for example a birth) and is
  converted to the Hebrew date once.
- Hebrew month numbers follow libhdate: 1 = Tishrei ... 12 = Elul, 6 = Adar
  (plain year), 13 / 14 = Adar I / Adar II (leap year).
- Lines starting with `#` are comments.
- Your real data file is personal: keep it outside the repository.

## Known limitations

- Holiday names are a table keyed by libhdate's holiday numbers (1-37). They
  may change between libhdate versions, so re-check the table in
  `cpp/src/translator.cpp` after upgrading the library.
- Adar: an entry in the Adar of a plain year falls on Adar II in a leap year;
  Adar I and Adar II fall on Adar in a plain year. Other customs (for example
  Sephardic) are not supported.
- A Gregorian date is converted as written, so an event after nightfall, which
  belongs to the next Hebrew day, should be entered as a Hebrew date.
- The 30th of Cheshvan, Kislev or Adar I in a year where that day does not
  exist is handled however libhdate handles it; this was not checked.
- Hebrew as an interface language is not supported yet.

## Credits

- Widget template: [ruinivist/kde-6-widget-starter](https://github.com/ruinivist/kde-6-widget-starter)
- CSV reading: [fast-cpp-csv-parser](https://github.com/ben-strasser/fast-cpp-csv-parser)
  by Ben Strasser (BSD-3-Clause), included as `cpp/src/csv.h`
- Hebrew calendar: libhdate

## License

See the `LICENSE` file.
