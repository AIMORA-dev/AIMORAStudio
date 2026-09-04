// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/catalog/catalog_library.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QResource>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

static void initializeEquipmentCatalogResource() {
    Q_INIT_RESOURCE(equipment_catalog);
}

namespace aimora::studio::catalog {
namespace {

constexpr qsizetype maximumEntries = 10'000;
constexpr qsizetype maximumKeywords = 256;
constexpr qsizetype maximumParts = 1'000;
constexpr auto catalogMimeType = "application/vnd.aimora.catalog-entry+json";

enum CatalogRole {
    IdRole = Qt::UserRole + 1,
    ScopeRole,
    CategoryRole,
    KindRole,
    SearchRole,
};

void setError(QString* errorMessage, const QString& message) {
    if(errorMessage != nullptr) {
        *errorMessage = message;
    }
}

[[nodiscard]] bool requiredString(
    const QJsonObject& object,
    const QString& key,
    QString* destination,
    QString* errorMessage
) {
    const QJsonValue value = object.value(key);
    if(!value.isString() || value.toString().trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("Catalog field '%1' is missing.").arg(key));
        return false;
    }
    *destination = value.toString();
    return true;
}

[[nodiscard]] bool stringArray(
    const QJsonValue& value,
    QStringList* destination,
    QString* errorMessage
) {
    if(!value.isArray() || value.toArray().size() > maximumKeywords) {
        setError(errorMessage, QStringLiteral("Catalog string array is invalid."));
        return false;
    }
    QSet<QString> unique;
    for(const QJsonValue item : value.toArray()) {
        if(!item.isString() || item.toString().trimmed().isEmpty()
           || unique.contains(item.toString())) {
            setError(errorMessage, QStringLiteral("Catalog string array contains invalid data."));
            return false;
        }
        unique.insert(item.toString());
        destination->push_back(item.toString());
    }
    return true;
}

[[nodiscard]] std::optional<CatalogPart>
parsePart(const QJsonObject& object, QString* errorMessage) {
    CatalogPart part;
    if(!requiredString(object, QStringLiteral("number"), &part.number, errorMessage)
       || !requiredString(
           object,
           QStringLiteral("description"),
           &part.description,
           errorMessage
       )
       || !requiredString(object, QStringLiteral("unit"), &part.unit, errorMessage)) {
        return std::nullopt;
    }
    const QJsonValue quantity = object.value(QStringLiteral("quantity"));
    if(!quantity.isDouble() || quantity.toInteger() <= 0
       || quantity.toInteger() > 1'000'000) {
        setError(errorMessage, QStringLiteral("Catalog part quantity is invalid."));
        return std::nullopt;
    }
    part.quantity = static_cast<int>(quantity.toInteger());
    return part;
}

[[nodiscard]] std::optional<CatalogEntry>
parseEntry(const QJsonObject& object, QString* errorMessage) {
    CatalogEntry entry;
    if(!requiredString(object, QStringLiteral("kind"), &entry.kind, errorMessage)
       || !requiredString(object, QStringLiteral("id"), &entry.id, errorMessage)
       || !requiredString(object, QStringLiteral("scope"), &entry.scope, errorMessage)
       || !requiredString(object, QStringLiteral("category"), &entry.category, errorMessage)
       || !requiredString(object, QStringLiteral("label"), &entry.label, errorMessage)
       || !requiredString(
           object,
           QStringLiteral("description"),
           &entry.description,
           errorMessage
       )) {
        return std::nullopt;
    }
    if(entry.kind != QStringLiteral("equipment")
       && entry.kind != QStringLiteral("assembly")) {
        setError(errorMessage, QStringLiteral("Catalog entry kind is unsupported."));
        return std::nullopt;
    }
    if(entry.scope != QStringLiteral("system") && entry.scope != QStringLiteral("user")
       && entry.scope != QStringLiteral("project")) {
        setError(errorMessage, QStringLiteral("Catalog entry scope is unsupported."));
        return std::nullopt;
    }
    entry.equipmentClass =
        object.value(QStringLiteral("equipment_class")).toString();
    entry.symbolId = object.value(QStringLiteral("symbol_id")).toString();
    entry.designatorPrefix =
        object.value(QStringLiteral("designator_prefix")).toString();
    if(entry.kind == QStringLiteral("equipment")
       && (entry.equipmentClass.isEmpty() || entry.symbolId.isEmpty()
           || entry.designatorPrefix.isEmpty())) {
        setError(errorMessage, QStringLiteral("Equipment display metadata is incomplete."));
        return std::nullopt;
    }
    if(!stringArray(
           object.value(QStringLiteral("keywords")),
           &entry.keywords,
           errorMessage
       )) {
        return std::nullopt;
    }
    const QJsonArray parts = object.value(QStringLiteral("parts")).toArray();
    if(parts.isEmpty() || parts.size() > maximumParts) {
        setError(errorMessage, QStringLiteral("Catalog parts-list data is invalid."));
        return std::nullopt;
    }
    for(const QJsonValue value : parts) {
        if(!value.isObject()) {
            setError(errorMessage, QStringLiteral("Catalog part is not an object."));
            return std::nullopt;
        }
        const auto part = parsePart(value.toObject(), errorMessage);
        if(!part.has_value()) {
            return std::nullopt;
        }
        entry.parts.push_back(*part);
    }
    const QJsonValue memberCount = object.value(QStringLiteral("member_count"));
    if(!memberCount.isDouble() || memberCount.toInteger() < 0
       || memberCount.toInteger() > 1'000) {
        setError(errorMessage, QStringLiteral("Catalog member count is invalid."));
        return std::nullopt;
    }
    entry.memberCount = memberCount.toInteger();
    if((entry.kind == QStringLiteral("assembly")) != (entry.memberCount > 0)) {
        setError(errorMessage, QStringLiteral("Catalog assembly member count is inconsistent."));
        return std::nullopt;
    }
    return entry;
}

class CatalogEntryModel final : public QStandardItemModel {
  public:
    using QStandardItemModel::QStandardItemModel;

