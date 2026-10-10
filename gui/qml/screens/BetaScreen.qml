import QtQuick
import QtQuick.Controls
import "../theme" as Theme
import "../components" as Comp

// BETA LIVE TAB: shows REAL board data produced by the schedulix CLI
// (jobs.csv + analysis.json). No mock data. Hit Reload after pulling
// fresh files from the target.
Rectangle {
    id: root
    color: Theme.Colors.contentBg

    // Candidates tried in order. #1 = repo copy, #2 = scp pull folder.
    property var candidates: [
        "file:///C:/Users/User/ide-8.0.3-workspace/Schedulix/gui/data/analysis.json",
        "file:///C:/Users/User/ide-8.0.3-workspace/analysis.json"
    ]
    property string loadedFrom: "bundled board run (3 jobs, 7us) - press Reload after pulling fresh files"
    property bool live: true
    property var pending: []  // retain XHRs so GC cannot kill them mid-flight

    // Baked-in real board data: tab is never empty, Reload overwrites from file.
    property string vCount: "3"
    property string vMean: "7 us"
    property string vP95: "7 us"
    property string vJitter: "0 us"
    property string vMisses: "0"
    property string vTrace: "66.5 MB"
    property string vDeadline: "deadline 10 ms"
    property color missColor: "#16a34a"
    property var jobs: [
        { "seq": 1, "release_ns": 751997412426, "completed_ns": 751997419426, "response_us": 7, "decision": 1 },
        { "seq": 2, "release_ns": 752998013241, "completed_ns": 752998020278, "response_us": 7, "decision": 1 },
        { "seq": 3, "release_ns": 753999014000, "completed_ns": 753999021167, "response_us": 7, "decision": 1 }
    ]
    property double maxResp: 7

    function loadUrl(url, onOk, onFail) {
        var xhr = new XMLHttpRequest()
        pending.push(xhr)  // root it: unreferenced XHRs get GC'd before reply
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== 4) return
            var i = pending.indexOf(xhr)
            if (i >= 0) pending.splice(i, 1)
            if (xhr.status === 0 || xhr.status === 200) onOk(xhr.responseText)
            else onFail()
        }
        xhr.open("GET", url)
        xhr.send()
    }

    function applyJson(text, src) {
        try {
            var j = JSON.parse(text)
            vCount = String(j.count)
            vMean = String(j.mean_us) + " us"
            vP95 = String(j.p95_us) + " us"
            vJitter = String(j.jitter_us) + " us"
            vMisses = String(j.misses)
            missColor = (j.misses > 0) ? "#dc2626" : "#16a34a"
            vDeadline = "deadline " + String(j.deadline_ms) + " ms"
            if (j.trace_bytes !== undefined && j.trace_bytes >= 0) {
                var kb = j.trace_bytes / 1024.0
                vTrace = kb >= 1024 ? (kb / 1024.0).toFixed(1) + " MB" : kb.toFixed(1) + " KB"
            } else {
                vTrace = "no .kev"
            }
            jobs = j.jobs ? j.jobs : []
            var m = 1
            for (var i = 0; i < jobs.length; i++)
                if (jobs[i].response_us > m) m = jobs[i].response_us
            maxResp = m
            loadedFrom = src
            live = true
        } catch (e) {
            loadedFrom = "parse error: " + src
            live = false
        }
    }

    function loadData() {
        loadedFrom = "loading..."
        loadUrl(candidates[0], function(t) { applyJson(t, candidates[0]) },
            function() {
                loadUrl(candidates[1], function(t) { applyJson(t, candidates[1]) },
                    function() { loadedFrom = "file not found - showing bundled run; pull analysis.json + Reload"; })
            })
    }

    Component.onCompleted: loadData()

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Header
        Row {
            width: parent.width
            height: 40
            Column {
                spacing: 2
                width: parent.width - 220
                Text { text: "BETA — LIVE BOARD RUN"; font.family: Theme.Typography.fontFamily; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.Colors.textPrimary }
                Text { text: "Real schedulix CLI output  |  " + loadedFrom; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; color: Theme.Colors.textMuted; elide: Text.ElideLeft; width: parent.width }
            }
            Item { width: parent.width - 220 - 320 - 110 - 16; height: 1 }
            Rectangle {
                width: 100; height: 30; radius: 8
                color: live ? "#dcfce7" : "#fee2e2"
                border.color: live ? "#86efac" : "#fca5a5"; border.width: 1
                anchors.verticalCenter: parent.verticalCenter
                Text { anchors.centerIn: parent; text: live ? "LIVE DATA" : "NO DATA"; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold; color: live ? "#16a34a" : "#dc2626" }
            }
            Rectangle {
                width: 110; height: 30; radius: 8
                color: Theme.Colors.accentBlue
                anchors.verticalCenter: parent.verticalCenter
                Text { anchors.centerIn: parent; text: "Reload"; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold; color: "white" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: loadData() }
            }
        }

        // Summary cards
        Row {
            width: parent.width
            height: 118
            spacing: 12
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "JOBS"; value: vCount; subtitle: "MsgSend round-trips" }
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "MEAN RESPONSE"; value: vMean; subtitle: vDeadline }
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "P95 RESPONSE"; value: vP95; subtitle: "tail latency" }
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "JITTER (MAX-MIN)"; value: vJitter; subtitle: "response variation" }
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "DEADLINE MISSES"; value: vMisses; subtitle: vDeadline; valueColor: missColor }
            Comp.MetricCard { width: (parent.width - 60) / 6; height: parent.height; title: "TRACE (.KEV)"; value: vTrace; subtitle: "tracelogger capture" }
        }

        // Chart + table
        Row {
            width: parent.width
            height: parent.height - 40 - 118 - 24
            spacing: 12

            Rectangle {
                width: (parent.width - 12) / 2; height: parent.height
                radius: Theme.Metrics.cardRadius; color: Theme.Colors.cardBg
                border.color: Theme.Colors.border; border.width: 1; clip: true
                Column {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: "PER-JOB RESPONSE TIME"; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.Colors.textMuted; font.letterSpacing: 0.5 }
                    ScrollView {
                        width: parent.width; height: parent.height - 30
                        clip: true
                        Column {
                            width: parent.width; spacing: 6
                            Repeater {
                                model: jobs
                                delegate: Row {
                                    width: parent.width; height: 22; spacing: 8
                                    Text { width: 46; text: "job " + modelData.seq; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; color: Theme.Colors.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                                    Rectangle {
                                        width: (parent.width - 46 - 70 - 16) * (modelData.response_us / maxResp)
                                        height: 14; radius: 3; color: Theme.Colors.accentBlue
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text { text: modelData.response_us + " us"; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.Colors.textPrimary; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: (parent.width - 12) / 2; height: parent.height
                radius: Theme.Metrics.cardRadius; color: Theme.Colors.cardBg
                border.color: Theme.Colors.border; border.width: 1; clip: true
                Column {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: "JOB TABLE (jobs.csv)"; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.Colors.textMuted; font.letterSpacing: 0.5 }
                    Row {
                        width: parent.width; height: 20
                        Text { width: 50; text: "seq"; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold; color: Theme.Colors.textMuted }
                        Text { width: 130; text: "release_ns"; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold; color: Theme.Colors.textMuted }
                        Text { width: 130; text: "complete_ns"; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold; color: Theme.Colors.textMuted }
                        Text { width: 70; text: "resp_us"; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold; color: Theme.Colors.textMuted }
                    }
                    ScrollView {
                        width: parent.width; height: parent.height - 58
                        clip: true
                        Column {
                            width: parent.width; spacing: 4
                            Repeater {
                                model: jobs
                                delegate: Row {
                                    width: parent.width; height: 18
                                    Text { width: 50; text: modelData.seq; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; color: Theme.Colors.textPrimary }
                                    Text { width: 130; text: modelData.release_ns; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; color: Theme.Colors.textSecondary }
                                    Text { width: 130; text: modelData.completed_ns; font.family: Theme.Typography.fontFamily; font.pixelSize: 10; color: Theme.Colors.textSecondary }
                                    Text { width: 70; text: modelData.response_us; font.family: Theme.Typography.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.Colors.textPrimary }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
