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

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        // Only reachable before mpv starts, or when a scene has no stream at
        // all (the Scenes view already filters those out, so this is a guard).
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (playerRoot.streamUrl === "") {
                goBack()
                event.accepted = true
            }
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

    Component.onCompleted: {
        if (playerRoot.streamUrl !== "")
            startTimer.start()
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

        // Shown while mpv launches and buffers the stream (before its window
        // takes over).
        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.0333333 //16

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
                text: playerRoot.streamUrl === "" ? "NO PLAYABLE STREAM" : "LOADING..."
                color: "white"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
            }
        }
    }
}
