#include "chartgraphicsview.h"
#include <QInputDialog>
#include <QPen>
#include <QBrush>
#include <QUrlQuery>

ChartGraphicsView::ChartGraphicsView(QWidget *parent)
    : QGraphicsView(parent),
    scene(new QGraphicsScene(this)),
    netManager(new QNetworkAccessManager(this)) {

    setScene(scene);
    setRenderHint(QPainter::Antialiasing);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    mapBackgroundItem = scene->addPixmap(QPixmap());
    vectorLayerGroup = new QGraphicsItemGroup();
    scene->addItem(vectorLayerGroup);

    connect(netManager, &QNetworkAccessManager::finished, this, &ChartGraphicsView::onImageDownloaded);
}

void ChartGraphicsView::setInitialPosition(double lat, double lon, double rangeNM) {
    projection.centerLat = lat;
    projection.centerLon = lon;
    projection.rangeNM = rangeNM;
    requestChartUpdate();
}

void ChartGraphicsView::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    projection.viewWidth = viewport()->width();
    projection.viewHeight = viewport()->height();
    scene->setSceneRect(0, 0, projection.viewWidth, projection.viewHeight);
    requestChartUpdate();
}

void ChartGraphicsView::requestChartUpdate() {
    if (projection.viewWidth <= 0 || projection.viewHeight <= 0) return;

    // Adjust the endpoint and query params to your specific Chart Server spec
    QUrl url("http://your-chart-server.local/api/render");
    QUrlQuery query;
    query.addQueryItem("lat", QString::number(projection.centerLat, 'f', 6));
    query.addQueryItem("lon", QString::number(projection.centerLon, 'f', 6));
    query.addQueryItem("range", QString::number(projection.rangeNM, 'f', 2));
    query.addQueryItem("width", QString::number(projection.viewWidth));
    query.addQueryItem("height", QString::number(projection.viewHeight));
    url.setQuery(query);

    netManager->get(QNetworkRequest(url));
}

void ChartGraphicsView::onImageDownloaded(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QPixmap pixmap;
        if (pixmap.loadFromData(reply->readAll(), "PNG")) {
            mapBackgroundItem->setPixmap(pixmap);
            mapBackgroundItem->setPos(0, 0);
            redrawAllVectorLayers(); // Sync all vectors to the new chart position
        }
    }
    reply->deleteLater();
}

void ChartGraphicsView::zoomIn() {
    projection.rangeNM = qMax(0.5, projection.rangeNM * 0.75);
    requestChartUpdate();
}

void ChartGraphicsView::zoomOut() {
    projection.rangeNM = qMin(500.0, projection.rangeNM * 1.33);
    requestChartUpdate();
}

void ChartGraphicsView::wheelEvent(QWheelEvent *event) {
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
}

void ChartGraphicsView::setToolMode(ToolMode mode) {
    currentMode = mode;
    activeWaypointGeoPoints.clear();
    activeMeasureGeoPoints.clear();
    redrawAllVectorLayers();
}

void ChartGraphicsView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (currentMode == ToolMode::Pan || currentMode == ToolMode::None) {
            isPanning = true;
            panStartPos = event->pos();
            return;
        }

        QPointF geo = projection.pixelToGeo(event->pos());
        if (currentMode == ToolMode::AddAnnotation)    handleAddAnnotation(geo);
        else if (currentMode == ToolMode::AddWaypoint)  handleAddWaypoint(geo);
        else if (currentMode == ToolMode::MeasureDistance) handleMeasureDistance(geo);
    }
    QGraphicsView::mousePressEvent(event);
}

