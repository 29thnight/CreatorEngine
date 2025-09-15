#pragma once
#include <string>
#include <unordered_map>

// Identifies the type of trigger that causes an event.
enum class TriggerType
{
    None = 0,
    // Fired immediately when the owning trigger updates.
    Immediate,
    // Custom values can be added as needed.
};

// Identifies the target type that will handle the event.
enum class TargetType
{
    None = 0,
    // Represents a generic action. Extend as necessary.
    Generic,
};

// Describes a single event definition loaded from the CSV file.
struct EventDefinition
{
    int id{};                                   // Unique identifier for the event
    TriggerType triggerType{ TriggerType::None }; // How the event is triggered
    TargetType targetType{ TargetType::None };    // Target behaviour type
    std::unordered_map<std::string, std::string> parameters; // Optional parameters
};