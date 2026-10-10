#ifndef BRAKE_MODEL_H
#define BRAKE_MODEL_H

#include <QObject>
#include <QVector>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

struct BrakeJob {
    int sequence;
    int decision;       // 1 = brake triggered, 0 = no brake
    qreal responseTimeMs; // in milliseconds
    qlonglong releaseNs;
    qlonglong receivedNs;
    qlonglong completedNs;
};

class BrakeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVector<BrakeJob> jobs READ jobs NOTIFY jobsChanged)

public:
    explicit BrakeModel(QObject *parent = nullptr);
    ~BrakeModel();

    QVector<BrakeJob> jobs() const { return m_jobs; }

    void loadFromJsonFile(const QString &filePath);
    void clear();

signals:
    void jobsChanged();

private:
    QVector<BrakeJob> m_jobs;
    QTimer m_retryTimer;
};

#endif // BRAKE_MODEL_H