    void setEntries(const QVector<CatalogEntry>& entries) {
        clear();
        for(const CatalogEntry& entry : entries) {
            auto* item = new QStandardItem{
                QStringLiteral("%1\n%2").arg(entry.label, entry.category)
            };
            item->setEditable(false);
            item->setData(entry.id, IdRole);
            item->setData(entry.scope, ScopeRole);
            item->setData(entry.category, CategoryRole);
            item->setData(entry.kind, KindRole);
            item->setData(
                QStringLiteral("%1 %2 %3 %4 %5")
                    .arg(
                        entry.id,
                        entry.label,
                        entry.description,
                        entry.equipmentClass,
                        entry.keywords.join(QLatin1Char(' '))
                    )
                    .toLower(),
                SearchRole
            );
            appendRow(item);
        }
    }

    [[nodiscard]] QStringList mimeTypes() const override {
        return {QString::fromLatin1(catalogMimeType)};
    }

    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override {
        auto* data = new QMimeData;
        if(indexes.isEmpty()) {
            return data;
        }
        const QModelIndex index = indexes.front();
        const QJsonObject payload{
            {QStringLiteral("catalog_id"), index.data(IdRole).toString()},
            {QStringLiteral("kind"), index.data(KindRole).toString()},
        };
        data->setData(
            QString::fromLatin1(catalogMimeType),
            QJsonDocument{payload}.toJson(QJsonDocument::Compact)
        );
        return data;
    }

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override {
        return QStandardItemModel::flags(index) | Qt::ItemIsDragEnabled;
    }
};

class CatalogFilterModel final : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setSearch(const QString& search) {
        beginFilterChange();
        searchTerms_ =
            search.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    void setScope(QString scope) {
        beginFilterChange();
        scope_ = std::move(scope);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    void setCategory(QString category) {
        beginFilterChange();
        category_ = std::move(category);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

  protected:
    [[nodiscard]] bool filterAcceptsRow(
        int sourceRow,
        const QModelIndex& sourceParent
    ) const override {
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        if(!scope_.isEmpty() && index.data(ScopeRole).toString() != scope_) {
            return false;
        }
        if(!category_.isEmpty()
           && index.data(CategoryRole).toString() != category_) {
            return false;
        }
        const QString searchText = index.data(SearchRole).toString();
        return std::all_of(
            searchTerms_.cbegin(),
            searchTerms_.cend(),
            [&searchText](const QString& term) {
                return searchText.contains(term, Qt::CaseInsensitive);
            }
        );
    }

  private:
    QStringList searchTerms_;
    QString scope_;
    QString category_;
};

} // namespace

std::optional<CatalogDocument>
CatalogDocument::fromJson(const QJsonObject& object, QString* errorMessage) {
    CatalogDocument document;
    if(!requiredString(object, QStringLiteral("schema"), &document.schema, errorMessage)
       || !requiredString(object, QStringLiteral("version"), &document.version, errorMessage)
       || !requiredString(
           object,
           QStringLiteral("source_owner"),
           &document.sourceOwner,
           errorMessage
       )) {
        return std::nullopt;
    }
    if(document.schema != QStringLiteral("aimora-equipment-library-v1")
       || document.version != QStringLiteral("1.0.0")
       || document.sourceOwner != QStringLiteral("AIMORAResources/AIMORACatalogs")) {
        setError(errorMessage, QStringLiteral("Catalog identity is unsupported."));
        return std::nullopt;
    }
    const QJsonArray collections =
        object.value(QStringLiteral("collections")).toArray();
    if(collections.size() != 3) {
        setError(errorMessage, QStringLiteral("Catalog collection set is incomplete."));
        return std::nullopt;
    }
    QSet<QString> scopes;
    for(const QJsonValue value : collections) {
        const QJsonObject collectionObject = value.toObject();
        CatalogCollection collection;
        if(!requiredString(
               collectionObject,
               QStringLiteral("scope"),
               &collection.scope,
               errorMessage
           )) {
            return std::nullopt;
        }
        const QJsonValue mutableValue =
            collectionObject.value(QStringLiteral("mutable"));
        const QJsonValue countValue =
            collectionObject.value(QStringLiteral("count"));
        if(!mutableValue.isBool() || !countValue.isDouble()
           || countValue.toInteger() < 0 || scopes.contains(collection.scope)) {
            setError(errorMessage, QStringLiteral("Catalog collection is invalid."));
            return std::nullopt;
        }
        collection.mutableCollection = mutableValue.toBool();
        collection.count = countValue.toInteger();
        scopes.insert(collection.scope);
        document.collections.push_back(collection);
    }
    const QSet<QString> requiredScopes{
        QStringLiteral("system"),
        QStringLiteral("user"),
        QStringLiteral("project"),
    };
    if(scopes != requiredScopes) {
        setError(errorMessage, QStringLiteral("Catalog collection scopes are incomplete."));
        return std::nullopt;
    }
    const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    if(entries.isEmpty() || entries.size() > maximumEntries) {
        setError(errorMessage, QStringLiteral("Catalog entry count is invalid."));
        return std::nullopt;
    }
    QSet<QString> identifiers;
    for(const QJsonValue value : entries) {
        if(!value.isObject()) {
            setError(errorMessage, QStringLiteral("Catalog entry is not an object."));
            return std::nullopt;
        }
        const auto entry = parseEntry(value.toObject(), errorMessage);
        if(!entry.has_value()) {
            return std::nullopt;
        }
        if(identifiers.contains(entry->id)) {
            setError(errorMessage, QStringLiteral("Catalog entry ID is duplicated."));
            return std::nullopt;
        }
        identifiers.insert(entry->id);
        document.entries.push_back(*entry);
    }
    for(const CatalogCollection& collection : document.collections) {
        const qsizetype actualCount = std::count_if(
            document.entries.cbegin(),
            document.entries.cend(),
            [&collection](const CatalogEntry& entry) {
                return entry.scope == collection.scope;
            }
        );
        if(actualCount != collection.count
           || collection.mutableCollection
               == (collection.scope == QStringLiteral("system"))) {
            setError(errorMessage, QStringLiteral("Catalog collection count is inconsistent."));
            return std::nullopt;
        }
    }
    return document;
}

QJsonObject bundledCatalogDocument(QString* errorMessage) {
    initializeEquipmentCatalogResource();
    QFile resource{QStringLiteral(":/aimora/catalog/equipment_catalog.json")};
    if(!resource.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Bundled equipment catalog is unavailable."));
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(resource.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("Bundled equipment catalog is malformed."));
        return {};
    }
    return document.object();
}

struct CatalogLibraryWidget::State {
    std::optional<CatalogDocument> document;
    CatalogEntryModel* sourceModel{nullptr};
    CatalogFilterModel* filterModel{nullptr};
    QLineEdit* search{nullptr};
    QComboBox* scope{nullptr};
    QComboBox* category{nullptr};
    QListView* entries{nullptr};
    QLabel* details{nullptr};
    QLabel* status{nullptr};
    QPushButton* place{nullptr};
};

CatalogLibraryWidget::CatalogLibraryWidget(QWidget* parent)
    : QWidget{parent},
      state_{std::make_unique<State>()} {
    setObjectName(QStringLiteral("aimora.catalog.library"));
    setAccessibleName(tr("Equipment library"));

    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    state_->search = new QLineEdit{this};
    state_->search->setObjectName(QStringLiteral("aimora.catalog.search"));
    state_->search->setPlaceholderText(tr("Search equipment, assemblies, or IDs"));
    state_->search->setClearButtonEnabled(true);
    state_->search->setAccessibleName(tr("Search equipment library"));
    layout->addWidget(state_->search);

    auto* filters = new QFormLayout;
    state_->scope = new QComboBox{this};
    state_->scope->setObjectName(QStringLiteral("aimora.catalog.scope"));
    state_->scope->setAccessibleName(tr("Collection scope"));
    state_->category = new QComboBox{this};
    state_->category->setObjectName(QStringLiteral("aimora.catalog.category"));
    state_->category->setAccessibleName(tr("Equipment category"));
    filters->addRow(tr("Collection"), state_->scope);
    filters->addRow(tr("Category"), state_->category);
    layout->addLayout(filters);

    state_->sourceModel = new CatalogEntryModel{this};
    state_->filterModel = new CatalogFilterModel{this};
    state_->filterModel->setSourceModel(state_->sourceModel);
    state_->filterModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    state_->filterModel->sort(0);

    state_->entries = new QListView{this};
    state_->entries->setObjectName(QStringLiteral("aimora.catalog.entries"));
    state_->entries->setModel(state_->filterModel);
    state_->entries->setSelectionMode(QAbstractItemView::SingleSelection);
    state_->entries->setDragEnabled(true);
    state_->entries->setDragDropMode(QAbstractItemView::DragOnly);
    state_->entries->setAccessibleName(tr("Equipment and reusable assemblies"));
    layout->addWidget(state_->entries, 1);

    state_->details = new QLabel{this};
    state_->details->setObjectName(QStringLiteral("aimora.catalog.details"));
    state_->details->setWordWrap(true);
    state_->details->setTextInteractionFlags(Qt::TextSelectableByKeyboard);
    state_->details->setAccessibleName(tr("Selected catalog entry details"));
    layout->addWidget(state_->details);

    state_->place = new QPushButton{tr("Place in drawing"), this};
    state_->place->setObjectName(QStringLiteral("aimora.catalog.place"));
    state_->place->setEnabled(false);
    state_->place->setAccessibleDescription(
        tr("Requests placement using the selected stable Julia catalog ID.")
    );
    layout->addWidget(state_->place);

    state_->status = new QLabel{this};
    state_->status->setObjectName(QStringLiteral("aimora.catalog.status"));
    state_->status->setAccessibleName(tr("Equipment library status"));
    layout->addWidget(state_->status);

    connect(state_->search, &QLineEdit::textChanged, this, [this](const QString& text) {
        state_->filterModel->setSearch(text);
        updateStatus();
    });
    connect(
        state_->scope,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            state_->filterModel->setScope(state_->scope->currentData().toString());
            updateStatus();
        }
    );
    connect(
        state_->category,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            state_->filterModel->setCategory(
                state_->category->currentData().toString()
            );
            updateStatus();
        }
    );
    connect(
        state_->entries->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        [this](const QModelIndex&, const QModelIndex&) {
            updateSelection();
        }
    );
    connect(
        state_->entries,
        &QListView::doubleClicked,
        this,
        [this](const QModelIndex&) {
            requestSelectedPlacement();
        }
    );
    connect(state_->place, &QPushButton::clicked, this, [this]() {
        requestSelectedPlacement();
    });

