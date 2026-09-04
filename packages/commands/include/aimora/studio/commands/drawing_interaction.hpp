// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringView>
#include <QVector>
#include <QtTypes>
#include <cstdint>
#include <optional>

namespace aimora::studio::commands {

enum class CoordinateInputKind : std::uint8_t {
    Absolute,
    Relative,
    Polar,
};

struct CoordinateInput final {
    QPointF point;
    CoordinateInputKind kind{CoordinateInputKind::Absolute};
};

class CoordinateInterpreter final {
  public:
    [[nodiscard]] static std::optional<CoordinateInput> parse(QStringView input,
                                                               const QPointF& anchor);
};

struct PrecisionViewport final {
    QPointF center;
    qreal zoom{1.0};
    qreal minimumZoom{0.01};
    qreal maximumZoom{256.0};

    [[nodiscard]] bool isValid(const QSizeF& pixelExtent) const noexcept;
    [[nodiscard]] QPointF scenePoint(const QPointF& pixel,
                                     const QSizeF& pixelExtent) const noexcept;
    [[nodiscard]] QPointF pixelPoint(const QPointF& scene,
                                     const QSizeF& pixelExtent) const noexcept;
    [[nodiscard]] bool zoomAt(const QPointF& pixel,
                              const QSizeF& pixelExtent,
                              qreal wheelSteps) noexcept;
    void panBy(const QPointF& pixelDelta) noexcept;
};

enum class SnapKind : std::uint8_t {
    None,
    Grid,
    Alignment,
    Center,
    Midpoint,
    Intersection,
    Endpoint,
    ElectricalPort,
};

struct SnapCandidate final {
    quint64 itemId{0};
    QPointF point;
    SnapKind kind{SnapKind::Endpoint};
};

struct AlignmentGuide final {
    Qt::Orientation orientation{Qt::Horizontal};
    qreal coordinate{0.0};
};

struct SnapSettings final {
    bool gridEnabled{true};
    bool objectEnabled{true};
    bool electricalPortEnabled{true};
    bool alignmentEnabled{true};
    bool orthoEnabled{false};
    bool polarEnabled{false};
    qreal gridSpacing{10.0};
    qreal tolerancePixels{10.0};
    qreal polarIncrementDegrees{15.0};

    [[nodiscard]] bool isValid() const noexcept;
};

struct SnapResult final {
    QPointF scenePoint;
    SnapKind kind{SnapKind::None};
    quint64 itemId{0};
    QVector<AlignmentGuide> guides;

    [[nodiscard]] bool snapped() const noexcept;
};

class SnapResolver final {
  public:
    [[nodiscard]] SnapResult resolve(const QPointF& rawScenePoint,
                                     const std::optional<QPointF>& anchor,
                                     const PrecisionViewport& viewport,
                                     const QSizeF& pixelExtent,
                                     const QVector<SnapCandidate>& candidates,
                                     const SnapSettings& settings) const;
};

enum class SelectionOperation : std::uint8_t {
    Replace,
    Add,
    Toggle,
    Subtract,
};

struct SelectionRecord final {
    quint64 itemId{0};
    QRectF bounds;
};

enum class EditHandleKind : std::uint8_t {
    Grip,
    VirtualNode,
};

struct EditHandle final {
    quint64 itemId{0};
    QPointF point;
    EditHandleKind kind{EditHandleKind::Grip};
};

class SelectionModel final {
  public:
    void clear() noexcept;
    void applyHit(const QVector<quint64>& hitIds, SelectionOperation operation);
    void applyMarquee(const QVector<SelectionRecord>& records,
                      const QRectF& area,
                      bool crossing,
                      SelectionOperation operation);

    [[nodiscard]] bool contains(quint64 itemId) const;
    [[nodiscard]] QVector<quint64> selectedIds() const;
    [[nodiscard]] QVector<EditHandle> handles(const QVector<SelectionRecord>& records) const;
    [[nodiscard]] qsizetype size() const noexcept;

  private:
    void applyIds(const QVector<quint64>& itemIds, SelectionOperation operation);

    QSet<quint64> selectedIds_;
};

struct CommandPreview final {
    QVector<QLineF> segments;
    QVector<QPointF> fixedPoints;
    std::optional<QPointF> pointerPoint;
};

struct CanonicalEditRequest final {
    quint64 serial{0};
    QString commandId;
    QVector<QPointF> points;
    QVector<quint64> selectedItemIds;
};

enum class CommandStartResult : std::uint8_t {
    Started,
    UnknownCommand,
    AlreadyActive,
};

class DrawingCommandSession final {
  public:
    DrawingCommandSession();

    [[nodiscard]] CommandStartResult begin(QStringView commandOrAlias);
    [[nodiscard]] bool acceptPoint(const QPointF& point);
    void updatePointer(const QPointF& point);
    [[nodiscard]] std::optional<CanonicalEditRequest>
    complete(const QVector<quint64>& selectedItemIds);
    void cancel() noexcept;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] QString activeCommandId() const;
    [[nodiscard]] std::optional<QPointF> anchor() const;
    [[nodiscard]] CommandPreview preview() const;
    [[nodiscard]] quint64 completedEditCount() const noexcept;

  private:
    [[nodiscard]] static qsizetype minimumPointCount(QStringView commandId) noexcept;
    [[nodiscard]] QString resolveCommand(QStringView commandOrAlias) const;

    QHash<QString, QString> aliases_;
    QString activeCommandId_;
    QVector<QPointF> points_;
    std::optional<QPointF> pointerPoint_;
    quint64 nextSerial_{1};
    quint64 completedEditCount_{0};
};

} // namespace aimora::studio::commands
