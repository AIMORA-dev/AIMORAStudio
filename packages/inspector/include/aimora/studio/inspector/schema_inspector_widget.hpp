// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/inspector/inspection_document.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>

namespace aimora::studio::protocol {
class ServiceClient;
}

namespace aimora::studio::inspector {

class SchemaInspectorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit SchemaInspectorWidget(QWidget* parent = nullptr);
    ~SchemaInspectorWidget() override;

    void bindServiceClient(protocol::ServiceClient* client);
    void inspect(const InspectionIdentity& identity);
    [[nodiscard]] bool setDocument(const QJsonObject& object);
    void clearInspection();

    [[nodiscard]] const InspectionDocument* document() const noexcept;
    [[nodiscard]] bool hasLocalEdits() const noexcept;
    [[nodiscard]] QJsonObject pendingCommitRequest() const;
    [[nodiscard]] QString statusMessage() const;

  signals:
    void affectedViewsChanged(const QStringList& modelPaths,
                              const QStringList& viewIds,
                              const QStringList& invalidatedResultIds);

  private:
    struct State;
    std::unique_ptr<State> state_;

    void renderDocument();
    void updateActions();
    void setStatus(const QString& message, bool error = false);
    void stageScalar(const QString& path);
    void stageTable(const QString& path);
    void submitCommit();
    void submitHistory(bool undo);
    void reloadAuthoritative();
    void handleResponse(const QString& requestId,
                        bool ok,
                        const QJsonObject& result,
                        const QString& errorCode,
                        const QString& errorMessage);
};

} // namespace aimora::studio::inspector
