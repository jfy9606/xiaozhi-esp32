#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <list>
#include <vector>
#include <deque>
#include <condition_variable>
#include <memory>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "protocol.h"
#include "ota.h"
#include "background_task.h"
#include "audio_processor.h"
#include "components.h"
#include "wake_word.h"
#include "audio_debugger.h"

// 只有在电机控制器启用时才包含相关头文件
#if defined(CONFIG_ENABLE_MOTOR_CONTROLLER)
#include "vehicle/vehicle.h"
#endif

// 只有在Web服务器启用时才包含相关头文件
#if defined(CONFIG_ENABLE_WEB_SERVER)
#include "web/web.h"
#endif

#define SCHEDULE_EVENT (1 << 0)
#define SEND_AUDIO_EVENT (1 << 1)
#define CHECK_NEW_VERSION_DONE_EVENT (1 << 2)

enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError
};

#define OPUS_FRAME_DURATION_MS 60
#define MAX_AUDIO_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return voice_detected_; }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void UpdateIotStates();
    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    void PlaySound(const std::string_view& sound);
    bool CanEnterSleepMode();
    
    // Component management methods
    void InitializeComponents();
    void StartComponents();
    void StopComponents();
    Component* GetComponent(const char* name);
    void SendMcpMessage(const std::string& payload);
    bool InitComponents();
    void SetAecMode(AecMode mode);
    bool ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples);
    AecMode GetAecMode() const { return aec_mode_; }
    BackgroundTask* GetBackgroundTask() const { return background_task_; }
    void InitVehicleComponent(Web* web_server);

private:
    Application();
    ~Application();

    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool voice_detected_ = false;
    bool busy_decoding_audio_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    BackgroundTask* background_task_ = nullptr;
    std::chrono::steady_clock::time_point last_output_time_;
    std::list<AudioStreamPacket> audio_send_queue_;
    std::list<AudioStreamPacket> audio_decode_queue_;
    std::condition_variable audio_decode_cv_;
    std::list<AudioStreamPacket> audio_testing_queue_;

    // 用于维护音频包的timestamp队列
    std::deque<uint32_t> timestamp_queue_;
    std::mutex timestamp_mutex_;

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckNewVersion(Ota& ota);
    void ShowActivationCode(const std::string& code, const std::string& message);
    void OnClockTimer();
    void SetListeningMode(ListeningMode mode);
    void AudioLoop();

#ifdef CONFIG_ENABLE_LOCATION_CONTROLLER
    // 初始化位置控制器
    void InitLocationController();
#endif

    void CleanupComponents();
    void EnterAudioTestingMode();
    void ExitAudioTestingMode();
};

#endif // _APPLICATION_H_
