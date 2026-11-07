import QtQuick
import QtQuick.Layouts
import "cppbridge" as CppBridge
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    // qmllint disable missing-property
    property string memoFile: plasmoid.configuration.filePath
    property string language: plasmoid.configuration.language
    // Falls back to 15 % if main.xml has no such entry (for example an old
    // main.xml next to a new main.qml), so the widget still loads and draws.
    property real backgroundTransparency: plasmoid.configuration.backgroundTransparency !== undefined
                                          ? plasmoid.configuration.backgroundTransparency : 15

    // Widget Setup
    width: Kirigami.Units.gridUnit * 16
    height: Kirigami.Units.gridUnit * 17
    toolTipMainText: "Hebrew Memory dates"
    toolTipSubText: memo.todayText

    // The standard Plasma background can only be switched on or off, so the
    // widget draws its own background (see the Rectangle in fullRepresentation)
    // with a transparency the user can set in the widget settings. Its color
    // comes from the theme, so it still fits light and dark color schemes.
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    CppBridge.MemoBridge {
        id: memo
        filePath: root.memoFile
        language: root.language
    }

    // Section titles come from the same C++ string table as everything else.
    CppBridge.Translator {
        id: tr
        language: root.language
    }

    // Define the full representation (popup or desktop widget)
    fullRepresentation: Item {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 12
        // Height follows the content, so there is no unused space at the
        // bottom whatever the font size or language is.
        Layout.minimumHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2
        Layout.preferredWidth: root.width
        Layout.preferredHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2

        // The watermark below is larger than the corner it sits in on purpose,
        // so whatever sticks out must be cut off at the widget edge.
        clip: true

        // Hebrew reads right to left: when it is the chosen language, mirror
        // the whole layout (text alignment, the number/unit row, the corner of
        // the watermark). Nothing changes for the other languages.
        LayoutMirroring.enabled: tr.rtl
        LayoutMirroring.childrenInherit: true

        // Own background with adjustable transparency (0 % = solid, 100 % = none).
        // Drawn first, so everything else lies on top of it. Shown only for a
        // widget on the desktop: inside a panel's popup, Plasma already draws
        // the popup frame and a second background would double it.
        Rectangle {
            anchors.fill: parent
            visible: Plasmoid.formFactor === PlasmaCore.Types.Planar
            radius: Kirigami.Units.gridUnit * 0.6
            color: Kirigami.Theme.backgroundColor
            opacity: 1 - root.backgroundTransparency / 100
        }

        // Subtle symbol in the bottom-right corner. It is drawn first, so it
        // lies under the text. isMask keeps only the shape and repaints it in
        // the theme's text color, which works on light and dark themes alike.
        Kirigami.Icon {
            source: Qt.resolvedUrl("../images/star.svg")
            isMask: true
            color: Kirigami.Theme.textColor
            opacity: 0.09
            width: Kirigami.Units.gridUnit * 8
            height: width
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            // Slightly cut off on the right, but raised off the bottom edge so
            // the whole lower point of the star stays visible.
            anchors.rightMargin: -Kirigami.Units.gridUnit / 2
            anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
        }

        // One block = small title, big countdown, event name, both dates.
        // Used twice (memorial date, holiday) so they look identical.
        // All colors come from the Kirigami theme, so it fits any color scheme.
        component EventBlock: ColumnLayout {
            property string title
            property var entry

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents.Label {
                text: title
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.capitalization: Font.AllUppercase
                opacity: 0.7
            }

            RowLayout {
                visible: entry.found === true
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents.Label {
                    text: entry.countdown
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 2.4
                    font.bold: true
                    color: Kirigami.Theme.highlightColor
                }

                PlasmaComponents.Label {
                    text: entry.unit
                    Layout.alignment: Qt.AlignBottom
                    opacity: 0.8
                }
            }

            PlasmaComponents.Label {
                visible: entry.found === true
                text: entry.name
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            PlasmaComponents.Label {
                visible: entry.found === true
                text: entry.gregorian + "   |   " + entry.hebrew
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // "Nothing found" message (empty file, no holiday in range).
            PlasmaComponents.Label {
                visible: entry.found !== true && !!entry.message
                text: entry.message ? entry.message : ""
                // Problems (e.g. file not found) in the error color, hints in a muted one.
                color: entry.isError === true ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                opacity: entry.isError === true ? 1 : 0.7
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // Some lines of the data file were skipped, so the result above
            // may be incomplete. Stays visible until the file is fixed.
            PlasmaComponents.Label {
                visible: !!entry.warning
                text: entry.warning ? entry.warning : ""
                color: Kirigami.Theme.neutralTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            PlasmaComponents.Label {
                text: memo.todayText
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            EventBlock {
                title: tr.strings.memorialTitle
                entry: memo.memorial
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            EventBlock {
                title: tr.strings.holidayTitle
                entry: memo.holiday
            }

            // Shown only when the C++ side reports a problem (missing file, bad header...)
            PlasmaComponents.Label {
                visible: memo.errorText !== ""
                text: memo.errorText
                color: Kirigami.Theme.negativeTextColor
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

        }
    }

    // Compact representation (what is shown in a panel): the symbol plus the
    // number of days to the nearest memorial date, so it can be read without
    // opening the widget.
    compactRepresentation: MouseArea {
        Layout.minimumWidth: compactRow.implicitWidth
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        onClicked: root.expanded = !root.expanded

        RowLayout {
            id: compactRow
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.alignment: Qt.AlignVCenter
                source: Qt.resolvedUrl("../images/star.svg")
                isMask: true
                color: Kirigami.Theme.textColor
            }

            // Hidden when there is no memorial date to count down to.
            PlasmaComponents.Label {
                visible: memo.memorial.found === true
                text: memo.memorial.days
                font.bold: true
                color: Kirigami.Theme.highlightColor
            }
        }
    }
}
