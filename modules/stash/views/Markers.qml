import QtQuick
import Components

// Scene markers: named time indexes inside scenes. Selecting one plays its
// scene from the marker's offset. Rows are grouped by primary tag so a long
// list stays navigable with a remote.
FocusScope {
    id: markersRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var markers: []
    property bool isLoading: true
    property string errorMessage: ""

    Connections {
        target: stashBackend

        function onMarkersLoaded(loaded) {
            markersRoot.isLoading = false
            markersRoot.errorMessage = ""
            markersRoot.markers = loaded
            if (loaded.length > 0) {
                var restore = (markersRoot.navListState.currentIndex !== undefined)
                        ? markersRoot.navListState.currentIndex : 0
                markerList.currentIndex = Math.min(restore, loaded.length - 1)
                markerList.positionViewAtIndex(markerList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            markersRoot.isLoading = false
            markersRoot.errorMessage = msg
            console.log("[Stash Markers] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (!stashBackend.has_config()) {
            isLoading = false
            errorMessage = stashBackend.get_config_error()
            return
        }
        stashBackend.load_markers()
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // Secondary line under the title: tag · scene · time index.
    function metaLine(marker) {
        var parts = []
        if (marker.tagText)    parts.push(marker.tagText)
        if (marker.sceneTitle) parts.push(marker.sceneTitle)
        if (marker.timeText)   parts.push(marker.timeText)
        return parts.join(" · ")
    }

    function selectMarker() {
        var marker = markers[markerList.currentIndex]
        if (!marker) return
        markersRoot.navigateTo("Player.qml", {
            item: {
                streamUrl: marker.streamUrl,
                startSeconds: marker.seconds || 0
            }
        }, { currentIndex: markerList.currentIndex })
    }

    // ---
    // UI
    // ---

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "MARKERS"
    }

    // Loading / error / empty states
    Text {
        visible: markersRoot.isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !markersRoot.isLoading && markersRoot.errorMessage !== ""
        text: markersRoot.errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !markersRoot.isLoading && markersRoot.errorMessage === "" && markersRoot.markers.length === 0
        text: "NO MARKERS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body — same two-line row style as the scene list
    ListView {
        id: markerList
        model: markersRoot.markers
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            markerList.positionViewAtIndex(markerList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            markerList.positionViewAtIndex(markerList.currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: markersRoot.selectMarker()

        delegate: Item {
            width: markerList.width
            height: root.sh * 0.075 //36 — two lines, matching the scene rows

            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: markerList.currentIndex === index
            }

            Column {
                id: textColumn
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0109375 //7
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0109375 //7
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.sh * 0.0041667 //2

                Item {
                    id: titleClip
                    width: Math.min(titleLabel.implicitWidth, textColumn.width)
                    height: titleLabel.implicitHeight
                    clip: true

                    Text {
                        id: titleLabel
                        text: modelData.title || ""
                        color: markerList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0333333 //16
                        x: 0
                    }

                    SequentialAnimation {
                        running: (markerList.currentIndex === index) &&
                                 (titleLabel.implicitWidth > titleClip.width)
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) titleLabel.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: titleLabel; property: "x"
                            to: titleClip.width - titleLabel.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: titleLabel; property: "x"; value: 0 }
                    }
                }

                Text {
                    text: markersRoot.metaLine(modelData)
                    width: textColumn.width
                    elide: Text.ElideRight
                    visible: text !== ""
                    color: markerList.currentIndex === index ? root.surfaceColor : root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0208333 //10
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":PLAY"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
