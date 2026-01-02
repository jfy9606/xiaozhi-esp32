#include "thing.h"
#include "application.h"

#include <esp_log.h>

#define TAG "Thing"


namespace iot {

#if CONFIG_IOT_PROTOCOL_XIAOZHI
static std::map<std::string, std::function<Thing*()>>* thing_creators = nullptr;
#endif

void RegisterThing(const std::string& type, std::function<Thing*()> creator) {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    if (!creator) {
        ESP_LOGW(TAG, "Attempted to register null creator for thing type: %s", type.c_str());
        return;
    }
    if (thing_creators == nullptr) {
        thing_creators = new std::map<std::string, std::function<Thing*()>>();
    }
    (*thing_creators)[type] = creator;
#endif
}

Thing* CreateThing(const std::string& type) {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    if (thing_creators == nullptr) {
        ESP_LOGE(TAG, "Thing creators map not initialized");
        return nullptr;
    }
    auto creator = thing_creators->find(type);
    if (creator == thing_creators->end()) {
        ESP_LOGE(TAG, "Thing type not found: %s", type.c_str());
        return nullptr;
    }
    if (!creator->second) {
        ESP_LOGE(TAG, "Creator function for thing type %s is null", type.c_str());
        return nullptr;
    }
    return creator->second();
#else
    return nullptr;
#endif
}

// Component interface implementation
bool Thing::Start() {
    ESP_LOGI(TAG, "Starting thing: %s", name_.c_str());
    running_ = true;
    return true;
}

void Thing::Stop() {
    ESP_LOGI(TAG, "Stopping thing: %s", name_.c_str());
    running_ = false;
}

bool Thing::IsRunning() const {
    return running_;
}

const char* Thing::GetName() const {
    return name_.c_str();
}

std::string Thing::GetDescriptorJson() {
    std::string json_str = "{";
    json_str += "\"name\":\"" + name_ + "\",";
    json_str += "\"description\":\"" + description_ + "\",";
    json_str += "\"properties\":" + properties_.GetDescriptorJson() + ",";
    json_str += "\"methods\":" + methods_.GetDescriptorJson();
    json_str += "}";
    return json_str;
}

std::string Thing::GetStateJson() {
    std::string json_str = "{";
    json_str += "\"name\":\"" + name_ + "\",";
    json_str += "\"state\":" + properties_.GetStateJson();
    json_str += "}";
    return json_str;
}

void Thing::Invoke(const cJSON* command) {
    auto method_name = cJSON_GetObjectItem(command, "method");
    auto input_params = cJSON_GetObjectItem(command, "parameters");

    try {
        auto& method = methods_[method_name->valuestring];
        for (auto& param : method.parameters()) {
            auto input_param = cJSON_GetObjectItem(input_params, param.name().c_str());
            if (param.required() && input_param == nullptr) {
                throw std::runtime_error("Parameter " + param.name() + " is required");
            }
            if (param.type() == kValueTypeNumber) {
                if (cJSON_IsNumber(input_param)) {
                    param.set_number(input_param->valueint);
                }
            } else if (param.type() == kValueTypeString) {
                if (cJSON_IsString(input_param) || cJSON_IsObject(input_param) || cJSON_IsArray(input_param)) {
                    std::string value_str = input_param->valuestring;
                    param.set_string(value_str);
                }
            } else if (param.type() == kValueTypeBoolean) {
                if (cJSON_IsBool(input_param)) {
                    param.set_boolean(input_param->valueint == 1);
                }
            }
        }

        Application::GetInstance().Schedule([&method]() {
            method.Invoke();
        });
    } catch (const std::runtime_error& e) {
        ESP_LOGE(TAG, "Method not found: %s", method_name->valuestring);
        return;
    }
}

// Get a value by property name
float Thing::GetValue(const std::string& property_name) const {
    auto it = property_values_.find(property_name);
    if (it != property_values_.end()) {
        return it->second;
    }
    
    // If not found in values map, try to get from properties
    try {
        return properties_[property_name].value();
    } catch (const std::runtime_error& e) {
        ESP_LOGW(TAG, "Property not found: %s", property_name.c_str());
        return 0.0f;
    }
}

// Get all values as a map
std::map<std::string, float> Thing::GetValues() const {
    return property_values_;
}

// Set value implementations
bool Thing::SetValue(const std::string& property_name, float value) {
    property_values_[property_name] = value;
    return true;
}

bool Thing::SetValue(const std::string& property_name, int value) {
    return SetValue(property_name, static_cast<float>(value));
}

bool Thing::SetValue(const std::string& property_name, bool value) {
    return SetValue(property_name, value ? 1.0f : 0.0f);
}

bool Thing::SetValue(const std::string& property_name, const std::string& value) {
    try {
        float float_value = std::stof(value);
        return SetValue(property_name, float_value);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to convert string to float: %s", value.c_str());
        return false;
    }
}

} // namespace iot