    QString error;
    const QJsonObject bundled = bundledCatalogDocument(&error);
    if(bundled.isEmpty() || !setDocument(bundled)) {
        state_->status->setText(
            error.isEmpty() ? tr("Equipment catalog could not be loaded.") : error
        );
    }
}

CatalogLibraryWidget::~CatalogLibraryWidget() = default;

bool CatalogLibraryWidget::setDocument(const QJsonObject& object) {
    QString error;
    const auto document = CatalogDocument::fromJson(object, &error);
    if(!document.has_value()) {
        state_->document.reset();
        state_->sourceModel->clear();
        state_->details->clear();
        state_->place->setEnabled(false);
        state_->status->setText(error);
        return false;
    }
    state_->document = *document;
    state_->sourceModel->setEntries(document->entries);
    {
        const QSignalBlocker blocker{state_->scope};
        state_->scope->clear();
        state_->scope->addItem(tr("All collections"), QString{});
        for(const CatalogCollection& collection : document->collections) {
            state_->scope->addItem(
                tr("%1 (%2)").arg(collection.scope).arg(collection.count),
                collection.scope
            );
        }
    }
    {
        const QSignalBlocker blocker{state_->category};
        state_->category->clear();
        state_->category->addItem(tr("All categories"), QString{});
        QSet<QString> categories;
        for(const CatalogEntry& entry : document->entries) {
            categories.insert(entry.category);
        }
        QStringList orderedCategories = categories.values();
        orderedCategories.sort(Qt::CaseInsensitive);
        for(const QString& category : orderedCategories) {
            state_->category->addItem(category, category);
        }
    }
    state_->filterModel->setScope({});
    state_->filterModel->setCategory({});
    state_->filterModel->setSearch(state_->search->text());
    updateSelection();
    updateStatus();
    return true;
}

