#include "customgraphicsview.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QtMath>

CustomGraphicsView::CustomGraphicsView(QWidget *parent)
    : QGraphicsView(parent), scene(new QGraphicsScene(this)) {
    setScene(scene);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
}

void CustomGraphicsView::loadImage(const QString &filePath) {
    QImage image(filePath);
    if (image.isNull()) return;

    scene->clear();
    waypointPoints.clear();
    measurePoints.clear();

    imageItem = scene->addPixmap(QPixmap::fromImage(image));
    scene->setSceneRect(imageItem->boundingRect());

    // Fit the image into view while maintaining aspect ratio
    fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

void CustomGraphicsView::setToolMode(ToolMode mode) {
    currentMode = mode;
    waypointPoints.clear();
    measurePoints.clear();
    setDragMode(mode == ToolMode::None ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
}

void CustomGraphicsView::zoomIn() {
    scale(1.25, 1.25);
}

void CustomGraphicsView::zoomOut() {
    scale(0.8, 0.8);
}

void CustomGraphicsView::wheelEvent(QWheelEvent *event) {
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

void CustomGraphicsView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && currentMode != ToolMode::None) {
        QPointF scenePos = mapToScene(event->pos());

        switch (currentMode) {
        case ToolMode::AddAnnotation:
            handleAddAnnotation(scenePos);
            break;
        case ToolMode::AddWaypoint:
            handleAddWaypoint(scenePos);
            break;
        case ToolMode::MeasureDistance:
            handleMeasureDistance(scenePos);
            break;
        default:
            break;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void CustomGraphicsView::handleAddAnnotation(const QPointF &scenePos) {
    bool okId = false, okName = false;
    int id = QInputDialog::getInt(this, "Annotation ID", "Enter ID:", 1, 0, 100000, 1, &okId);
    if (!okId) return;

    QString name = QInputDialog::getText(this, "Annotation Name", "Enter Name:", QLineEdit::Normal, "", &okName);
    if (!okName || name.trimmed().isEmpty()) return;

    // Draw marker dot (fixed in scene coordinates)
    scene->addEllipse(scenePos.x() - 4, scenePos.y() - 4, 8, 8, QPen(Qt::red), QBrush(Qt::red));

    // Draw label
    QGraphicsTextItem *text = scene->addText(QString("[%1] %2").arg(id).arg(name));
    text->setDefaultTextColor(Qt::yellow);
    text->setPos(scenePos.x() + 6, scenePos.y() - 10);
}

void CustomGraphicsView::handleAddWaypoint(const QPointF &scenePos) {
    waypointPoints.append(scenePos);
    scene->addEllipse(scenePos.x() - 3, scenePos.y() - 3, 6, 6, QPen(Qt::blue), QBrush(Qt::cyan));

    if (waypointPoints.size() > 1) {
        QPointF p1 = waypointPoints[waypointPoints.size() - 2];
        QPointF p2 = waypointPoints.last();
        scene->addLine(QLineF(p1, p2), QPen(Qt::cyan, 2, Qt::DashLine));
    }
}

void CustomGraphicsView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (currentMode == ToolMode::AddWaypoint && !waypointPoints.isEmpty()) {
        finishWaypointPath();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CustomGraphicsView::finishWaypointPath() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Waypoint Group", "Enter Waypoint Route Name:", QLineEdit::Normal, "", &ok);

    if (ok && !name.trimmed().isEmpty()) {
        QGraphicsTextItem *text = scene->addText("Route: " + name);
        text->setDefaultTextColor(Qt::cyan);
        text->setPos(waypointPoints.first().x(), waypointPoints.first().y() - 20);
    }
    waypointPoints.clear();
}

void CustomGraphicsView::handleMeasureDistance(const QPointF &scenePos) {
    measurePoints.append(scenePos);
    scene->addEllipse(scenePos.x() - 3, scenePos.y() - 3, 6, 6, QPen(Qt::green), QBrush(Qt::green));

    if (measurePoints.size() == 2) {
        QLineF line(measurePoints[0], measurePoints[1]);
        scene->addLine(line, QPen(Qt::green, 2));

        double dist = line.length(); // Pixel distance
        QPointF midPoint = (measurePoints[0] + measurePoints[1]) / 2.0;

        QGraphicsTextItem *label = scene->addText(QString("%1 px").arg(dist, 0, 'f', 1));
        label->setDefaultTextColor(Qt::white);
        label->setPos(midPoint.x() + 4, midPoint.y() - 10);

        measurePoints.clear();
    }
}
