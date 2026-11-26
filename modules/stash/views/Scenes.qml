import QtQuick
import Components

// Scenes for a saved filter or a tag. Rows show the scene title plus its tags;
// selecting a row hands the scene's stream URL to the Player (mpv).
FocusScope {
    id: scenesRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string listTitle: navParams.title || ""
    property string tagId: navParams.tagId || ""
    property var findFilter: navParams.findFilter || ({})
    property var objectFilter: navParams.objectFilter || ({})

    property var scenes: []
    property bool isLoading: true
    property string errorMessage: ""

    Connections {
        target: stashBackend

        function onScenesLoaded(loaded) {
            scenesRoot.isLoading = false
            scenesRoot.errorMessage = ""
            scenesRoot.scenes = loaded
            if (loaded.length > 0) {
                var restore = (scenesRoot.navListState.currentIndex !== undefined)
                        ? scenesRoot.navListState.currentIndex : 0
                sceneList.currentIndex = Math.min(restore, loaded.length - 1)
                sceneList.positionViewAtIndex(sceneList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            scenesRoot.isLoading = false
            scenesRoot.errorMessage = msg
            console.log("[Stash Scenes] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (!stashBackend.has_config()) {
            isLoading = false
            errorMessage = stashBackend.get_config_error()
            return
        }
        if (tagId !== "")
            stashBackend.load_scenes_for_tag(tagId)
        else
            stashBackend.load_scenes(findFilter, objectFilter)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    function selectScene() {
        var scene = scenes[sceneList.currentIndex]
        if (!scene) return
        scenesRoot.navigateTo("Player.qml", { item: scene }, { currentIndex: sceneList.currentIndex })
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
        subtitle: scenesRoot.listTitle
    }

    // Loading / error / empty states
    Text {
        visible: scenesRoot.isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !scenesRoot.isLoading && scenesRoot.errorMessage !== ""
        text: scenesRoot.errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !scenesRoot.isLoading && scenesRoot.errorMessage === "" && scenesRoot.scenes.length === 0
        text: "NO SCENES FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Secondary line under the title: studio · resolution · tags, only the parts
    // the scene actually has. Built here so the delegate stays declarative.
    function metaLine(item) {
        var parts = []
        if (item.studio)     parts.push(item.studio)
        if (item.resolution) parts.push(item.resolution)
        if (item.tagsText)   parts.push(item.tagsText)
        return parts.join(" · ")
    }

    // Body — title first, then the studio/resolution/tags line in the smaller
    // two-line style the YouTube module uses for its video lists.
    ListView {
        id: sceneList
        model: scenesRoot.scenes
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
            sceneList.positionViewAtIndex(sceneList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            sceneList.positionViewAtIndex(sceneList.currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: scenesRoot.selectScene()

        delegate: Item {
            width: sceneList.width
            height: root.sh * 0.075 //36 — two lines, matching the YouTube list rows

            // Full-width background highlight for the active row
            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: sceneList.currentIndex === index
            }

            Column {
                id: textColumn
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0109375 //7
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0109375 //7
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.sh * 0.0041667 //2

                // Clipped title with marquee scroll when it overflows the row
                Item {
                    id: titleClip
                    width: Math.min(titleLabel.implicitWidth, textColumn.width)
                    height: titleLabel.implicitHeight
                    clip: true

                    Text {
                        id: titleLabel
                        text: modelData.title || ""
                        color: sceneList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0333333 //16
                        x: 0
                    }

                    SequentialAnimation {
                        running: (sceneList.currentIndex === index) &&
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
                    id: metaLabel
                    // Elide rather than marquee: the metadata is secondary, so a
                    // long tag list shouldn't scroll the row.
                    text: scenesRoot.metaLine(modelData)
                    width: textColumn.width
                    elide: Text.ElideRight
                    visible: text !== ""
                    color: sceneList.currentIndex === index ? root.surfaceColor : root.secondaryColor
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
