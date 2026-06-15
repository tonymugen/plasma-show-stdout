import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import com.github.tonymugen.plasmashowstdout 1.0

KCM.SimpleKCM {
    id: configPage

    // The whole module list lives in one JSON-string config key (a variable-length
    // list does not fit fixed cfg_<key> scalars). Plasma auto-loads/saves this
    // property; we keep an in-memory ListModel in sync with it and re-encode on
    // every edit — that single string change is what marks the page dirty.
    property string cfg_modules

    // Single separator between adjacent script outputs. A plain scalar, so the
    // standard cfg_<key> alias to the field handles load/save automatically.
    property alias cfg_delimiter: delimiterField.text

    // Font family for the displayed output (empty/"monospace" = generic fixed-width).
    property alias cfg_fontFamily: fontField.text

    // Point size (0 = theme default) and bold/italic style of the displayed output.
    // Plain scalars, so the standard cfg_<key> alias to each control persists them.
    property alias cfg_fontSize: fontSizeSpin.value
    property alias cfg_fontBold: boldCheck.checked
    property alias cfg_fontItalic: italicCheck.checked

    // A module-less probe, used to read maxSignalOffset (SIGRTMAX - SIGRTMIN) for
    // the signal spinboxes' range and hostPid for the trigger hint. It spawns no
    // threads and touches no global signal state, so it is safe alongside the
    // running widget instance.
    ScriptOutput {
        id: pluginInfo
    }

    // The editable, ordered list of modules. Roles match the JSON object keys.
    ListModel {
        id: modulesModel
    }

    // Encode modulesModel back into the persisted JSON string.
    function serialize() {
        var arr = [];
        for (var i = 0; i < modulesModel.count; ++i) {
            var m = modulesModel.get(i);
            arr.push({
                kind: m.kind,
                script: m.script,
                interval: m.interval,
                signalOffset: m.signalOffset,
                outputLimit: m.outputLimit
            });
        }
        cfg_modules = JSON.stringify(arr);
    }

    // Decode the persisted JSON string into modulesModel (on first load).
    function load() {
        modulesModel.clear();
        var arr = [];
        try {
            arr = JSON.parse(cfg_modules || "[]");
        } catch (e) {
            arr = [];
        }
        for (var i = 0; i < arr.length; ++i) {
            var m = arr[i];
            modulesModel.append({
                kind: m.kind !== undefined ? m.kind : 0,
                script: m.script !== undefined ? m.script : "",
                interval: m.interval !== undefined ? m.interval : 5,
                signalOffset: m.signalOffset !== undefined ? m.signalOffset : 0,
                // 300 mirrors ModuleSpec's C++ default for modules saved before
                // the output-limit field existed.
                outputLimit: m.outputLimit !== undefined ? m.outputLimit : 300
            });
        }
    }

    function addModule() {
        // Append a blank timed module; the user fills in the path next.
        modulesModel.append({ kind: 0, script: "", interval: 5, signalOffset: 0, outputLimit: 300 });
        serialize();
    }

    // Removing is deferred via Qt.callLater by the caller: a Repeater destroys the
    // delegate that hosts the Remove button as soon as the row is removed, which
    // would abort the rest of an inline click handler (so serialize() — and thus
    // the dirty/Apply state — would never run). Running it from configPage after
    // the click handler returns avoids destroying an object mid-handler.
    function removeModule(i) {
        modulesModel.remove(i);
        serialize();
    }

    Component.onCompleted: load()

    ColumnLayout {
        width: configPage.width
        spacing: Kirigami.Units.largeSpacing

        // One delimiter for the whole combined output (the backend joins with a
        // single separator). Surrounding spaces are significant, so no trimming.
        RowLayout {
            Layout.fillWidth: true
            QQC2.Label { text: i18n("Delimiter between outputs:") }
            QQC2.TextField {
                id: delimiterField
                Layout.fillWidth: true
                placeholderText: i18n("e.g. \" | \" (spaces count)")
            }
        }

        // Display font for the output. Type a family name or pick one; a sample
        // below previews glyph coverage so unsupported characters are obvious.
        RowLayout {
            Layout.fillWidth: true
            QQC2.Label { text: i18n("Display font:") }
            QQC2.TextField {
                id: fontField
                Layout.fillWidth: true
                placeholderText: i18n("e.g. MesloLGS Nerd Font Mono (blank = monospace)")
            }
            QQC2.Button {
                text: i18n("Choose…")
                icon.name: "preferences-desktop-font"
                // Seed the picker from the current family/size/style so it opens on
                // the active font, then apply every chosen attribute on accept.
                onClicked: {
                    fontDialog.selectedFont = Qt.font({
                        family: fontField.text.length > 0 ? fontField.text : "monospace",
                        pointSize: fontSizeSpin.value > 0 ? fontSizeSpin.value
                                                          : Kirigami.Theme.defaultFont.pointSize,
                        bold: boldCheck.checked,
                        italic: italicCheck.checked
                    });
                    fontDialog.open();
                }
            }
        }

        // Size (point) and style (bold/italic) of the display font. The font picker
        // above writes these too; the inline controls let the user tweak them
        // directly. 0 size keeps the theme's default point size.
        RowLayout {
            Layout.fillWidth: true
            QQC2.Label { text: i18n("Font size (pt, 0 = default):") }
            QQC2.SpinBox {
                id: fontSizeSpin
                from: 0
                to: 144
                editable: true
            }
            QQC2.CheckBox {
                id: boldCheck
                text: i18n("Bold")
            }
            QQC2.CheckBox {
                id: italicCheck
                text: i18n("Italic")
            }
            Item { Layout.fillWidth: true }
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            // sample mixing ASCII, accents, symbols and box drawing so missing
            // glyphs (shown as tofu/boxes) are immediately visible
            text: i18n("Preview:") + "  AaBb 0123  é ñ ✓ ★ ⚙ ░▒▓ │┤"
            font.family: fontField.text.length > 0 ? fontField.text : "monospace"
            font.pointSize: fontSizeSpin.value > 0 ? fontSizeSpin.value
                                                   : Kirigami.Theme.defaultFont.pointSize
            font.bold: boldCheck.checked
            font.italic: italicCheck.checked
            opacity: 0.8
            elide: Text.ElideRight
        }

        Repeater {
            model: modulesModel

            delegate: Kirigami.AbstractCard {
                id: card
                Layout.fillWidth: true

                // Required properties matching the ListModel roles — injected by the
                // Repeater, and unambiguous inside the nested control scopes (unlike
                // bare role names, which can clash with e.g. ComboBox.model).
                required property int index
                required property int kind
                required property string script
                required property int interval
                required property int signalOffset
                required property int outputLimit

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label {
                            text: i18n("Script %1", card.index + 1)
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        QQC2.Button {
                            text: i18n("Remove")
                            icon.name: "list-remove"
                            // defer: removing destroys this delegate (see removeModule)
                            onClicked: {
                                var i = card.index;
                                Qt.callLater(function() { configPage.removeModule(i); });
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.TextField {
                            id: scriptField
                            Layout.fillWidth: true
                            placeholderText: i18n("Absolute path to a script")
                            text: card.script
                            onEditingFinished: {
                                modulesModel.setProperty(card.index, "script", text);
                                configPage.serialize();
                            }
                        }
                        QQC2.Button {
                            text: i18n("Browse…")
                            icon.name: "document-open"
                            onClicked: {
                                scriptDialog.targetIndex = card.index;
                                scriptDialog.open();
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label { text: i18n("Trigger:") }
                        QQC2.ComboBox {
                            id: kindCombo
                            Layout.fillWidth: true
                            // index 0 = Timed, 1 = Signal — matches PSSspace::ModuleSpec::Kind
                            model: [ i18n("Timed (poll on an interval)"),
                                     i18n("Signal (run on SIGRTMIN+N)") ]
                            currentIndex: card.kind
                            onActivated: {
                                modulesModel.setProperty(card.index, "kind", currentIndex);
                                configPage.serialize();
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: kindCombo.currentIndex === 0
                        QQC2.Label { text: i18n("Interval (seconds):") }
                        QQC2.SpinBox {
                            from: 1
                            to: 86400
                            editable: true
                            value: card.interval
                            onValueModified: {
                                modulesModel.setProperty(card.index, "interval", value);
                                configPage.serialize();
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: kindCombo.currentIndex === 1
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            QQC2.Label { text: i18n("Signal offset (RTMIN+N):") }
                            QQC2.SpinBox {
                                from: 0
                                to: pluginInfo.maxSignalOffset
                                editable: true
                                value: card.signalOffset
                                onValueModified: {
                                    modulesModel.setProperty(card.index, "signalOffset", value);
                                    configPage.serialize();
                                }
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            // pkill (a real binary) accepts an RTMIN+N signal spec;
                            // the `kill` shell builtin (e.g. zsh's) does not. hostName
                            // is the actual host (plasmashell / plasmoidviewer /
                            // plasmawindowed), so this targets the right process.
                            text: i18n("Trigger with: pkill --signal RTMIN+%1 %2", card.signalOffset, pluginInfo.hostName)
                            opacity: 0.7
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Applies to both trigger kinds: the output is truncated to this
                    // many characters before being displayed.
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label { text: i18n("Output limit (characters):") }
                        QQC2.SpinBox {
                            from: 1
                            to: 1000000
                            editable: true
                            value: card.outputLimit
                            onValueModified: {
                                modulesModel.setProperty(card.index, "outputLimit", value);
                                configPage.serialize();
                            }
                        }
                    }
                }
            }
        }

        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Add script")
            icon.name: "list-add"
            onClicked: configPage.addModule()
        }
    }

    Dialogs.FileDialog {
        id: scriptDialog
        title: i18n("Select a script")
        // which module row the Browse… button was clicked for
        property int targetIndex: -1
        // a local filesystem path is what the backend runs
        onAccepted: {
            if (targetIndex < 0) {
                return;
            }
            var path = String(selectedFile).replace(/^file:\/\//, "");
            modulesModel.setProperty(targetIndex, "script", path);
            configPage.serialize();
        }
    }

    Dialogs.FontDialog {
        id: fontDialog
        title: i18n("Select a display font")
        // Apply every attribute the user chose. Writing through the aliased controls
        // marks the page dirty (so Apply enables) and updates the live preview.
        onAccepted: {
            fontField.text = selectedFont.family;
            if (selectedFont.pointSize > 0) {
                fontSizeSpin.value = Math.round(selectedFont.pointSize);
            }
            boldCheck.checked = selectedFont.bold;
            italicCheck.checked = selectedFont.italic;
        }
    }
}
