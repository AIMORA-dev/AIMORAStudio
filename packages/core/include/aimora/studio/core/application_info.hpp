// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QString>

namespace aimora::studio::core {

class ApplicationInfo final {
public:
    [[nodiscard]] static QString productName();
    [[nodiscard]] static QString version();
    [[nodiscard]] static QString requiredQtVersion();
    [[nodiscard]] static QString runtimeQtVersion();
    [[nodiscard]] static QString architectureSummary();
};

} // namespace aimora::studio::core
