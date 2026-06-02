import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import com.github.tonymugen.plasmashowstdout 1.0

KCM.SimpleKCM {
    id: configPage

    // Plasma binds these cfg_<key> aliases to the entries in config/main.xml,
    // handling load, save and defaults automatically.
    property alias cfg_scriptPath: scriptField.text
    property alias cfg_triggerKind: kindCombo.currentIndex
    property alias cfg_interval: intervalSpin.value
    property alias cfg_signalOffset: signalSpin.value

    // A module-less probe, purely to read maxSignalOffset (SIGRTMAX - SIGRTMIN)
    // for the signal spinbox range. It spawns no threads and touches no global
    // signal state, so it is safe alongside the running widget instance.
    ScriptOutput {
        id: signalInfo
    }

    Kirigami.FormLayout {
        anchors.fill: parent

        RowLayout {
            Kirigami.FormData.label: i18n("Script:")
            QQC2.TextField {
                id: scriptField
                Layout.fillWidth: true
                placeholderText: i18n("Absolute path to a script")
            }
            QQC2.Button {
                text: i18n("Browse…")
                icon.name: "document-open"
                onClicked: scriptDialog.open()
            }
        }

        QQC2.ComboBox {
            id: kindCombo
            Kirigami.FormData.label: i18n("Trigger:")
            // index 0 = Timed, 1 = Signal — matches PSSspace::ModuleSpec::Kind
            model: [ i18n("Timed (poll on an interval)"), i18n("Signal (run on SIGRTMIN+N)") ]
        }

        QQC2.SpinBox {
            id: intervalSpin
            Kirigami.FormData.label: i18n("Interval (seconds):")
            visible: kindCombo.currentIndex === 0
            from: 1
            to: 86400
            editable: true
        }

        QQC2.SpinBox {
            id: signalSpin
            Kirigami.FormData.label: i18n("Signal offset (RTMIN+N):")
            visible: kindCombo.currentIndex === 1
            from: 0
            to: signalInfo.maxSignalOffset
            editable: true
        }

        QQC2.Label {
            visible: kindCombo.currentIndex === 1
            text: i18n("Trigger with: pkill --signal RTMIN+%1 plasmashell", signalSpin.value)
            opacity: 0.7
            font: Kirigami.Theme.smallFont
        }
    }

    Dialogs.FileDialog {
        id: scriptDialog
        title: i18n("Select a script")
        // a local filesystem path is what the backend runs
        onAccepted: scriptField.text = String(selectedFile).replace(/^file:\/\//, "")
    }
}
