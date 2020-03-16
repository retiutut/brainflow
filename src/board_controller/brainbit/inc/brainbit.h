#pragma once

#include <thread>

#include "board.h"
#include "board_controller.h"

#ifdef _WIN32
#include "c_eeg_channels.h"
#include "cdevice.h"
#endif

class BrainBit : public Board
{

private:
    // as for now BrainBit supports only windows, to dont write ifdef in board_controller.cpp and
    // dont change CmakeLists.txt add dummy implementation for Unix
#ifdef _WIN32
    volatile bool keep_alive;
    bool initialized;
    bool is_streaming;
    std::thread streaming_thread;
    Device *device;
    // store data from callbacks as object fields and add it to eeg data
    volatile int last_battery;
    volatile double last_resistance_t3;
    volatile double last_resistance_t4;
    volatile double last_resistance_o1;
    volatile double last_resistance_o2;

    void read_thread ();

    int find_device (long long serial_number);
    int find_device_info (
        DeviceEnumerator *enumerator, uint64_t serial_number, DeviceInfo *out_device_info);
    int connect_device ();
    void free_listeners ();
    void free_device ();
    void free_channels ();

    ListenerHandle battery_listener;
    ListenerHandle resistance_listener_t3;
    ListenerHandle resistance_listener_t4;
    ListenerHandle resistance_listener_o1;
    ListenerHandle resistance_listener_o2;
    EegDoubleChannel *signal_t4;
    EegDoubleChannel *signal_t3;
    EegDoubleChannel *signal_o1;
    EegDoubleChannel *signal_o2;

#endif

public:
    BrainBit (struct BrainFlowInputParams params);
    ~BrainBit ();

    int prepare_session ();
    int start_stream (int buffer_size, char *streamer_params);
    int stop_stream ();
    int release_session ();
    int config_board (char *config);

#ifdef _WIN32
    // callbacks must be public since they are called from plain C callbacks
    void on_battery_charge_received (
        Device *device, ChannelInfo channel_info, IntDataArray battery_data);
    void on_resistance_received (
        Device *device, ChannelInfo channel_info, DoubleDataArray data_array);

    static constexpr int package_size = 10;

#endif
};
