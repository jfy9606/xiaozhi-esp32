#include "simple_error_handler.h"
#include "esp_log.h"
#include "cJSON.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

static const char* TAG = "SimpleErrorHandler";

// Static member definitions
std::deque<SimpleErrorHandler::ErrorRecord> SimpleErrorHandler::error_log_;
std::mutex SimpleErrorHandler::error_mutex_;
size_t SimpleErrorHandler::max_error_records_ = 100;

void SimpleErrorHandler::LogError(ErrorLevel level, const std::string& component, 
                                 const std::string& message) {
    // Log to ESP-IDF logging system
    switch (level) {
        case ErrorLevel::INFO:
            ESP_LOGI(TAG, "[%s] %s", component.c_str(), message.c_str());
            break;
        case ErrorLevel::WARNING:
            ESP_LOGW(TAG, "[%s] %s", component.c_str(), message.c_str());
            break;
        case ErrorLevel::ERROR:
            ESP_LOGE(TAG, "[%s] %s", component.c_str(), message.c_str());
            break;
        case ErrorLevel::CRITICAL:
            ESP_LOGE(TAG, "[CRITICAL][%s] %s", component.c_str(), message.c_str());
            break;
    }

    // Create error record
    ErrorRecord record;
    record.level = level;
    record.component = component;
    record.message = message;
    record.timestamp = std::chrono::system_clock::now();
    record.count = 1;

    AddErrorRecord(record);
}

void SimpleErrorHandler::LogInfo(const std::string& component, const std::string& message) {
    LogError(ErrorLevel::INFO, component, message);
}

void SimpleErrorHandler::LogWarning(const std::string& component, const std::string& message) {
    LogError(ErrorLevel::WARNING, component, message);
}

void SimpleErrorHandler::LogCritical(const std::string& component, const std::string& message) {
    LogError(ErrorLevel::CRITICAL, component, message);
}

std::vector<SimpleErrorHandler::ErrorRecord> SimpleErrorHandler::GetRecentErrors(size_t max_count) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    std::vector<ErrorRecord> result;
    size_t count = (max_count == 0) ? error_log_.size() : std::min(max_count, error_log_.size());
    
    // Get most recent errors (from the end of deque)
    auto start_it = error_log_.end() - count;
    result.assign(start_it, error_log_.end());
    
    // Reverse to get most recent first
    std::reverse(result.begin(), result.end());
    
    return result;
}

std::vector<SimpleErrorHandler::ErrorRecord> SimpleErrorHandler::GetComponentErrors(
    const std::string& component, size_t max_count) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    std::vector<ErrorRecord> result;
    size_t count = 0;
    
    // Search from most recent to oldest
    for (auto it = error_log_.rbegin(); it != error_log_.rend() && 
         (max_count == 0 || count < max_count); ++it) {
        if (it->component == component) {
            result.push_back(*it);
            count++;
        }
    }
    
    return result;
}

size_t SimpleErrorHandler::GetErrorCount(ErrorLevel level) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    return std::count_if(error_log_.begin(), error_log_.end(),
                        [level](const ErrorRecord& record) {
                            return record.level == level;
                        });
}

size_t SimpleErrorHandler::GetTotalErrorCount() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_log_.size();
}

void SimpleErrorHandler::ClearErrors() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_log_.clear();
    ESP_LOGI(TAG, "All error records cleared");
}

void SimpleErrorHandler::ClearComponentErrors(const std::string& component) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    auto new_end = std::remove_if(error_log_.begin(), error_log_.end(),
                                 [&component](const ErrorRecord& record) {
                                     return record.component == component;
                                 });
    
    size_t removed_count = error_log_.end() - new_end;
    error_log_.erase(new_end, error_log_.end());
    
    ESP_LOGI(TAG, "Cleared %zu error records for component: %s", removed_count, component.c_str());
}

bool SimpleErrorHandler::HasCriticalErrors() {
    return GetErrorCount(ErrorLevel::CRITICAL) > 0;
}