qsizetype CatalogLibraryWidget::entryCount() const noexcept {
    return state_->document.has_value() ? state_->document->entries.size() : 0;
}

QStringList CatalogLibraryWidget::visibleEntryIds() const {
    QStringList result;
    result.reserve(state_->filterModel->rowCount());
    for(int row = 0; row < state_->filterModel->rowCount(); ++row) {
        result.push_back(
            state_->filterModel->index(row, 0).data(IdRole).toString()
        );
    }
    return result;
}

QString CatalogLibraryWidget::selectedEntryId() const {
    return state_->entries->currentIndex().data(IdRole).toString();
}

QString CatalogLibraryWidget::statusMessage() const {
    return state_->status->text();
}

void CatalogLibraryWidget::setSearchQuery(const QString& query) {
    state_->search->setText(query);
}

void CatalogLibraryWidget::setScopeFilter(const QString& scope) {
    const int index = state_->scope->findData(scope);
    if(index >= 0) {
        state_->scope->setCurrentIndex(index);
    }
}

void CatalogLibraryWidget::setCategoryFilter(const QString& category) {
    const int index = state_->category->findData(category);
    if(index >= 0) {
        state_->category->setCurrentIndex(index);
    }
}

bool CatalogLibraryWidget::selectEntry(const QString& catalogId) {
    for(int row = 0; row < state_->filterModel->rowCount(); ++row) {
        const QModelIndex index = state_->filterModel->index(row, 0);
        if(index.data(IdRole).toString() == catalogId) {
            state_->entries->setCurrentIndex(index);
            state_->entries->scrollTo(index);
            return true;
        }
    }
    return false;
}

