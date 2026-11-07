import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import "cppbridge" as CppBridge
import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: page

    // The alias name must be "cfg_" + the entry name from main.xml.
    // Every alias must point to an existing id, otherwise the whole page fails to load.
    property alias cfg_filePath: filePathField.text
    property string cfg_language

    // Plasma also sets "<name>Default" properties; declaring them avoids
    // "Setting initial properties failed" warnings in the log.
    property string cfg_filePathDefault
    property string cfg_languageDefault

    // Order must match the ComboBox model below.
    readonly property var languageCodes: ["ru", "en"]

    // Texts of this page come from the same C++ table as the widget itself.
    // Bound to cfg_language, so labels switch immediately when the user picks
    // another language, even before pressing "Apply".
    CppBridge.Translator {
        id: tr
        language: page.cfg_language
    }

    Kirigami.FormLayout {
        QQC2.ComboBox {
            Kirigami.FormData.label: tr.strings.configLanguageLabel

            // Language names are shown in their own language on purpose,
            // so a user who ended up in the wrong language can still find it.
            model: ["Русский", "English"]
            currentIndex: Math.max(0, page.languageCodes.indexOf(page.cfg_language))
            onActivated: page.cfg_language = page.languageCodes[currentIndex]
        }

        RowLayout {
            Kirigami.FormData.label: tr.strings.configFileLabel

            QQC2.TextField {
                id: filePathField
                Layout.fillWidth: true
                // Empty field = the default file in the Documents folder.
                placeholderText: tr.strings.configFilePlaceholder
            }

            QQC2.Button {
                icon.name: "document-open"
                onClicked: fileDialog.open()
            }
        }
    }

    // Non-visual dialog: it does not belong inside the form layout.
    FileDialog {
        id: fileDialog
        title: tr.strings.fileDialogTitle
        onAccepted: filePathField.text = selectedFile
    }
}
