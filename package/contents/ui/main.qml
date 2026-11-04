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

    // Widget Setup
    width: Kirigami.Units.gridUnit * 20
    height: Kirigami.Units.gridUnit * 20
    toolTipMainText: "Hebrew Memory dates"
    toolTipSubText: memo.todayText

    // Use the standard Plasma background instead of drawing our own: it follows
    // the user's theme (light/dark, blur, rounded corners) and, with
    // ConfigurableBackground, the user can switch it to translucent or off
    // from the widget's right-click menu.
    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground | PlasmaCore.Types.ConfigurableBackground

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
        Layout.minimumHeight: Kirigami.Units.gridUnit * 14
        Layout.preferredWidth: root.width
        Layout.preferredHeight: root.height

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
                opacity: 0.7
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
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

            Item {
                Layout.fillHeight: true
            }
        }
    }

    // Define the compact representation (icon in panel)
    compactRepresentation: Item {
        Kirigami.Icon {
            anchors.fill: parent
            source: "plasma"

            MouseArea {
                anchors.fill: parent
                onClicked: root.expanded = !root.expanded
            }
        }
    }
}
