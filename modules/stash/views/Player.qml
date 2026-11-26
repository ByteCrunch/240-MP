import QtQuick

// Hands the scene's Stash stream URL to mpv. The Player is deliberately thin:
// mpv covers the screen, and the one playbackEnded handler always returns to
// the list (no autoplay, no progress reporting — this module is read-only).
FocusScope {
    id: playerRoot

    property var navParams: ({})
    property var item: navParams.item || ({})
    property string streamUrl: item.streamUrl || ""
    property string streamTitle: item.title || ""
    property string errorMessage: ""

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && playerRoot.errorMessage !== "") {
            // Retry after a failed preflight.
            playerRoot.checkStream()
            event.accepted = true
        }
    }

    // Starting mpv runs synchronously and, on the Pi, switches VT before Qt's
    // render thread can present this view. Defer one tick so the loading frame
    // is actually on screen before the display is handed over.
    //
    // subTrack -2 = --sid=no: this module has no subtitle UI, so it matches the
    // other modules' "Off" default rather than auto-selecting embedded subs.
    Timer {
        id: startTimer
        interval: 50
        repeat: false
        onTriggered: mpvController.loadAndPlay(playerRoot.streamUrl, 0, 0, -2)
    }

    // mpv can only report "unrecognized file format" when a reverse proxy
    // answers a stream request with a login page, so ask the backend to check
    // first and show something actionable instead of a black screen.
    function checkStream() {
        if (playerRoot.streamUrl === "") {
            playerRoot.errorMessage = "NO PLAYABLE STREAM"
            return
        }
        playerRoot.errorMessage = ""
        stashBackend.check_stream(playerRoot.streamUrl)
    }

    Component.onCompleted: playerRoot.checkStream()

    Connections {
        target: stashBackend

        function onStreamCheckFinished(ok, error) {
            if (!ok) {
                playerRoot.errorMessage = error
                console.log("[Stash Player] stream check failed: " + error.replace(/\n/g, " | "))
                return
            }
            startTimer.start()
        }
    }

    Connections {
        target: mpvController

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // "eof", "stopped" and "failed" all return to the scene list. Never
            // leave a defunct Player focused over an exited mpv subprocess.
            playerRoot.goBack()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while the stream is checked and mpv launches/buffers, before
        // mpv's window takes over. Replaced by the error state when the server
        // cannot hand over a playable stream.
        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.0333333 //16
            visible: playerRoot.errorMessage === ""

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.sw * 0.76875 //492
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: playerRoot.streamTitle
                color: "white"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
                visible: playerRoot.streamTitle !== ""
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "LOADING..."
                color: "white"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.05 //36
            visible: playerRoot.errorMessage !== ""

            Text {
                id: errorText
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.sw * 0.76875 //492
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: playerRoot.errorMessage
                color: "white"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.hints.select + ":RETRY  " + root.hints.back + ":BACK"
                color: "#a0a0a0"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
            }
        }
    }
}
