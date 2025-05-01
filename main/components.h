#ifndef _COMPONENTS_H_
#define _COMPONENTS_H_

#include <string>
#include <functional>

// 组件接口基类
class Component {
public:
    virtual ~Component() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual const char* GetName() const = 0;
};

// 组件管理器
class ComponentManager {
public:
    static ComponentManager& GetInstance() {
        static ComponentManager instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符
    ComponentManager(const ComponentManager&) = delete;
    ComponentManager& operator=(const ComponentManager&) = delete;

    // 启动所有组件
    void StartAll();
    
    // 停止所有组件
    void StopAll();
    
    // 注册组件
    void RegisterComponent(Component* component);
    
    // 卸载组件
    void UnregisterComponent(Component* component);
    
    // 获取组件
    Component* GetComponent(const char* name);

private:
    ComponentManager() = default;
    ~ComponentManager();

    // 保存所有注册的组件
    std::vector<Component*> components_;
};

#endif // _COMPONENTS_H_ 