// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "function_list.h"

namespace star::function_list::detail {
inline constexpr qsizetype MaxDefinitionBytes = 512 * 1024;
inline constexpr qsizetype MaxExpressionBytes = 64 * 1024;
void fail(Error* error, ErrorCode code, const QString& message);
bool validUtf8(const QByteArray& bytes);
bool validateDefinition(const Definition& definition, Error* error);
}
