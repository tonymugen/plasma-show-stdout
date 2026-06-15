import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.plasma.plasmoid
import com.github.tonymugen.plasmashowstdout 1.0

PlasmoidItem {
	id: root

	Plasmoid.status: PlasmaCore.Types.ActiveStatus

	// Number of configured scripts (entries with a non-empty path). Drives the
	// "add scripts" placeholder: it tracks configuration, not the current output,
	// so a configured-but-idle module (e.g. an untriggered signal) does not fall
	// back to the placeholder.
	property int scriptCount: 0

	// Lives at the root (not inside fullRepresentation) so the configured
	// modules run regardless of whether the widget is expanded.
	ScriptOutput {
		id: scriptOutput
	}

	// Decode the JSON module list from config and push it into the backend. The
	// signal number is computed C++-side from the RTMIN offset, so we pass the
	// offset; entries with an empty script are skipped by setModules.
	function applyConfig() {
		var arr = [];
		try {
			arr = JSON.parse(Plasmoid.configuration.modules || "[]");
		} catch (e) {
			arr = [];
		}
		var specs = [];
		var count = 0;
		for (var i = 0; i < arr.length; ++i) {
			var m = arr[i];
			if (m.script && m.script.length > 0) {
				count += 1; // matches what setModules will actually run
			}
			var spec = {
				kind: m.kind,
				script: m.script,
				interval: m.interval,
				signalOffset: m.signalOffset
			};
			// Older configs predate the per-script output limit; omit the key when
			// absent so setModules falls back to its own default rather than
			// receiving an invalid value (which would truncate to nothing).
			if (m.outputLimit !== undefined) {
				spec.outputLimit = m.outputLimit;
			}
			specs.push(spec);
		}
		scriptOutput.setModules(specs);
		root.scriptCount = count;
	}

	// Push only the delimiter; this re-joins the existing fragments in the backend
	// without restarting any worker, so a delimiter change is cheap.
	function applyDelimiter() {
		scriptOutput.delimiter = Plasmoid.configuration.delimiter;
	}

	Component.onCompleted: {
		applyDelimiter();
		applyConfig();
	}

	Connections {
		target: Plasmoid.configuration
		function onModulesChanged() { root.applyConfig() }
		function onDelimiterChanged() { root.applyDelimiter() }
	}

	// Shown inline in a panel (like the Digital Clock's time). Renders the combined
	// output as a single elided line; clicking opens the full scrollable view for
	// long/multi-line output. Falls back to the terminal icon only when there is
	// nothing to show (configured but no output yet) so the applet never collapses
	// to zero width and stays clickable.
	compactRepresentation: MouseArea {
		id: compactRoot
		readonly property string displayText: root.scriptCount > 0
			? scriptOutput.text : i18n("Add scripts")
		readonly property bool showText: displayText.length > 0

		onClicked: root.expanded = !root.expanded
		// In a panel the applet width comes from these Layout properties, not from
		// implicitWidth; drive them off the text's natural width so the widget grows
		// with the output (icon mode is square: width == height).
		Layout.minimumWidth: showText ? outputLabel.implicitWidth : height
		Layout.preferredWidth: Layout.minimumWidth
		implicitWidth: Layout.minimumWidth
		implicitHeight: outputLabel.implicitHeight

		PlasmaComponents3.Label {
			id: outputLabel
			anchors.fill: parent
			visible: compactRoot.showText
			text: compactRoot.displayText
			opacity: root.scriptCount > 0 ? 1.0 : 0.6
			font.family: Plasmoid.configuration.fontFamily || "monospace"
			// 0 = keep the theme's default point size (prior behaviour).
			font.pointSize: Plasmoid.configuration.fontSize > 0
				? Plasmoid.configuration.fontSize : Kirigami.Theme.defaultFont.pointSize
			font.bold: Plasmoid.configuration.fontBold
			font.italic: Plasmoid.configuration.fontItalic
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignVCenter
			maximumLineCount: 1
			elide: Text.ElideRight
		}
		Kirigami.Icon {
			anchors.fill: parent
			visible: !compactRoot.showText
			source: "utilities-terminal"
		}
	}

	fullRepresentation: Item {
		implicitWidth: 300
		implicitHeight: 200

		Kirigami.Theme.colorSet: Kirigami.Theme.View
		Kirigami.Theme.inherit: false

		Rectangle {
			anchors.fill: parent
			color: Kirigami.Theme.backgroundColor
		}

		PlasmaComponents3.ScrollView {
			id: scrollView
			anchors.fill: parent

			PlasmaComponents3.Label {
				width: scrollView.availableWidth
				// Show the combined script output, or a placeholder prompt when
				// nothing is configured yet (dimmed/italic so it reads as a hint,
				// not as script output).
				text: root.scriptCount > 0 ? scriptOutput.text : i18n("Add scripts")
				color: Kirigami.Theme.textColor
				opacity: root.scriptCount > 0 ? 1.0 : 0.6
				font.family: Plasmoid.configuration.fontFamily || "monospace"
				// 0 = keep the theme's default point size (prior behaviour).
				font.pointSize: Plasmoid.configuration.fontSize > 0
					? Plasmoid.configuration.fontSize : Kirigami.Theme.defaultFont.pointSize
				font.bold: Plasmoid.configuration.fontBold
				// the placeholder prompt is always italic; otherwise honour config
				font.italic: root.scriptCount === 0 || Plasmoid.configuration.fontItalic
				wrapMode: Text.WordWrap
			}
		}
	}
}
