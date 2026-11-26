import QtQuick
import Components

// Stash home screen: SCENES (saved filters from Stash) and TAGS (all tags).
// Connection details live in stash.json; when that file is missing or invalid
// this view shows where to put it instead of an empty menu.
FocusScope {
    id: browseRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // Static menu — kept as plain strings to match the YouTube module's
    // simple-menu pattern.
    property var items: ["Scenes", "Tags", "Markers"]

    property bool configured: true
    property string configError: ""
    property string configHelp: ""

    Component.onCompleted: {
        browseRoot.configured = stashBackend.has_config()
        browseRoot.configError = stashBackend.get_config_error()
        if (!browseRoot.configured) {
            var msg = browseRoot.configError !== "" ? browseRoot.configError + "\n\n" : ""
            browseRoot.configHelp = msg + "ADD SERVERURL AND APIKEY TO:\n"
                    + stashBackend.get_config_path() + "\n\n"
                    + "SEE MODULES/STASH/README.MD FOR DETAILS"
            return
        }
        var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
        browseList.currentIndex = Math.min(restore, browseRoot.items.length - 1)
        browseList.positionViewAtIndex(browseList.currentIndex, ListView.Contain)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
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
    }

    // Not-configured / invalid-config guidance
    Text {
        visible: !browseRoot.configured
        text: browseRoot.configHelp
        color: root.secondaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492 — long paths wrap instead of clipping offscreen
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: browseList
        model: browseRoot.items
        visible: browseRoot.configured
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
            browseList.positionViewAtIndex(browseList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            browseList.positionViewAtIndex(browseList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var selected = browseRoot.items[browseList.currentIndex]
            if (!selected) return
            var state = { currentIndex: browseList.currentIndex }
            if (selected === "Scenes")
                browseRoot.navigateTo("Filters.qml", {}, state)
            else if (selected === "Tags")
                browseRoot.navigateTo("Tags.qml", {}, state)
            else if (selected === "Markers")
                browseRoot.navigateTo("Markers.qml", {}, state)
        }

        delegate: Item {
            width: browseList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, browseList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: browseList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData
                    color: browseList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                    running: (browseList.currentIndex === index) &&
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
        text: browseRoot.configured
              ? root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
              : root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
