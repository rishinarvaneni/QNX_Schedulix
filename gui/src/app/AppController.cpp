#include "AppController.h"
#include "SystemMetrics.h"
#include "TaskMetrics.h"
#include "RootCauseModel.h"
#include "TimelineModel.h"
#include "JitterModel.h"
#include "ExperimentModel.h"
#include "MockProvider.h"
#include <QFile>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QQmlContext>

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    // Parent all child models to guarantee standard QObject lifetime management and clean destruction
    m_systemMetrics = new SystemMetrics(this);
    m_taskMetrics = new TaskMetrics(this);
    m_rootCauseModel = new RootCauseModel(this);
    m_timelineModel = new TimelineModel(this);
    m_jitterModel = new JitterModel(this);
    m_experimentModel = new ExperimentModel(this);
    m_brakeModel = new BrakeModel(this);  // NEW: Create brake model
    m_mockProvider = new MockProvider(this);

    // Populate all domain models from the canonical MockProvider source of truth
    m_mockProvider->populateSystemMetrics(m_systemMetrics);
    m_mockProvider->populateTaskMetrics(m_taskMetrics);
    m_mockProvider->populateRootCauseModel(m_rootCauseModel);
    m_mockProvider->populateTimelineModel(m_timelineModel);
    m_mockProvider->populateJitterModel(m_jitterModel);
    m_mockProvider->populateExperimentModel(m_experimentModel);

    // Load brake analysis data from JSON file
    // Look for the file in multiple locations
    QString brakeJsonPath;
    
    // First: resource file (:/) - deployed with the app
    brakeJsonPath = ":/data/brake_data.json";
    
    // Second: built-in build directory
    if (!QFile::exists(brakeJsonPath)) {
        brakeJsonPath = QCoreApplication::applicationDirPath() + "/brake_data.json";
    }
    
    // Third: documents directory
    if (!QFile::exists(brakeJsonPath)) {
        brakeJsonPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/brake_data.json";
    }
    
    // Load the brake data
    m_brakeModel->loadFromJsonFile(brakeJsonPath);
}

AppController::~AppController()
{
    // Destruction of this object automatically stops/releases child resources
}

void AppController::registerContextProperties(QQmlEngine *engine)
{
    if (!engine) return;
    
    QQmlContext *ctx = engine->rootContext();
    ctx->setContextProperty("appController", this);
    ctx->setContextProperty("systemMetrics", m_systemMetrics);
    ctx->setContextProperty("taskMetrics", m_taskMetrics);
    ctx->setContextProperty("rootCauseModel", m_rootCauseModel);
    ctx->setContextProperty("timelineModel", m_timelineModel);
    ctx->setContextProperty("jitterModel", m_jitterModel);
    ctx->setContextProperty("experimentModel", m_experimentModel);
    ctx->setContextProperty("brakeModel", m_brakeModel);  // NEW: Register brake model
}
