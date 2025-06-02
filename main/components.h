#pragma once

#include <string>
#include <functional>
#include <vector>
#include "sdkconfig.h"

// 组件类型枚举，用于更好地组织和控制组件
enum ComponentType {
    COMPONENT_TYPE_GENERIC = 0,    // 基础通用组件
    COMPONENT_TYPE_WEB = 1,        // Web服务相关组件
    COMPONENT_TYPE_VISION = 2,     // 视觉相关组件
    COMPONENT_TYPE_MOTOR = 3,      // 电机控制相关组件
    COMPONENT_TYPE_IOT = 4,        // IoT设备相关组件
    COMPONENT_TYPE_AUDIO = 5,      // 音频相关组件
    COMPONENT_TYPE_SYSTEM = 6,     // 系统相关组件
    COMPONENT_TYPE_LOCATION = 7    // 位置定位相关组件
};

/**
 * Base interface for all components
 */
class Component {
public:
    virtual ~Component() = default;
    
    /**
     * Get component name
     * @return Component name
     */
    virtual const char* GetName() const = 0;
    
    /**
     * Start the component
     * @return true if started successfully
     */
    virtual bool Start() = 0;
    
    /**
     * Stop the component
     */
    virtual void Stop() = 0;
    
    /**
     * Check if component is running
     * @return true if running
     */
    virtual bool IsRunning() const = 0;
    
    // 新增：获取组件类型
    virtual ComponentType GetType() const { return COMPONENT_TYPE_GENERIC; }
    
    // 新增：组件是否已初始化
    bool IsInitialized() const { return is_initialized_; }
    void SetInitialized(bool initialized) { is_initialized_ = initialized; }

protected:
    // 组件初始化状态
    bool is_initialized_ = false;
};

/**
 * Component Manager singleton to manage system components
 */
class ComponentManager {
public:
    /**
     * Get singleton instance
     * @return ComponentManager singleton instance
     */
    static ComponentManager& GetInstance();
    
    /**
     * Register a component
     * @param component Component to register
     * @return true if registered successfully
     */
    bool RegisterComponent(Component* component);
    
    /**
     * Unregister a component
     * @param component Component to unregister
     * @return true if unregistered successfully
     */
    bool UnregisterComponent(Component* component);
    
    /**
     * Get a component by name
     * @param name Component name
     * @return Component pointer or nullptr if not found
     */
    Component* GetComponent(const char* name);

    // 启动所有组件
    void StartAll();
    
    // 启动指定类型的组件
    void StartComponentsByType(ComponentType type);
    
    // 停止所有组件
    void StopAll();
    
    // 停止指定类型的组件
    void StopComponentsByType(ComponentType type);
    
    // 获取指定类型的组件列表
    std::vector<Component*> GetComponentsByType(ComponentType type);
    
    // 获取所有组件
    const std::vector<Component*>& GetComponents() const {
        return components_;
    }
    
    // 检查是否有指定类型的组件
    bool HasComponentType(ComponentType type) const;
    
    // 模块化条件编译检查 - 允许根据配置条件检查某类组件是否可用
    static bool IsComponentTypeEnabled(ComponentType type) {
        switch(type) {
            case COMPONENT_TYPE_WEB:
                #if defined(CONFIG_ENABLE_WEB_SERVER)
                return true;
                #else
                return false;
                #endif
                
            case COMPONENT_TYPE_VISION:
                #if defined(CONFIG_ENABLE_VISION_CONTROLLER)
                return true;
                #else
                return false;
                #endif
                
            case COMPONENT_TYPE_MOTOR:
                #if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
                return true;
                #else
                return false;
                #endif
                
            case COMPONENT_TYPE_LOCATION:
                #if defined(CONFIG_ENABLE_LOCATION_CONTROLLER)
                return true;
                #else
                return false;
                #endif
                
            case COMPONENT_TYPE_IOT:
                #if defined(CONFIG_IOT_PROTOCOL_XIAOZHI) || defined(CONFIG_IOT_PROTOCOL_MCP)
                return true;
                #else
                return false;
                #endif
                
            case COMPONENT_TYPE_AUDIO:
                return true; // 音频组件总是启用
                
            default:
                return true; // 通用组件和系统组件默认启用
        }
    }

private:
    ComponentManager() = default;
    ~ComponentManager() = default;
    ComponentManager(const ComponentManager&) = delete;
    ComponentManager& operator=(const ComponentManager&) = delete;

    // 保存所有注册的组件
    std::vector<Component*> components_;
};