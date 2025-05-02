#include "components.h"
#include <esp_log.h>
#include <algorithm>
#include <cstring>

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
    if (!component) {
        return;
    }
    
    // 检查是否已存在同名组件
    auto it = std::find_if(components_.begin(), components_.end(),
        [component](const Component* c) {
            return strcmp(c->GetName(), component->GetName()) == 0;
        });
    
    if (it != components_.end()) {
        ESP_LOGW(TAG, "Component %s already registered", component->GetName());
        return;
    }
    
    components_.push_back(component);
    ESP_LOGI(TAG, "Component registered: %s", component->GetName());
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
    if (!name) {
        return nullptr;
    }
    
    auto it = std::find_if(components_.begin(), components_.end(),
        [name](const Component* c) {
            return strcmp(c->GetName(), name) == 0;
        });
    
    return (it != components_.end()) ? *it : nullptr;
}

ComponentManager::~ComponentManager() {
    StopAll();
    
    for (auto component : components_) {
        delete component;
    }
    components_.clear();
} 