void ChartGraphicsView::mouseMoveEvent(QMouseEvent *event) {
    if (isPanning) {
        QPoint delta = event->pos() - panStartPos;
        // Visual feedback: temporarily offset the scene items while dragging
        mapBackgroundItem->setPos(delta);
        vectorLayerGroup->setPos(delta);
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ChartGraphicsView::mouseReleaseEvent(QMouseEvent *event) {
    if (isPanning && event->button() == Qt::LeftButton) {
        isPanning = false;
        QPoint delta = event->pos() - panStartPos;

        // Reset temporary visual offset
        mapBackgroundItem->setPos(0, 0);
        vectorLayerGroup->setPos(0, 0);

        // Convert the center shifted by delta to new Geo Center
        QPoint oldCenterPx(projection.viewWidth / 2, projection.viewHeight / 2);
        QPointF newCenterGeo = projection.pixelToGeo(oldCenterPx - delta);

        projection.centerLon = newCenterGeo.x();
        projection.centerLat = newCenterGeo.y();

        requestChartUpdate();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ChartGraphicsView::handleAddAnnotation(const QPointF &geo) {
    bool okId = false, okName = false;
    int id = QInputDialog::getInt(this, "Annotation", "Enter ID:", 1, 0, 100000, 1, &okId);
    if (!okId) return;

    QString name = QInputDialog::getText(this, "Annotation", "Enter Name:", QLineEdit::Normal, "", &okName);
    if (!okName || name.trimmed().isEmpty()) return;

    annotations.append({id, name, geo.y(), geo.x()});
    redrawAllVectorLayers();
}

void ChartGraphicsView::handleAddWaypoint(const QPointF &geo) {
    activeWaypointGeoPoints.append(geo);
    redrawAllVectorLayers();
}

void ChartGraphicsView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (currentMode == ToolMode::AddWaypoint && !activeWaypointGeoPoints.isEmpty()) {
        finishWaypointRoute();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ChartGraphicsView::finishWaypointRoute() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Route", "Route Name:", QLineEdit::Normal, "", &ok);
    if (ok && !name.trimmed().isEmpty()) {
        routes.append({name, activeWaypointGeoPoints});
    }
    activeWaypointGeoPoints.clear();
    redrawAllVectorLayers();
}

void ChartGraphicsView::handleMeasureDistance(const QPointF &geo) {
    activeMeasureGeoPoints.append(geo);
    redrawAllVectorLayers();
}


void ChartGraphicsView::redrawAllVectorLayers() {
    // Delete existing visual elements inside the vector group
    qDeleteAll(vectorLayerGroup->childItems());

    // 1. Render Saved Annotations
    for (const auto &ann : annotations) {
        QPointF px = projection.geoToPixel(ann.lon, ann.lat);

        auto *dot = scene->addEllipse(px.x() - 4, px.y() - 4, 8, 8, QPen(Qt::red), QBrush(Qt::red));
        auto *txt = scene->addText(QString("[%1] %2").arg(ann.id).arg(ann.name));
        txt->setDefaultTextColor(Qt::yellow);
        txt->setPos(px.x() + 6, px.y() - 10);

        dot->setParentItem(vectorLayerGroup);
        txt->setParentItem(vectorLayerGroup);
    }

    // 2. Render Completed Routes
    for (const auto &route : routes) {
        for (int i = 0; i < route.geoPoints.size(); ++i) {
            QPointF p1 = projection.geoToPixel(route.geoPoints[i].x(), route.geoPoints[i].y());
            auto *dot = scene->addEllipse(p1.x() - 3, p1.y() - 3, 6, 6, QPen(Qt::blue), QBrush(Qt::cyan));
            dot->setParentItem(vectorLayerGroup);

            if (i > 0) {
                QPointF p0 = projection.geoToPixel(route.geoPoints[i - 1].x(), route.geoPoints[i - 1].y());
                auto *line = scene->addLine(QLineF(p0, p1), QPen(Qt::cyan, 2, Qt::SolidLine));
                line->setParentItem(vectorLayerGroup);
            }
        }
    }

    // 3. Render Active Waypoint Trail
    for (int i = 0; i < activeWaypointGeoPoints.size(); ++i) {
        QPointF p = projection.geoToPixel(activeWaypointGeoPoints[i].x(), activeWaypointGeoPoints[i].y());
        auto *dot = scene->addEllipse(p.x() - 3, p.y() - 3, 6, 6, QPen(Qt::blue), QBrush(Qt::yellow));
        dot->setParentItem(vectorLayerGroup);

        if (i > 0) {
            QPointF prev = projection.geoToPixel(activeWaypointGeoPoints[i - 1].x(), activeWaypointGeoPoints[i - 1].y());
            auto *line = scene->addLine(QLineF(prev, p), QPen(Qt::yellow, 2, Qt::DashLine));
            line->setParentItem(vectorLayerGroup);
        }
    }

    // 4. Render Active Measurement
    if (activeMeasureGeoPoints.size() >= 1) {
        QPointF p1_px = projection.geoToPixel(activeMeasureGeoPoints[0].x(), activeMeasureGeoPoints[0].y());
        auto *dot1 = scene->addEllipse(p1_px.x() - 3, p1_px.y() - 3, 6, 6, QPen(Qt::green), QBrush(Qt::green));
        dot1->setParentItem(vectorLayerGroup);

        if (activeMeasureGeoPoints.size() == 2) {
            QPointF p2_px = projection.geoToPixel(activeMeasureGeoPoints[1].x(), activeMeasureGeoPoints[1].y());
            auto *dot2 = scene->addEllipse(p2_px.x() - 3, p2_px.y() - 3, 6, 6, QPen(Qt::green), QBrush(Qt::green));
            auto *line = scene->addLine(QLineF(p1_px, p2_px), QPen(Qt::green, 2));

            // Great-circle calculation
            double lat1 = activeMeasureGeoPoints[0].y(), lon1 = activeMeasureGeoPoints[0].x();
            double lat2 = activeMeasureGeoPoints[1].y(), lon2 = activeMeasureGeoPoints[1].x();

            double dLat = qDegreesToRadians(lat2 - lat1);
            double dLon = qDegreesToRadians(lon2 - lon1);
            double a = qSin(dLat / 2.0) * qSin(dLat / 2.0) +
                       qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2)) *
                           qSin(dLon / 2.0) * qSin(dLon / 2.0);
            double distNM = 3440.065 * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));

            QPointF mid = (p1_px + p2_px) / 2.0;
            auto *txt = scene->addText(QString("%1 NM").arg(distNM, 0, 'f', 2));
            txt->setDefaultTextColor(Qt::white);
            txt->setPos(mid.x() + 4, mid.y() - 10);

            dot2->setParentItem(vectorLayerGroup);
            line->setParentItem(vectorLayerGroup);
            txt->setParentItem(vectorLayerGroup);
        }
    }
}

void ChartGraphicsView::drawHeadingLine(const QPointF &startScenePos, double headingDeg) {
    double w = viewport()->width();
    double h = viewport()->height();

    // 1. Calculate edge intersection
    QPointF edgePoint = getRayEdgeIntersection(startScenePos, headingDeg, w, h);

    // 2. Draw line from starting point to the screen boundary
    QPen pen(Qt::yellow, 2, Qt::DashLine);
    QGraphicsLineItem *line = scene->addLine(QLineF(startScenePos, edgePoint), pen);

    // If you use vectorLayerGroup, attach it so it syncs with drags
    line->setParentItem(vectorLayerGroup);
}
