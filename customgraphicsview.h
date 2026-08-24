#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector>
#include <QPointF>

enum class ToolMode {
    None,
    AddAnnotation,
    AddWaypoint,
    MeasureDistance
};

class CustomGraphicsView : public QGraphicsView {
    Q_OBJECT

public:
    explicit CustomGraphicsView(QWidget *parent = nullptr);
    void loadImage(const QString &filePath);
    void setToolMode(ToolMode mode);
    void zoomIn();
    void zoomOut();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void handleAddAnnotation(const QPointF &scenePos);
    void handleAddWaypoint(const QPointF &scenePos);
    void handleMeasureDistance(const QPointF &scenePos);
    void finishWaypointPath();

    QGraphicsScene *scene;
    QGraphicsPixmapItem *imageItem = nullptr;
    ToolMode currentMode = ToolMode::None;

    // Waypoint state
    QVector<QPointF> waypointPoints;
    QGraphicsPathItem *activeWaypointLine = nullptr;

    // Measurement state
    QVector<QPointF> measurePoints;
    QGraphicsLineItem *activeMeasureLine = nullptr;
};
