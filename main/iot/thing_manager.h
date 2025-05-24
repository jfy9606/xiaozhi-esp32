#ifndef THING_MANAGER_H
#define THING_MANAGER_H


#include "thing.h"

#include <cJSON.h>

#include <vector>
#include <memory>
#include <functional>
#include <map>

namespace iot {

class ThingManager {
public:
    static ThingManager& GetInstance() {
        static ThingManager instance;
        return instance;
    }
    ThingManager(const ThingManager&) = delete;
    ThingManager& operator=(const ThingManager&) = delete;

    void AddThing(Thing* thing);

    // 根据名称查找Thing
    Thing* FindThingByName(const std::string& name);

    // 获取所有注册的Things
    const std::vector<Thing*>& GetThings() const { return things_; }

    std::string GetDescriptorsJson();
    bool GetStatesJson(std::string& json, bool delta = false);
    void Invoke(const cJSON* command);
    
    // 检查 ThingManager 是否已初始化（至少有一个 Thing 或者已有效配置）
    bool IsInitialized() const {
        return !things_.empty();
    }

private:
    ThingManager() = default;
    ~ThingManager() = default;

    std::vector<Thing*> things_;
    std::map<std::string, std::string> last_states_;
};


} // namespace iot

#endif // THING_MANAGER_H
