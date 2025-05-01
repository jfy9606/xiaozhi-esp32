#include "components.h"
#include <esp_log.h>
#include <algorithm>

#define TAG "Components"

void ComponentManager::StartAll() {
    for (auto component : components_) {
        if (!component->IsRunning()) {
            ESP_LOGI(TAG, "Starting component: %s", component->GetName());
            bool success = component->Start();
            if (success) {
                ESP_LOGI(TAG, "Component %s started successfully", component->GetName());
            } else {
                ESP_LOGE(TAG, "Failed to start component: %s", component->GetName());
            }
        }
    }
}

void ComponentManager::StopAll() {
    for (auto component : components_) {
        if (component->IsRunning()) {
            ESP_LOGI(TAG, "Stopping component: %s", component->GetName());
            component->Stop();
        }
    }
}

void ComponentManager::RegisterComponent(Component* component) {
    if (component) {
        // 检查组件是否已经注册
        auto it = std::find_if(components_.begin(), components_.end(), 
            [component](Component* c) { 
                return strcmp(c->GetName(), component->GetName()) == 0; 
            });
        
        if (it == components_.end()) {
            components_.push_back(component);
            ESP_LOGI(TAG, "Component registered: %s", component->GetName());
        } else {
            ESP_LOGW(TAG, "Component %s already registered", component->GetName());
        }
    }
}

void ComponentManager::UnregisterComponent(Component* component) {
    if (component) {
        // 如果组件正在运行，先停止它
        if (component->IsRunning()) {
            component->Stop();
        }
        
        // 从列表中移除
        auto it = std::find(components_.begin(), components_.end(), component);
        if (it != components_.end()) {
            components_.erase(it);
            ESP_LOGI(TAG, "Component unregistered: %s", component->GetName());
        }
    }
}

Component* ComponentManager::GetComponent(const char* name) {
    auto it = std::find_if(components_.begin(), components_.end(), 
        [name](Component* c) { 
            return strcmp(c->GetName(), name) == 0; 
        });
    
    if (it != components_.end()) {
        return *it;
    }
    
    return nullptr;
}

ComponentManager::~ComponentManager() {
    StopAll();
    // 注意: 不要删除组件，因为它们可能在其他地方被使用
} 