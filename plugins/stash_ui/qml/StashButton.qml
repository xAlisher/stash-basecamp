// StashButton.qml — drop into your plugin's QML directory alongside Stash.png
//
// Usage:
//   StashButton { moduleName: "notes" }
//
// Your C++ plugin must expose (Q_INVOKABLE):
//   QString getFileForStash()                          → {"ok":true,"path":"..."}
//   QString setBackupCid(QString cid, QString timestamp) → {"ok":true}
//
// See docs/stash-button-integration.md for the full integration guide.

import QtQuick
import QtQuick.Layouts

Item {
    id: root

    // ── Required ────────────────────────────────────────────────────────────
    property string moduleName: ""   // your module's IPC name, e.g. "notes"

    // ── Optional ────────────────────────────────────────────────────────────
    property string iconSource:   "Stash.png"
    property int    iconSize:     16
    property int    fontSize:     13
    property color  colorText:    "#FFFFFF"
    property color  colorMuted:   "#5D5D5D"
    property color  colorHover:   "#333333"
    property color  colorBusy:    "#1A1A1A"

    // ── Read-only state ──────────────────────────────────────────────────────
    property bool   busy:      false
    property string lastError: ""

    implicitWidth:  72
    implicitHeight: 36

    // ── Internal ─────────────────────────────────────────────────────────────
    function _parse(raw) {
        try {
            var tmp = JSON.parse(raw)
            return (typeof tmp === "string") ? JSON.parse(tmp) : tmp
        } catch(e) { return null }
    }

    function trigger() {
        if (root.busy || root.moduleName === "") return
        if (typeof logos === "undefined" || !logos.callModule) return

        root.busy = true
        root.lastError = ""

        // 1 — export file from the host module
        var fileRes = _parse(logos.callModule(root.moduleName, "getFileForStash", []))
        if (!fileRes || !fileRes.ok || !fileRes.path) {
            root.lastError = fileRes ? (fileRes.error || "getFileForStash failed") : "IPC error"
            root.busy = false
            return
        }

        // 2 — choose transport
        var atRes     = _parse(logos.callModule("stash", "getActiveTransport", []))
        var transport = atRes && atRes.transport ? atRes.transport : "kubo"

        if (transport === "logos") {
            // Async — stash calls setBackupCid internally when the upload settles
            var qRes = _parse(logos.callModule("stash", "upload", [fileRes.path]))
            if (!qRes || qRes.error)
                root.lastError = qRes ? (qRes.error || "upload failed") : "upload IPC error"
            root.busy = false
            return
        }

        // 3 — kubo / pinata: synchronous, CID returned immediately
        var upRes = _parse(logos.callModule("stash", "uploadViaIpfs", [fileRes.path]))
        if (!upRes || !upRes.cid) {
            root.lastError = upRes ? (upRes.error || "upload failed") : "upload IPC error"
            root.busy = false
            return
        }

        // 4 — record the CID on the host module
        logos.callModule(root.moduleName, "setBackupCid",
                         [upRes.cid, String(Math.floor(Date.now() / 1000))])
        root.busy = false
    }

    // ── Visual ───────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        radius: 4
        color: btnArea.containsMouse ? root.colorHover
             : root.busy             ? root.colorBusy
             : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }

        Row {
            anchors.centerIn: parent
            spacing: 6

            Image {
                source: root.iconSource
                width:  root.iconSize; height: root.iconSize
                sourceSize: Qt.size(root.iconSize, root.iconSize)
                fillMode: Image.PreserveAspectFit
                anchors.verticalCenter: parent.verticalCenter
                opacity: root.busy ? 0.4 : 1.0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            Text {
                text:          root.busy ? "…" : "Stash"
                font.pixelSize: root.fontSize
                color:          root.busy ? root.colorMuted : root.colorText
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        MouseArea {
            id: btnArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.busy ? Qt.WaitCursor : Qt.PointingHandCursor
            enabled: !root.busy
            onClicked: root.trigger()
        }
    }
}
