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

		ScriptOutput {
			id: scriptOutput
		}

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
