#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>
#include <Slic3r/Biz/PrintHost/PrintHostJobInfoTag.hpp>
#include "Slic3r/Assert.hpp"

#include <jthread/JThread.hpp>
#include <memory>
#include <functional>
#include <future>

namespace Slic3r::Biz::PrintHost {

enum class PrintHostJobProgressState : int
{
    None = 0,
    Info,
    Error,
};

struct PrintHostJobProgressPayload
{
    size_t id;
    PrintHostJobInfoTag tag = PrintHostJobInfoTag::None;
    std::string message = std::string();
};

struct PrintHostJobWrapper {
    std::unique_ptr<IPrintHost> print_host;
    size_t id;
    std::string host;
    std::vector<std::shared_ptr<PrintHostJobWrapper>> dependencies;
    std::promise<void> promise;
    std::shared_future<void> future;

    PrintHostJobWrapper(std::unique_ptr<IPrintHost>&& ph, size_t id) :
        print_host(std::move(ph)),
        id(id),
        host(print_host->get_host()),
        future(promise.get_future().share())
    {}
};

} // namespace Slic3r::Biz::PrintHost
