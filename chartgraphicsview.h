#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsItemGroup>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QWheelEvent>

#pragma once
#include <QPointF>
#include <QtMath>

#include <QString>
#include <cmath>

QString decimalToDMS(double decimalDegrees, bool isLatitude) {
    char hemisphere;
    if (isLatitude) {
        hemisphere = (decimalDegrees >= 0.0) ? 'N' : 'S';
    } else {
        hemisphere = (decimalDegrees >= 0.0) ? 'E' : 'W';
    }

    double val = std::abs(decimalDegrees);
    int degrees = static_cast<int>(val);

    double totalMinutes = (val - degrees) * 60.0;
    int minutes = static_cast<int>(totalMinutes);

    double seconds = (totalMinutes - minutes) * 60.0;

    return QString("%1°%2'%3\" %4")
        .arg(degrees)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 5, 'f', 2, QChar('0'))
        .arg(hemisphere);
}

// Example usage:
// QString latStr = decimalToDMS(10.27738, true);   // "10°16'38.57" N"
// QString lonStr = decimalToDMS(76.15792, false);  // "76°09'28.51" E"
struct ChartProjection {
    double centerLat = 0.0;
    double centerLon = 0.0;
    double rangeNM   = 10.0; // Total vertical coverage or view radius in Nautical Miles
    int viewWidth    = 800;
    int viewHeight   = 600;

    // Convert Screen (Pixel) to Geo (Lon, Lat)
    QPointF pixelToGeo(const QPointF &px) const {
        double nmPerPixelY = rangeNM / static_cast<double>(viewHeight);
        double nmPerPixelX = nmPerPixelY; // 1:1 aspect ratio

        double dy_pixels = px.y() - (viewHeight / 2.0);
        double dx_pixels = px.x() - (viewWidth / 2.0);

        // 1 Nautical Mile = 1 minute of latitude = 1/60 degrees
        double dLat = -(dy_pixels * nmPerPixelY) / 60.0;
        // Longitude convergence depends on latitude
        double latRad = qDegreesToRadians(centerLat);
        double dLon = (dx_pixels * nmPerPixelX) / (60.0 * qCos(latRad));

        return QPointF(centerLon + dLon, centerLat + dLat); // x=Lon, y=Lat
    }

    // Convert Geo (Lon, Lat) to Screen (Pixel)
    QPointF geoToPixel(double lon, double lat) const {
        double nmPerPixelY = rangeNM / static_cast<double>(viewHeight);
        double nmPerPixelX = nmPerPixelY;

        double dLat = lat - centerLat;
        double dLon = lon - centerLon;

        double dy_nm = -dLat * 60.0;
        double dx_nm = dLon * 60.0 * qCos(qDegreesToRadians(centerLat));

        double px_x = (viewWidth / 2.0) + (dx_nm / nmPerPixelX);
        double px_y = (viewHeight / 2.0) + (dy_nm / nmPerPixelY);

        return QPointF(px_x, px_y);
    }
};

enum class ToolMode { None, Pan, AddAnnotation, AddWaypoint, MeasureDistance };

struct AnnotationData {
    int id;
    QString name;
    double lat;
    double lon;
};

struct WaypointRoute {
    QString name;
    QVector<QPointF> geoPoints; // x = lon, y = lat
};

#include <QPointF>
#include <QtMath>
#include <limits>
#include <algorithm>

// startPoint: Origin in scene/viewport coordinates
// headingDeg: Angle in degrees (0 = North/Up, 90 = East/Right, clockwise)
// w, h: Viewport dimensions (viewport()->width(), viewport()->height())
QPointF getRayEdgeIntersection(const QPointF &startPoint, double headingDeg, double w, double h) {
    double rad = qDegreesToRadians(headingDeg);
    double dx =  qSin(rad);
    double dy = -qCos(rad); // Negative because screen Y increases downward

    double minT = std::numeric_limits<double>::infinity();

    // 1. Check vertical boundaries (X = 0, X = w)
    if (dx > 1e-6) {
        double t = (w - startPoint.x()) / dx;
        if (t > 0) minT = std::min(minT, t);
    } else if (dx < -1e-6) {
        double t = (0.0 - startPoint.x()) / dx;
        if (t > 0) minT = std::min(minT, t);
    }

    // 2. Check horizontal boundaries (Y = 0, Y = h)
    if (dy > 1e-6) {
        double t = (h - startPoint.y()) / dy;
        if (t > 0) minT = std::min(minT, t);
    } else if (dy < -1e-6) {
        double t = (0.0 - startPoint.y()) / dy;
        if (t > 0) minT = std::min(minT, t);
    }

    // Edge intersection point
    return QPointF(startPoint.x() + minT * dx, startPoint.y() + minT * dy);
}

class ChartGraphicsView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ChartGraphicsView(QWidget *parent = nullptr);
    void setInitialPosition(double lat, double lon, double rangeNM);
    void setToolMode(ToolMode mode);
    void zoomIn();
    void zoomOut();

    void drawHeadingLine(const QPointF &startScenePos, double headingDeg);
signals:
    void chartRequested(double lat, double lon, double range, int w, int h);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onImageDownloaded(QNetworkReply *reply);

private:
    void requestChartUpdate();
    void redrawAllVectorLayers();

    // Interaction handlers
    void handleAddAnnotation(const QPointF &geo);
    void handleAddWaypoint(const QPointF &geo);
    void handleMeasureDistance(const QPointF &geo);
    void finishWaypointRoute();

    ChartProjection projection;
    ToolMode currentMode = ToolMode::None;

    QGraphicsScene *scene;
    QGraphicsPixmapItem *mapBackgroundItem;
    QGraphicsItemGroup *vectorLayerGroup;

    // Pan tracking
    bool isPanning = false;
    QPoint panStartPos;

    // Data models (persistent in Geo coordinates)
    QVector<AnnotationData> annotations;
    QVector<WaypointRoute> routes;
    QVector<QPointF> activeWaypointGeoPoints;
    QVector<QPointF> activeMeasureGeoPoints;

    QNetworkAccessManager *netManager;
};
