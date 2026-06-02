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

	// Lives at the root (not inside fullRepresentation) so the configured
	// modules run regardless of whether the widget is expanded.
	ScriptOutput {
		id: scriptOutput
	}

	// Push the (single, for now) configured module into the backend. The signal
	// number is computed C++-side from the RTMIN offset, so we pass the offset.
	// An empty path means "not configured yet" -> no modules.
	function applyConfig() {
		if (Plasmoid.configuration.scriptPath.length === 0) {
			scriptOutput.setModules([]);
			return;
		}
		scriptOutput.setModules([{
			kind: Plasmoid.configuration.triggerKind,
			script: Plasmoid.configuration.scriptPath,
			interval: Plasmoid.configuration.interval,
			signalOffset: Plasmoid.configuration.signalOffset
		}]);
	}

	Component.onCompleted: applyConfig()

	Connections {
		target: Plasmoid.configuration
		function onScriptPathChanged() { root.applyConfig() }
		function onTriggerKindChanged() { root.applyConfig() }
		function onIntervalChanged() { root.applyConfig() }
		function onSignalOffsetChanged() { root.applyConfig() }
	}

	compactRepresentation: MouseArea {
		onClicked: root.expanded = !root.expanded
		Kirigami.Icon {
			anchors.fill: parent
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
				text: scriptOutput.text
				color: Kirigami.Theme.textColor
				font.family: "monospace"
				wrapMode: Text.WordWrap
			}
		}
	}
}
