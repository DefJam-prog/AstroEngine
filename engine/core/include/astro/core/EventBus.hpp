#pragma once

#include "astro/core/Result.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace astro::core
{
using SubscriptionId = std::uint64_t;

class EventBus
{
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <typename Event>
    Result<SubscriptionId> subscribe(std::function<void(const Event&)> callback)
    {
        if (!callback)
            return Result<SubscriptionId>::failure({ErrorCode::InvalidArgument, "Event callback cannot be empty"});
        if (nextId_ == 0)
            return Result<SubscriptionId>::failure({ErrorCode::InternalError, "Event subscription ID space exhausted"});

        const auto id = nextId_++;
        auto slot = std::make_shared<Slot<Event>>(id, std::move(callback));
        subscribers_[std::type_index(typeid(Event))].push_back(std::move(slot));
        return Result<SubscriptionId>::success(id);
    }

    Result<void> unsubscribe(SubscriptionId id)
    {
        for (auto& [eventType, slots] : subscribers_)
        {
            (void)eventType;
            const auto found = std::find_if(slots.begin(), slots.end(), [id](const auto& slot) { return slot->id == id; });
            if (found != slots.end())
            {
                (*found)->active = false;
                slots.erase(found);
                return Result<void>::success();
            }
        }
        return Result<void>::failure({ErrorCode::NotFound, "Event subscription does not exist"});
    }

    template <typename Event>
    Result<void> publish(const Event& event)
    {
        const auto found = subscribers_.find(std::type_index(typeid(Event)));
        if (found == subscribers_.end()) return Result<void>::success();

        const auto snapshot = found->second;
        std::string errors;
        for (const auto& baseSlot : snapshot)
        {
            const auto slot = std::static_pointer_cast<Slot<Event>>(baseSlot);
            if (!slot->active) continue;
            try
            {
                slot->callback(event);
            }
            catch (const std::exception& error)
            {
                if (!errors.empty()) errors += "\n";
                errors += std::string("Event callback failed: ") + error.what();
            }
            catch (...)
            {
                if (!errors.empty()) errors += "\n";
                errors += "Event callback failed with an unknown exception";
            }
        }
        if (!errors.empty()) return Result<void>::failure({ErrorCode::InternalError, std::move(errors)});
        return Result<void>::success();
    }

private:
    struct SlotBase
    {
        explicit SlotBase(SubscriptionId subscriptionId) : id(subscriptionId) {}
        virtual ~SlotBase() = default;
        SubscriptionId id;
        bool active = true;
    };

    template <typename Event>
    struct Slot final : SlotBase
    {
        Slot(SubscriptionId id, std::function<void(const Event&)> callback)
            : SlotBase(id), callback(std::move(callback)) {}
        std::function<void(const Event&)> callback;
    };

    SubscriptionId nextId_ = 1;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<SlotBase>>> subscribers_;
};
}
