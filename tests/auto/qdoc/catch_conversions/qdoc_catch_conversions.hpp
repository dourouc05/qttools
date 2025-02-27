// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qt_catch_conversions.hpp"

#include <boundaries/filesystem/directorypath.hpp>
#include <boundaries/filesystem/filepath.hpp>
#include <boundaries/filesystem/resolvedfile.hpp>

#include <ostream>

inline std::ostream& operator<<(std::ostream& os, const DirectoryPath& dirpath) {
    return os << dirpath.value().toStdString();
}

inline std::ostream& operator<<(std::ostream& os, const FilePath& filepath) {
    return os << filepath.value().toStdString();
}

inline std::ostream& operator<<(std::ostream& os, const ResolvedFile& resolved_file) {
    return os << "ResolvedFile{ query: " << resolved_file.get_query().toStdString() << ", " << "filepath: " << resolved_file.get_path() << " }";
}
