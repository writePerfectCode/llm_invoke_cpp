#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <task_scheduler/request_classifier.hpp>

namespace task_scheduler {

using ScheduledTaskFunction = std::function<json()>;

struct ScheduledTask {
    TaskExecutionPlan plan;
    ScheduledTaskFunction run;
};

class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    virtual std::future<json> submit(ScheduledTask task) = 0;
};

class KeyedTaskScheduler final : public ITaskScheduler {
public:
    explicit KeyedTaskScheduler(std::size_t worker_count = defaultWorkerCount())
        : workers_(startWorkers(normalizeWorkerCount(worker_count)))
    {
    }

    ~KeyedTaskScheduler() override
    {
        stop();
    }

    std::future<json> submit(ScheduledTask task) override
    {
        WorkItem item;
        item.task = std::move(task);
        auto future = item.promise.get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                throw std::runtime_error("KeyedTaskScheduler is stopping");
            }

            pending_tasks_.push_back(std::move(item));
        }

        condition_.notify_all();
        return future;
    }

private:
    struct WorkItem {
        ScheduledTask task;
        std::promise<json> promise;
    };

    static std::size_t defaultWorkerCount() noexcept
    {
        const std::size_t concurrency = std::thread::hardware_concurrency();
        return concurrency == 0 ? 4U : concurrency;
    }

    static std::size_t normalizeWorkerCount(std::size_t worker_count) noexcept
    {
        return worker_count == 0 ? 1U : worker_count;
    }

    std::vector<std::thread> startWorkers(std::size_t worker_count)
    {
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index)
        {
            workers.emplace_back([this]() {
                workerLoop();
            });
        }

        return workers;
    }

    void workerLoop()
    {
        while (true)
        {
            WorkItem item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || hasAdmissibleTaskLocked();
                });

                if (stopping_ && pending_tasks_.empty())
                {
                    return;
                }

                auto ready_it = findNextAdmissibleTaskLocked();
                if (ready_it == pending_tasks_.end())
                {
                    continue;
                }

                markTaskStartedLocked(ready_it->task.plan);
                item = std::move(*ready_it);
                pending_tasks_.erase(ready_it);
            }

            try
            {
                item.promise.set_value(item.task.run());
            }
            catch (...)
            {
                item.promise.set_exception(std::current_exception());
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                finishTaskLocked(item.task.plan);
            }

            condition_.notify_all();
        }
    }

    void stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                return;
            }

            stopping_ = true;
            for (auto& item : pending_tasks_)
            {
                item.promise.set_exception(std::make_exception_ptr(std::runtime_error("KeyedTaskScheduler stopped")));
            }
            pending_tasks_.clear();
        }

        condition_.notify_all();
        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    bool hasAdmissibleTaskLocked() const
    {
        if (pending_tasks_.empty())
        {
            return false;
        }

        for (const auto& item : pending_tasks_)
        {
            if (canRunLocked(item.task.plan))
            {
                return true;
            }
        }

        return false;
    }

    std::deque<WorkItem>::iterator findNextAdmissibleTaskLocked()
    {
        return std::find_if(pending_tasks_.begin(), pending_tasks_.end(), [this](const WorkItem& item) {
            return canRunLocked(item.task.plan);
        });
    }

    bool canRunLocked(const TaskExecutionPlan& plan) const
    {
        const bool session_barrier_pending = hasPendingSessionBarrierLocked();

        switch (plan.category)
        {
        case SchedulingCategory::FreeReadOnly:
            return !session_barrier_active_ && !session_barrier_pending;

        case SchedulingCategory::ObjectExclusive:
            if (!plan.scheduling_key.has_value() || plan.scheduling_key->value.empty())
            {
                throw std::invalid_argument("ObjectExclusive tasks require a non-empty scheduling key");
            }

            return !session_barrier_active_ && !session_barrier_pending &&
                active_object_tasks_.find(plan.scheduling_key->value) == active_object_tasks_.end();

        case SchedulingCategory::FactoryLane:
            if (!plan.scheduling_key.has_value() || plan.scheduling_key->value.empty())
            {
                throw std::invalid_argument("FactoryLane tasks require a non-empty scheduling key");
            }

            return !session_barrier_active_ && !session_barrier_pending &&
                active_factory_tasks_.find(plan.scheduling_key->value) == active_factory_tasks_.end();

        case SchedulingCategory::ToolExclusive:
            if (!plan.scheduling_key.has_value() || plan.scheduling_key->value.empty())
            {
                throw std::invalid_argument("ToolExclusive tasks require a non-empty scheduling key");
            }

            return !session_barrier_active_ && !session_barrier_pending &&
                active_tool_tasks_.find(plan.scheduling_key->value) == active_tool_tasks_.end();

        case SchedulingCategory::SessionBarrier:
            return !session_barrier_active_ &&
                active_free_read_only_ == 0 &&
                active_object_tasks_.empty() &&
                active_factory_tasks_.empty() &&
                active_tool_tasks_.empty();
        }

        return false;
    }

    bool hasPendingSessionBarrierLocked() const
    {
        return std::any_of(pending_tasks_.begin(), pending_tasks_.end(), [](const WorkItem& item) {
            return item.task.plan.category == SchedulingCategory::SessionBarrier;
        });
    }

    void markTaskStartedLocked(const TaskExecutionPlan& plan)
    {
        switch (plan.category)
        {
        case SchedulingCategory::FreeReadOnly:
            ++active_free_read_only_;
            break;

        case SchedulingCategory::ObjectExclusive:
            active_object_tasks_[plan.scheduling_key->value] = 1;
            break;

        case SchedulingCategory::FactoryLane:
            active_factory_tasks_[plan.scheduling_key->value] = 1;
            break;

        case SchedulingCategory::ToolExclusive:
            active_tool_tasks_[plan.scheduling_key->value] = 1;
            break;

        case SchedulingCategory::SessionBarrier:
            session_barrier_active_ = true;
            break;
        }
    }

    void finishTaskLocked(const TaskExecutionPlan& plan)
    {
        switch (plan.category)
        {
        case SchedulingCategory::FreeReadOnly:
            --active_free_read_only_;
            break;

        case SchedulingCategory::ObjectExclusive:
            active_object_tasks_.erase(plan.scheduling_key->value);
            break;

        case SchedulingCategory::FactoryLane:
            active_factory_tasks_.erase(plan.scheduling_key->value);
            break;

        case SchedulingCategory::ToolExclusive:
            active_tool_tasks_.erase(plan.scheduling_key->value);
            break;

        case SchedulingCategory::SessionBarrier:
            session_barrier_active_ = false;
            break;
        }
    };

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<WorkItem> pending_tasks_;
    bool stopping_{false};
    bool session_barrier_active_{false};
    std::size_t active_free_read_only_{0};
    std::unordered_map<std::string, std::size_t> active_object_tasks_;
    std::unordered_map<std::string, std::size_t> active_factory_tasks_;
    std::unordered_map<std::string, std::size_t> active_tool_tasks_;
    std::vector<std::thread> workers_;
};

template<typename Fn>
ScheduledTask makeScheduledTask(TaskExecutionPlan plan, Fn&& fn)
{
    return ScheduledTask{
        std::move(plan),
        ScheduledTaskFunction(std::forward<Fn>(fn)),
    };
}

} // namespace task_scheduler