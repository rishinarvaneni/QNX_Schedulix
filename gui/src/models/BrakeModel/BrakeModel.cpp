#include "BrakeModel.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

BrakeModel::BrakeModel(QObject *parent) : QObject(parent)
{
    m_jobs.resize(10);
    for (int i = 0; i < 10; i++) {
        m_jobs[i].sequence = i + 1;
        m_jobs[i].decision = 0;
        m_jobs[i].responseTimeMs = 0.0;
        m_jobs[i].releaseNs = 0;
        m_jobs[i].receivedNs = 0;
        m_jobs[i].completedNs = 0;
    }
}

BrakeModel::~BrakeModel() {}

void BrakeModel::loadFromJsonFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        qWarning() << "Cannot open JSON file:" << filePath;
        emit jobsChanged();
        return;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull() || !doc.isArray()) {
        qWarning() << "Invalid JSON format";
        emit jobsChanged();
        return;
    }

    QJsonArray array = doc.array();
    m_jobs.resize(qMin(array.size(), 10)); // At most 10 jobs

    for (int i = 0; i < qMin(array.size(), 10); i++) {
        if (array[i].isObject()) {
            QJsonObject obj = array[i].toObject();
            BrakeJob &job = m_jobs[i];
            job.sequence = obj["sequence"].toInt();
            job.decision = obj["decision"].toInt();          // 1 = brake triggered, 0 = no brake
            job.responseTimeMs = obj["response_time_ms"].toDouble(); // in milliseconds
            job.releaseNs = obj["release_ns"].toVariant().toLongLong();
            job.receivedNs = obj["received_ns"].toVariant().toLongLong();
            job.completedNs = obj["completed_ns"].toVariant().toLongLong();
        }
    }

    emit jobsChanged();
}

void BrakeModel::clear()
{
    m_jobs.fill(BrakeJob{0, 0, 0.0, 0, 0, 0});
    emit jobsChanged();
}