import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Charts 2.15
import QtQuick.Layouts 1.15

// BrakeAnalysisTab - displays brake system analysis with charts
// Requires context property "brakeModel" of type BrakeModel
// Theme is available via: import "../theme" as Theme

Page {
    id: root
    title: i18n.tr("Brake Analysis")
    objectName: "brakeAnalysisPage"

    // Apply Material theme colors from project theme
    // Theme.Colors provides: windowBg, cardBg, textPrimary, textSecondary, brake, etc.

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10

        // --- Summary Stats Row ---
        RowLayout {
            anchors.fill: parent
            anchors.topMargin: 0
            Layout.fillHeight: 0.3

            // Brakes Triggered % (left)
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 120

                Rectangle {
                    anchors.centerIn: parent
                    width: 100; height: 100
                    radius: 16
                    color: Theme.Colors.cardBg

                    // Calculate from brakeModel.jobs
                    property int brakeCount: (parent.brakeModel ? parent.brakeModel.jobs.filter(function(j) { return j.decision === 1; }).length : 0)
                    property int totalCount: (parent.brakeModel ? parent.brakeModel.jobs.length : 0)

                    // Draw brake red slice if there are brakes triggered
                    // Using a simple rectangle overlay to represent the pie concept
                    // Since JS pie in QML Charts is done via PieSeries in ChartView
                    Text {
                        anchors.centerIn: parent
                        text: totalCount > 0 ? brakeCount + "/" + totalCount : "0/0"
                        font.pixelSize: 22
                        font.bold: true
                        color: Theme.Colors.textPrimary
                    }
                    Text {
                        anchors { bottom: parent.bottom; bottomMargin: 8 }
                        text: i18n.tr("Brakes Triggered") + ": " + (totalCount > 0 ? Math.round(100 * brakeCount / totalCount) + "%" : "0%")
                        font.pixelSize: 11
                        color: Theme.Colors.textSecondary
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Average response time (right)
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 120

                Rectangle {
                    anchors.centerIn: parent
                    width: 100; height: 100
                    radius: 16
                    color: Theme.Colors.cardBg

                    Text {
                        anchors.centerIn: parent
                        // Calculate avg response time
                        property real avgMs: parent.brakeModel ? parent.brakeModel.jobs.reduce(function(sum, j) { return sum + j.responseTimeMs; }, 0) / (parent.brakeModel ? parent.brakeModel.jobs.length : 1) : 0
                        text: totalCount > 0 ? Math.round(avgMs) + "ms" : "0ms"
                        font.pixelSize: 22
                        font.bold: true
                        color: Theme.Colors.textPrimary
                    }
                    Text {
                        anchors { bottom: parent.bottom; bottomMargin: 8 }
                        text: i18n.tr("Avg Response Time")
                        font.pixelSize: 11
                        color: Theme.Colors.textSecondary
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // --- Divider ---
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: previousLayout.bottom; topMargin: 5; height: 1; color: Theme.Colors.borderLight }
        }

        // --- Chart Section ---
        Section {
            // Header
            RowLayout {
                anchors { left: parent.left; right: parent.right; top: previousLayout.bottom; topMargin: 5 }
                Text { text: i18n.tr("Brake Performance Metrics"); font.pixelSize: 16; font.bold: true; color: Theme.Colors.textPrimary }
                Item { Layout.fillWidth: true }
            }

            // Chart 1: Response Time Line Series
            ChartView {
                id: responseChart
                title: i18n.tr("Response Time per Job (ms)")
                anchorWidgets: [summaryRow.layout]
                interactive: true

                legend.visible: false
                antialiasing: true

                ValueAxis { title: i18n.tr("Response Time (ms)"); min: 0 }
                CategoryAxis { title: i18n.tr("Job Sequence"); categories: ["1","2","3","4","5","6","7","8","9","10"] }

                LineSeries {
                    name: i18n.tr("Response Time")
                    // Points will be appended from C++ model context property
                    // The brakeModel.jobs[].responseTimeMs will be used
                    // This is populated via Component.onCompleted binding
                    // Append points for up to 10 jobs
                    for (var i = 0; i < (brakeModel ? brakeModel.jobs.length : 0); i++) {
                        var job = brakeModel.jobs[i];
                        if (job.responseTimeMs > 0) {
                            append(job.sequence, job.responseTimeMs);
                        }
                    }
                }
            }

            // Chart 2: Combined Bar (Decisions) + Line (Response Time)
            ChartView {
                id: combinedChart
                title: i18n.tr("Decisions (Bars) + Response Time (Line)")
                anchorWidgets: [summaryRow.layout]
                interactive: true

                legend.visible: true
                antialiasing: true

                // Y-axes
                ValueAxis {
                    side: ValueAxis.Left
                    title: i18n.tr("Decision (1=Brake)")
                    min: -0.5
                    max: 1.5
                }
                ValueAxis {
                    side: ValueAxis.Right
                    title: i18n.tr("Response Time (ms)")
                    min: 0
                }

                CategoryAxis {
                    title: i18n.tr("Job")
                    categories: ["1","2","3","4","5","6","7","8","9","10"]
                }

                // Bar series for decisions (0 or 1)
                BarSeries {
                    name: i18n.tr("Decision")
                    // Color bars based on decision value
                    for (var i = 0; i < (brakeModel ? brakeModel.jobs.length : 0); i++) {
                        var job = brakeModel.jobs[i];
                        // Use brake red for decision=1, gray for decision=0
                        var barColor = job.decision === 1 ? Theme.Colors.failRed : Theme.Colors.borderLight;
                        append(job.sequence, job.decision);
                        // Note: BarSeries color is set per-point or via BarSet
                        // We'll set it via the point mapping
                    }
                }

                // Line series for response time
                LineSeries {
                    name: i18n.tr("Response Time")
                    for (var i = 0; i < (brakeModel ? brakeModel.jobs.length : 0); i++) {
                        var job = brakeModel.jobs[i];
                        append(job.sequence, job.responseTimeMs);
                    }
                    pointLabelsVisible: true
                    pointLabelFormat: i18n.tr("@Value ms")
                    color: Theme.Colors.accentBlue
                }
            }

            // Chart 3: Pie chart - Decision distribution
            ChartView {
                id: pieChart
                title: i18n.tr("Brake Decision Distribution")
                anchorWidgets: [summaryRow.layout]
                interactive: true

                legend.visible: true
                antialiasing: true

                PieSeries {
                    id: decisionPie
                    antialiasing: true

                    onInitializing: {
                        // Clear any existing slices
                        decisionPie.slices.clear();

                        var brakeCount = 0;
                        var noBrakeCount = 0;
                        for (var i = 0; i < (brakeModel ? brakeModel.jobs.length : 0); i++) {
                            if (brakeModel.jobs[i].decision === 1) brakeCount++;
                            else noBrakeCount++;
                        }

                        // Add "Brakes Triggered" slice in brake red (failRed from theme)
                        decisionPie.slices.append({
                            label: i18n.tr("Brakes Triggered"),
                            value: brakeCount,
                            // Use Theme.Colors.failRed which is #ef4444 (red) for brake triggered
                            color: Theme.Colors.failRed
                        });

                        // Add "No Brake" slice in neutral gray
                        decisionPie.slices.append({
                            label: i18n.tr("No Brake"),
                            value: noBrakeCount,
                            color: Theme.Colors.borderLight
                        });
                    }
                }

                // Display percentages below the pie
                Text {
                    anchors { top: pieChart.bottom; topMargin: 5; horizontalAlignment: Text.AlignHCenter }
                    text: "Brakes: " + (decisionPie.slices.length > 0 ? Math.round(100 * decisionPie.slices[0].value / (decisionPie.slices[0].value + decisionPie.slices[1].value || 1)) + "%" : "0%") + " No Brake: " + (decisionPie.slices.length > 1 ? Math.round(100 * decisionPie.slices[1].value / (decisionPie.slices[0].value + decisionPie.slices[1].value || 1)) + "%" : "0%")
                    font.pixelSize: 11
                    color: Theme.Colors.textSecondary
                }
            }
        }
    }
}