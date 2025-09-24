#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include "esp_err.h"

/**
 * @brief Simple error handler for hardware management system
 * 
 * This class provides basic error logging, storage, and recovery mechanisms
 * for the hardware management system.
 */
class SimpleErrorHandler {
public:
    /**
     * @brief Error severity levels
     */
    enum class ErrorLevel {
        INFO,       ///< Informational messages
        WARNING,    ///< Warning messages that don't affect functionality
        ERROR,      ///< Error messages that affect functionality
        CRITICAL    ///< Critical errors that may cause system instability
    };

    /**
     * @brief Error record structure
     */
    struct ErrorRecord {
        ErrorLevel level;
        std::string component;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        uint32_t count;  ///< Number of times this error occurred
    };

    /**
     * @brief Log an error with specified level
     * @param level Error severity level
     * @param component Component name where error occurred
     * @param message Error message
     */
    static void LogError(ErrorLevel level, const std::string& component, 
                        const std::string& message);

    /**
     * @brief Log an info message
     * @param component Component name
     * @param message Info message
     */
    static void LogInfo(const std::string& component, const std::string& message);

    /**
     * @brief Log a warning message
     * @param component Component name
     * @param message Warning message
     */
    static void LogWarning(const std::string& component, const std::string& message);

    /**
     * @brief Log a critical error
     * @param component Component name
     * @param message Critical error message
     */
    static void LogCritical(const std::string& component, const std::string& message);

    /**
     * @brief Get recent error records
     * @param max_count Maximum number of records to return (0 = all)
     * @return Vector of error records
     */
    static std::vector<ErrorRecord> GetRecentErrors(size_t max_count = 50);

    /**
     * @brief Get error records for a specific component
     * @param component Component name
     * @param max_count Maximum number of records to return (0 = all)
     * @return Vector of error records
     */
    static std::vector<ErrorRecord> GetComponentErrors(const std::string& component, 
                                                      size_t max_count = 20);

    /**
     * @brief Get error count by level
     * @param level Error level to count
     * @return Number of errors at specified level
     */
    static size_t GetErrorCount(ErrorLevel level);

    /**
     * @brief Get total error count
     * @return Total number of errors logged
     */
    static size_t GetTotalErrorCount();

    /**
     * @brief Clear all error records
     */
    static void ClearErrors();

    /**
     * @brief Clear errors for a specific component
     * @param component Component name
     */
    static void ClearComponentErrors(const std::string& component);

    /**
     * @brief Check if system has critical errors
     * @return true if critical errors exist, false otherwise
     */
    static bool HasCriticalErrors();

    /**
     * @brief Get error summary as JSON string
     * @return JSON string containing error summary
     */
    static std::string GetErrorSummaryJson();

    /**
     * @brief Get error recovery suggestions
     * @param component Component name
     * @return Vector of recovery suggestions
     */
    static std::vector<std::string> GetRecoverySuggestions(const std::string& component);

    /**
     * @brief Convert error level to string
     * @param level Error level
     * @return String representation of error level
     */
    static std::string ErrorLevelToString(ErrorLevel level);

    /**
     * @brief Set maximum number of error records to keep
     * @param max_records Maximum number of records
     */
    static void SetMaxErrorRecords(size_t max_records);

private:
    static std::deque<ErrorRecord> error_log_;
    static std::mutex error_mutex_;
    static size_t max_error_records_;
    
    /**
     * @brief Add error record to log
     * @param record Error record to add
     */
    static void AddErrorRecord(const ErrorRecord& record);

    /**
     * @brief Find existing error record for deduplication
     * @param component Component name
     * @param message Error message
     * @return Iterator to existing record or end()
     */
    static std::deque<ErrorRecord>::iterator FindExistingError(
        const std::string& component, const std::string& message);
};