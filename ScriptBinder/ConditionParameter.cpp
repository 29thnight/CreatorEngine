#include "ConditionParameter.h"

nlohmann::json ConditionParameter::Serialize()
{

    nlohmann::json j;
    j["param_name"] = name;
    j["param_vType"] = (int)vType;
    return j;
}

void ConditionParameter::Deserialize()
{
}
