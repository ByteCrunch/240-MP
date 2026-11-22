import QtQuick
import Components

// All Stash tags, name-sorted by the server. Selecting one browses every scene
// that carries that tag.
FocusScope {
    id: tagsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var tags: []
    property bool isLoading: true
    property string errorMessage: ""

    Connections {
        target: stashBackend

        function onTagsLoaded(loaded) {
            tagsRoot.isLoading = false
            tagsRoot.errorMessage = ""
            tagsRoot.tags = loaded
            if (loaded.length > 0) {
                var restore = (tagsRoot.navListState.currentIndex !== undefined)
                        ? tagsRoot.navListState.currentIndex : 0
                tagList.currentIndex = Math.min(restore, loaded.length - 1)
                tagList.positionViewAtIndex(tagList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            tagsRoot.isLoading = false
            tagsRoot.errorMessage = msg
            console.log("[Stash Tags] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (!stashBackend.has_config()) {
            isLoading = false
            errorMessage = stashBackend.get_config_error()
            return
        }
        stashBackend.load_tags()
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    function selectTag() {
        var tag = tags[tagList.currentIndex]
        if (!tag) return
        tagsRoot.navigateTo("Scenes.qml", {
            title: tag.title,
            tagId: tag.id
        }, { currentIndex: tagList.currentIndex })
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
        subtitle: "TAGS"
    }

    // Loading / error / empty states
    Text {
        visible: tagsRoot.isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !tagsRoot.isLoading && tagsRoot.errorMessage !== ""
        text: tagsRoot.errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !tagsRoot.isLoading && tagsRoot.errorMessage === "" && tagsRoot.tags.length === 0
        text: "NO TAGS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body — text list, matching the other modules' browse lists
    ListView {
        id: tagList
        model: tagsRoot.tags
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
            tagList.positionViewAtIndex(tagList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            tagList.positionViewAtIndex(tagList.currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: tagsRoot.selectTag()

        delegate: Item {
            width: tagList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, tagList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: tagList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: tagList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                SequentialAnimation {
                    running: (tagList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
