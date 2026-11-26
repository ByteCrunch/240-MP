import QtQuick

// Hands the scene's Stash stream URL to mpv. mpv covers the screen and owns
// playback; this view only forwards remote/keyboard input to mpv's OSC (the
// same relay the other modules' Player views do) and returns to the list once
// mpv exits. No autoplay and no progress reporting — this module is read-only.
FocusScope {
    id: playerRoot

    property var navParams: ({})
    property var item: navParams.item || ({})
    property string streamUrl: item.streamUrl || ""
    property string errorMessage: ""

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (playerRoot.errorMessage !== "") {
            // Preflight failed: mpv never started, so the keys are ours again.
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                playerRoot.checkStream()   // retry
                event.accepted = true
            }
            return
        }

        // Playing: relay to mpv, exactly like the other modules' Player views.
        // On RPi/EGLFS the Qt window keeps OS focus during playback, so this is
        // the path that drives the OSC; on macOS InputManager does it instead.
        if (event.key === Qt.Key_Escape) {
            mpvController.sendKey("ESC")
            event.accepted = true
        } else if (event.key === Qt.Key_Backspace) {
            mpvController.sendKey("BS")
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            mpvController.sendKey("UP")
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            mpvController.sendKey("DOWN")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            mpvController.sendKey("LEFT")
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            mpvController.sendKey("SPACE")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
            event.accepted = true
        }
    }

    // Starting mpv runs synchronously and, on the Pi, switches VT before Qt's
    // render thread can present this view. Defer one tick so the loading frame
    // is actually on screen before the display is handed over.
    //
    // subTrack -2 = --sid=no: this module has no subtitle picker, so it matches
    // the other modules' "Off" default rather than auto-selecting embedded subs.
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
        // cannot hand over a playable stream. Deliberately just "LOADING..."
        // like the other modules' players.
        Text {
            anchors.centerIn: parent
            visible: playerRoot.errorMessage === ""
            text: "LOADING..."
            color: "white"
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.05 //24
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
