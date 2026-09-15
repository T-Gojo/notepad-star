// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "function_list.h"
#include <memory>

class ScintillaSearch {
public:
    explicit ScintillaSearch(const QByteArray& source);
    ~ScintillaSearch();
    star::function_list::SearchReply operator()(const star::function_list::SearchRequest&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