void CatalogLibraryWidget::updateSelection() {
    const QString id = selectedEntryId();
    if(!state_->document.has_value()) {
        state_->details->setText(tr("Select equipment or a reusable assembly."));
        state_->place->setEnabled(false);
        return;
    }
    const auto found = std::find_if(
        state_->document->entries.cbegin(),
        state_->document->entries.cend(),
        [&id](const CatalogEntry& entry) {
            return entry.id == id;
        }
    );
    if(found == state_->document->entries.cend()) {
        state_->details->setText(tr("Select equipment or a reusable assembly."));
        state_->place->setEnabled(false);
        return;
    }
    const QString itemKind = found->kind == QStringLiteral("assembly")
        ? tr("Reusable assembly, %1 members").arg(found->memberCount)
        : tr("Equipment, designator %1").arg(found->designatorPrefix);
    state_->details->setText(
        tr("%1\n%2\n%3\nParts lines: %4")
            .arg(found->label, itemKind, found->description)
            .arg(found->parts.size())
    );
    state_->place->setEnabled(true);
}

void CatalogLibraryWidget::updateStatus() {
    state_->status->setText(
        tr("%1 of %2 entries")
            .arg(state_->filterModel->rowCount())
            .arg(entryCount())
    );
}

void CatalogLibraryWidget::requestSelectedPlacement() {
    const QModelIndex index = state_->entries->currentIndex();
    if(!index.isValid()) {
        return;
    }
    emit placementRequested(
        index.data(IdRole).toString(),
        index.data(KindRole).toString() == QStringLiteral("assembly")
    );
}

} // namespace aimora::studio::catalog