std::string SimpleErrorHandler::GetErrorSummaryJson() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return "{}";
    }
    
    // Add error counts by level
    cJSON* counts = cJSON_CreateObject();
    cJSON_AddNumberToObject(counts, "info", GetErrorCount(ErrorLevel::INFO));
    cJSON_AddNumberToObject(counts, "warning", GetErrorCount(ErrorLevel::WARNING));
    cJSON_AddNumberToObject(counts, "error", GetErrorCount(ErrorLevel::ERROR));
    cJSON_AddNumberToObject(counts, "critical", GetErrorCount(ErrorLevel::CRITICAL));
    cJSON_AddNumberToObject(counts, "total", error_log_.size());
    cJSON_AddItemToObject(root, "counts", counts);
    
    // Add recent errors (last 10)
    cJSON* recent_errors = cJSON_CreateArray();
    size_t recent_count = std::min(size_t(10), error_log_.size());
    
    for (auto it = error_log_.end() - recent_count; it != error_log_.end(); ++it) {
        cJSON* error_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(error_obj, "level", ErrorLevelToString(it->level).c_str());
        cJSON_AddStringToObject(error_obj, "component", it->component.c_str());
        cJSON_AddStringToObject(error_obj, "message", it->message.c_str());
        cJSON_AddNumberToObject(error_obj, "count", it->count);
        
        // Convert timestamp to string
        auto time_t = std::chrono::system_clock::to_time_t(it->timestamp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        cJSON_AddStringToObject(error_obj, "timestamp", ss.str().c_str());
        
        cJSON_AddItemToArray(recent_errors, error_obj);
    }
    cJSON_AddItemToObject(root, "recent_errors", recent_errors);
    
    char* json_string = cJSON_Print(root);
    std::string result = json_string ? json_string : "{}";
    
    if (json_string) {
        free(json_string);
    }
    cJSON_Delete(root);
    
    return result;
}

std::vector<std::string> SimpleErrorHandler::GetRecoverySuggestions(const std::string& component) {
    std::vector<std::string> suggestions;
    
    if (component == "HW178") {
        suggestions.push_back("Check I2C connections to HW178 multiplexer");
        suggestions.push_back("Verify PCA9548A is properly initialized");
        suggestions.push_back("Check power supply to HW178 module");
        suggestions.push_back("Try reinitializing the multiplexer system");
    } else if (component == "PCF8575") {
        suggestions.push_back("Check I2C connections to PCF8575 GPIO expander");
        suggestions.push_back("Verify PCA9548A channel selection");
        suggestions.push_back("Check power supply to PCF8575 module");
        suggestions.push_back("Verify GPIO pin configuration");
    } else if (component == "LU9685") {
        suggestions.push_back("Check I2C connections to LU9685 servo controller");
        suggestions.push_back("Verify PCA9548A channel selection");
        suggestions.push_back("Check power supply to LU9685 module");
        suggestions.push_back("Verify servo connections and power");
    } else if (component == "PCA9548A") {
        suggestions.push_back("Check I2C bus connections");
        suggestions.push_back("Verify I2C bus handle is valid");
        suggestions.push_back("Check power supply to PCA9548A");
        suggestions.push_back("Try different I2C address if applicable");
    } else if (component == "Sensor") {
        suggestions.push_back("Check sensor connections");
        suggestions.push_back("Verify sensor configuration");
        suggestions.push_back("Check expander channel selection");
        suggestions.push_back("Verify sensor power supply");
    } else if (component == "Motor") {
        suggestions.push_back("Check motor connections");
        suggestions.push_back("Verify motor driver configuration");
        suggestions.push_back("Check motor power supply");
        suggestions.push_back("Verify GPIO pin assignments");
    } else if (component == "Servo") {
        suggestions.push_back("Check servo connections");
        suggestions.push_back("Verify servo power supply");
        suggestions.push_back("Check PWM signal integrity");
        suggestions.push_back("Verify servo angle limits");
    } else {
        suggestions.push_back("Check hardware connections");
        suggestions.push_back("Verify configuration settings");
        suggestions.push_back("Check power supplies");
        suggestions.push_back("Try restarting the system");
    }
    
    return suggestions;
}

std::string SimpleErrorHandler::ErrorLevelToString(ErrorLevel level) {
    switch (level) {
        case ErrorLevel::INFO:
            return "INFO";
        case ErrorLevel::WARNING:
            return "WARNING";
        case ErrorLevel::ERROR:
            return "ERROR";
        case ErrorLevel::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

void SimpleErrorHandler::SetMaxErrorRecords(size_t max_records) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    max_error_records_ = max_records;
    
    // Trim existing records if necessary
    while (error_log_.size() > max_error_records_) {
        error_log_.pop_front();
    }
    
    ESP_LOGI(TAG, "Maximum error records set to %zu", max_records);
}

void SimpleErrorHandler::AddErrorRecord(const ErrorRecord& record) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    // Check for duplicate errors (same component and message)
    auto existing = FindExistingError(record.component, record.message);
    if (existing != error_log_.end()) {
        // Update existing record
        existing->count++;
        existing->timestamp = record.timestamp;
        return;
    }
    
    // Add new record
    error_log_.push_back(record);
    
    // Maintain maximum size
    while (error_log_.size() > max_error_records_) {
        error_log_.pop_front();
    }
}

std::deque<SimpleErrorHandler::ErrorRecord>::iterator 
SimpleErrorHandler::FindExistingError(const std::string& component, const std::string& message) {
    // Search recent errors (last 20) for duplicates
    size_t search_count = std::min(size_t(20), error_log_.size());
    auto start_it = error_log_.end() - search_count;
    
    return std::find_if(start_it, error_log_.end(),
                       [&component, &message](const ErrorRecord& record) {
                           return record.component == component && record.message == message;
                       });
}