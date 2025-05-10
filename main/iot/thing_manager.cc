#include "thing_manager.h"

#include <esp_log.h>

#define TAG "ThingManager"

namespace iot {

void ThingManager::AddThing(Thing* thing) {
    things_.push_back(thing);
}

std::string ThingManager::GetDescriptorsJson() {
    std::string json_str = "[";
    for (auto& thing : things_) {
        json_str += thing->GetDescriptorJson() + ",";
    }
    if (json_str.back() == ',') {
        json_str.pop_back();
    }
    json_str += "]";
    return json_str;
}

bool ThingManager::GetStatesJson(std::string& json, bool delta) {
    if (!delta) {
        last_states_.clear();
    }
    bool changed = false;
    json = "[";
    // 枚举thing，获取每个thing的state，如果发生变化，则更新，保存到last_states_
    // 如果delta为true，则只返回变化的部分
    for (auto& thing : things_) {
        std::string state = thing->GetStateJson();
        if (delta) {
            // 如果delta为true，则只返回变化的部分
            auto it = last_states_.find(thing->name());
            if (it != last_states_.end() && it->second == state) {
                continue;
            }
            changed = true;
            last_states_[thing->name()] = state;
        }
        json += state + ",";
    }
    if (json.back() == ',') {
        json.pop_back();
    }
    json += "]";
    return changed;
}

void ThingManager::Invoke(const cJSON* command) {
    // 添加安全检查
    if (!command) {
        ESP_LOGE(TAG, "Command is null");
        return;
    }
    
    auto name = cJSON_GetObjectItem(command, "name");
    // 检查 name 是否有效
    if (!name || !cJSON_IsString(name) || !name->valuestring) {
        // 没有 name 字段，尝试根据 command 字段查找
        auto cmd = cJSON_GetObjectItem(command, "command");
        if (cmd && cJSON_IsString(cmd) && cmd->valuestring) {
            // 对于通用命令，遍历所有 things，查找可以处理该命令的 thing
            ESP_LOGI(TAG, "Command without name, try to find by command: %s", cmd->valuestring);
            for (auto& thing : things_) {
                try {
                    thing->Invoke(command);
                    // 不返回，让所有匹配的 thing 都有机会处理命令
                } catch (const std::exception& e) {
                    ESP_LOGW(TAG, "Error invoking command on thing %s: %s", 
                             thing->name().c_str(), e.what());
                }
            }
        } else {
            ESP_LOGE(TAG, "Command has no valid name or command field");
        }
        return;
    }
    
    // 正常通过 name 查找 thing
    for (auto& thing : things_) {
        if (thing->name() == name->valuestring) {
            try {
                thing->Invoke(command);
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Error invoking command on thing %s: %s", 
                         thing->name().c_str(), e.what());
            }
            return;
        }
    }
    
    ESP_LOGW(TAG, "Thing with name '%s' not found", name->valuestring);
}

} // namespace iot
