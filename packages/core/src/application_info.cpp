// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"

#include "aimora/studio/core/version.hpp"

#include <QtGlobal>

namespace aimora::studio::core {

QString ApplicationInfo::productName() {
    return QStringLiteral(AIMORA_STUDIO_PRODUCT_NAME);
}

QString ApplicationInfo::version() {
    return QStringLiteral(AIMORA_STUDIO_VERSION_STRING);
}

QString ApplicationInfo::requiredQtVersion() {
    return QStringLiteral(AIMORA_STUDIO_REQUIRED_QT_VERSION);
}

QString ApplicationInfo::runtimeQtVersion() {
    return QString::fromLatin1(qVersion());
}

QString ApplicationInfo::architectureSummary() {
    return QStringLiteral("C++20 / Qt 6 Widgets / out-of-process Julia");
}

} // namespace aimora::studio::core
