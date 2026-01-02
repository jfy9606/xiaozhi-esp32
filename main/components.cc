#include "components.h"
#include <esp_log.h>
#include <algorithm>
#include <cstring>

#define TAG "Components"

// 实现单例获取方法
ComponentManager& ComponentManager::GetInstance() {
    static ComponentManager instance;
    return instance;
}

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

// 新增：按类型启动组件
void ComponentManager::StartComponentsByType(ComponentType type) {
    // 先检查该类型是否已被配置启用
    if (!IsComponentTypeEnabled(type)) {
        ESP_LOGI(TAG, "Component type %d disabled in configuration, skipping start", type);
        return;
    }
    
    ESP_LOGI(TAG, "Starting components of type %d", type);
    int count = 0;
    
    for (auto component : components_) {
        if (component->GetType() == type && !component->IsRunning()) {
            ESP_LOGI(TAG, "Starting component: %s", component->GetName());
            bool success = component->Start();
            if (success) {
                count++;
                ESP_LOGI(TAG, "Component %s started successfully", component->GetName());
            } else {
                ESP_LOGE(TAG, "Failed to start component: %s", component->GetName());
            }
        }
    }
    
    ESP_LOGI(TAG, "Started %d components of type %d", count, type);
}

void ComponentManager::StopAll() {
    for (auto component : components_) {
        if (component->IsRunning()) {
            ESP_LOGI(TAG, "Stopping component: %s", component->GetName());
            component->Stop();
        }
    }
}

// 新增：按类型停止组件
void ComponentManager::StopComponentsByType(ComponentType type) {
    ESP_LOGI(TAG, "Stopping components of type %d", type);
    int count = 0;
    
    for (auto component : components_) {
        if (component->GetType() == type && component->IsRunning()) {
            ESP_LOGI(TAG, "Stopping component: %s", component->GetName());
            component->Stop();
            count++;
        }
    }
    
    ESP_LOGI(TAG, "Stopped %d components of type %d", count, type);
}

bool ComponentManager::RegisterComponent(Component* component) {
    if (!component) {
        return false;
    }
    
    // 检查组件类型是否启用，如果没有启用则不注册
    ComponentType type = component->GetType();
    if (!IsComponentTypeEnabled(type)) {
        ESP_LOGI(TAG, "Component type %d not enabled in config, skipping registration for %s", 
                 type, component->GetName() ? component->GetName() : "unnamed");
        return false;
    }
    
    // 检查是否已存在同名组件
    auto it = std::find_if(components_.begin(), components_.end(),
        [component](const Component* c) {
            return strcmp(c->GetName(), component->GetName()) == 0;
        });
    
    if (it != components_.end()) {
        ESP_LOGW(TAG, "Component %s already registered", component->GetName());
        return false;
    }
    
    components_.push_back(component);
    ESP_LOGI(TAG, "Component registered: %s (type: %d)", component->GetName(), component->GetType());
    return true;
}

bool ComponentManager::UnregisterComponent(Component* component) {
    if (!component) {
        return false;
    }
    
        // 如果组件正在运行，先停止它
        if (component->IsRunning()) {
            component->Stop();
        }
        
        // 从列表中移除
        auto it = std::find(components_.begin(), components_.end(), component);
        if (it != components_.end()) {
            components_.erase(it);
            ESP_LOGI(TAG, "Component unregistered: %s", component->GetName());
        return true;
    }
    
    return false;
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

// 新增：获取指定类型的组件列表
std::vector<Component*> ComponentManager::GetComponentsByType(ComponentType type) {
    std::vector<Component*> result;
    
    for (auto component : components_) {
        if (component->GetType() == type) {
            result.push_back(component);
        }
    }
    
    return result;
}

// 新增：检查是否有指定类型的组件
bool ComponentManager::HasComponentType(ComponentType type) const {
    // 首先检查该类型在配置中是否启用
    if (!IsComponentTypeEnabled(type)) {
        return false;
    }
    
    // 然后检查是否有已注册的该类型组件
    for (auto component : components_) {
        if (component->GetType() == type) {
            return true;
        }
    }
    
    return false;
}

// Removed explicit destructor implementation since it's defaulted in header 