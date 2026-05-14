#pragma once

#include <exception>
#include <functional>
#include <future>
#include <utility>

#include <task_scheduler/task_scheduler.hpp>

namespace task_scheduler {

struct RequestExecutionObserver {
    std::function<void(const TaskExecutionPlan&)> on_classified;
    std::function<void()> on_started;
    std::function<void(const json&)> on_finished;
    std::function<void(std::exception_ptr)> on_failed;
};

template<bool EnableThreadSafety = false>
class BasicTaskSchedulerFacade {
public:
    using AdapterType = json_session_invoke::BasicJsonSessionInvokeAdapter<EnableThreadSafety>;
    using ClassifierType = BasicRequestClassifier<EnableThreadSafety>;

    explicit BasicTaskSchedulerFacade(AdapterType& adapter)
        : adapter_(adapter)
        , scheduler_()
        , classifier_(adapter_)
    {
    }

    BasicTaskSchedulerFacade(AdapterType& adapter, std::size_t worker_count)
        : adapter_(adapter)
        , scheduler_(worker_count)
        , classifier_(adapter_)
    {
    }

    std::future<json> submitRequest(json request)
    {
        return submitRequest(std::move(request), RequestExecutionObserver{});
    }

    std::future<json> submitRequest(json request, RequestExecutionObserver observer)
    {
        auto plan = classifier_.classify(request);
        if (observer.on_classified)
        {
            observer.on_classified(plan);
        }

        return scheduler_.submit(makeScheduledTask(
            std::move(plan),
            [this, request = std::move(request), observer = std::move(observer)]() mutable {
                if (observer.on_started)
                {
                    observer.on_started();
                }

                try
                {
                    auto response = adapter_.invokeJson(request);
                    if (observer.on_finished)
                    {
                        observer.on_finished(response);
                    }
                    return response;
                }
                catch (...)
                {
                    if (observer.on_failed)
                    {
                        observer.on_failed(std::current_exception());
                    }
                    throw;
                }
            }));
    }

private:
    AdapterType& adapter_;
    KeyedTaskScheduler scheduler_;
    ClassifierType classifier_;
};

using TaskSchedulerFacadeThreadSafe = BasicTaskSchedulerFacade<true>;
using TaskSchedulerFacadeUnsafe = BasicTaskSchedulerFacade<false>;

} // namespace task_scheduler