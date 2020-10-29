#include <chrono>
#include <stdint.h>
#include <string.h>

#include "auraxr.h"
#include "custom_cast.h"
#include "timestamp.h"

#ifndef _WIN32
#include <errno.h>
#endif

constexpr int AuraXR::num_channels;
constexpr int AuraXR::package_size;
constexpr int AuraXR::num_packages;
constexpr int AuraXR::transaction_size;


AuraXR::AuraXR (struct BrainFlowInputParams params) : Board ((int)BoardIds::AURAXR_BOARD, params)
{
    this->socket = NULL;
    this->is_streaming = false;
    this->keep_alive = false;
    this->initialized = false;
    this->start_time = 0.0;
    this->state = (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
}

AuraXR::~AuraXR ()
{
    skip_logs = true;
    release_session ();
}

int AuraXR::prepare_session ()
{
    if (initialized)
    {
        safe_logger (spdlog::level::info, "Session is already prepared");
        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    if (params.ip_address.empty ())
    {
        safe_logger (spdlog::level::info, "use default IP address 192.168.4.1");
        params.ip_address = "192.168.4.1";
    }
    if (params.ip_protocol == (int)IpProtocolType::TCP)
    {
        safe_logger (spdlog::level::err, "ip protocol is UDP for novaxr");
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }
    socket = new SocketClientUDP (params.ip_address.c_str (), 2390);
    int res = socket->connect ();
    if (res != (int)SocketClientUDPReturnCodes::STATUS_OK)
    {
        safe_logger (spdlog::level::err, "failed to init socket: {}", res);
        delete socket;
        socket = NULL;
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    // force default settings for device
    std::string tmp;
    std::string default_settings = "d";
    res = config_board (default_settings, tmp);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        safe_logger (spdlog::level::err, "failed to apply default settings");
        delete socket;
        socket = NULL;
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    // force default sampling rate - 250
    std::string sampl_rate = "~6";
    res = config_board (sampl_rate, tmp);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        safe_logger (spdlog::level::err, "failed to apply defaul sampling rate");
        delete socket;
        socket = NULL;
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    initialized = true;
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int AuraXR::config_board (std::string conf, std::string &response)
{
    if (socket == NULL)
    {
        safe_logger (spdlog::level::err, "You need to call prepare_session before config_board");
        return (int)BrainFlowExitCodes::BOARD_NOT_CREATED_ERROR;
    }
    const char *config = conf.c_str ();
    safe_logger (spdlog::level::debug, "Trying to config AuraXR with {}", config);
    int len = strlen (config);
    int res = socket->send (config, len);
    if (len != res)
    {
        if (res == -1)
        {
#ifdef _WIN32
            safe_logger (spdlog::level::err, "WSAGetLastError is {}", WSAGetLastError ());
#else
            safe_logger (spdlog::level::err, "errno {} message {}", errno, strerror (errno));
#endif
        }
        safe_logger (spdlog::level::err, "Failed to config a board");
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    if (!is_streaming)
    {
        constexpr int max_string_size = 8192;
        char b[max_string_size];
        res = AuraXR::transaction_size;
        while (res == AuraXR::transaction_size)
        {
            res = socket->recv (b, max_string_size);
            if (res == -1)
            {
#ifdef _WIN32
                safe_logger (
                    spdlog::level::err, "config_board WSAGetLastError is {}", WSAGetLastError ());
#else
                safe_logger (spdlog::level::err, "config_board errno {} message {}", errno,
                    strerror (errno));
#endif
                return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
            }
        }
        // set response string
        for (int i = 0; i < res; i++)
        {
            response = response + b[i];
        }
        switch (b[0])
        {
            case 'A':
                return (int)BrainFlowExitCodes::STATUS_OK;
            case 'I':
                safe_logger (spdlog::level::err, "invalid command");
                return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
            default:
                safe_logger (spdlog::level::warn, "unknown char received: {}", b[0]);
                return (int)BrainFlowExitCodes::STATUS_OK;
        }
    }

    return (int)BrainFlowExitCodes::STATUS_OK;
}

int AuraXR::start_stream (int buffer_size, char *streamer_params)
{
    if (!initialized)
    {
        safe_logger (spdlog::level::err, "You need to call prepare_session before config_board");
        return (int)BrainFlowExitCodes::BOARD_NOT_CREATED_ERROR;
    }
    if (is_streaming)
    {
        safe_logger (spdlog::level::err, "Streaming thread already running");
        return (int)BrainFlowExitCodes::STREAM_ALREADY_RUN_ERROR;
    }
    if (buffer_size <= 0 || buffer_size > MAX_CAPTURE_SAMPLES)
    {
        safe_logger (spdlog::level::err, "invalid array size");
        return (int)BrainFlowExitCodes::INVALID_BUFFER_SIZE_ERROR;
    }

    if (db)
    {
        delete db;
        db = NULL;
    }
    if (streamer)
    {
        delete streamer;
        streamer = NULL;
    }

    int res = prepare_streamer (streamer_params);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return res;
    }
    db = new DataBuffer (AuraXR::num_channels, buffer_size);
    if (!db->is_ready ())
    {
        safe_logger (spdlog::level::err, "unable to prepare buffer");
        delete db;
        db = NULL;
        return (int)BrainFlowExitCodes::INVALID_BUFFER_SIZE_ERROR;
    }

    // start streaming
    res = socket->send ("b", 1);
    if (res != 1)
    {
        if (res == -1)
        {
#ifdef _WIN32
            safe_logger (spdlog::level::err, "WSAGetLastError is {}", WSAGetLastError ());
#else
            safe_logger (spdlog::level::err, "errno {} message {}", errno, strerror (errno));
#endif
        }
        safe_logger (spdlog::level::err, "Failed to send a command to board");
        return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
    }
    start_time = get_timestamp ();

    keep_alive = true;
    streaming_thread = std::thread ([this] { this->read_thread (); });
    // wait for data to ensure that everything is okay
    std::unique_lock<std::mutex> lk (this->m);
    auto sec = std::chrono::seconds (1);
    if (cv.wait_for (lk, 5 * sec,
            [this] { return this->state != (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR; }))
    {
        this->is_streaming = true;
        return this->state;
    }
    else
    {
        safe_logger (spdlog::level::err, "no data received in 5sec, stopping thread");
        this->is_streaming = true;
        this->stop_stream ();
        return (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
    }
}

int AuraXR::stop_stream ()
{
    if (is_streaming)
    {
        keep_alive = false;
        is_streaming = false;
        streaming_thread.join ();
        if (streamer)
        {
            delete streamer;
            streamer = NULL;
        }
        this->state = (int)BrainFlowExitCodes::SYNC_TIMEOUT_ERROR;
        int res = socket->send ("s", 1);
        if (res != 1)
        {
            if (res == -1)
            {
#ifdef _WIN32
                safe_logger (spdlog::level::err, "WSAGetLastError is {}", WSAGetLastError ());
#else
                safe_logger (spdlog::level::err, "errno {} message {}", errno, strerror (errno));
#endif
            }
            safe_logger (spdlog::level::err, "Failed to send a command to board");
            return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
        }

        // free kernel buffer
        socket->set_timeout (2);
        unsigned char b[AuraXR::transaction_size];
        res = 0;
        int max_attempt = 25; // to dont get to infinite loop
        int current_attempt = 0;
        while (res != -1)
        {
            res = socket->recv (b, AuraXR::transaction_size);
            current_attempt++;
            if (current_attempt == max_attempt)
            {
                safe_logger (
                    spdlog::level::err, "Command 's' was sent but streaming is still running.");
                socket->set_timeout (5);
                return (int)BrainFlowExitCodes::BOARD_WRITE_ERROR;
            }
        }
        socket->set_timeout (5);

        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    else
    {
        return (int)BrainFlowExitCodes::STREAM_THREAD_IS_NOT_RUNNING;
    }
}

int AuraXR::release_session ()
{
    if (initialized)
    {
        if (is_streaming)
        {
            stop_stream ();
        }
        initialized = false;
        if (socket)
        {
            socket->close ();
            delete socket;
            socket = NULL;
        }
    }
    return (int)BrainFlowExitCodes::STATUS_OK;
}

void AuraXR::read_thread ()
{
    int res;
    unsigned char b[AuraXR::transaction_size];
    for (int i = 0; i < AuraXR::transaction_size; i++)
    {
        b[i] = 0;
    }
    while (keep_alive)
    {
        res = socket->recv (b, AuraXR::transaction_size);
        if (res == -1)
        {
#ifdef _WIN32
            safe_logger (spdlog::level::err, "WSAGetLastError is {}", WSAGetLastError ());
#else
            safe_logger (spdlog::level::err, "errno {} message {}", errno, strerror (errno));
#endif
        }
        if (res != AuraXR::transaction_size)
        {
            safe_logger (spdlog::level::trace, "unable to read {} bytes, read {}",
                AuraXR::transaction_size, res);
            continue;
        }
        else
        {
            socket->send ("a", sizeof (char)); // send ack that data received
            // inform main thread that everything is ok and first package was received
            if (this->state != (int)BrainFlowExitCodes::STATUS_OK)
            {
                safe_logger (spdlog::level::info,
                    "received first package with {} bytes streaming is started", res);
                {
                    std::lock_guard<std::mutex> lk (this->m);
                    this->state = (int)BrainFlowExitCodes::STATUS_OK;
                }
                this->cv.notify_one ();
                safe_logger (spdlog::level::debug, "start streaming");
            }
        }

        for (int cur_package = 0; cur_package < AuraXR::num_packages; cur_package++)
        {
            double package[AuraXR::num_channels] = {0.};
            int offset = cur_package * package_size;
            // package num
            package[0] = (double)b[0 + offset];
            // eeg and emg
            for (int i = 4, tmp_counter = 0; i < 20; i++, tmp_counter++)
            {
                // put them directly after package num in brainflow
                if (tmp_counter < 8)
                    package[i - 3] = eeg_scale_main_board *
                        (double)cast_24bit_to_int32 (b + offset + 5 + 3 * (i - 4));
                else if ((tmp_counter == 9) || (tmp_counter == 14))
                    package[i - 3] = eeg_scale_sister_board *
                        (double)cast_24bit_to_int32 (b + offset + 5 + 3 * (i - 4));
                else
                    package[i - 3] =
                        emg_scale * (double)cast_24bit_to_int32 (b + offset + 5 + 3 * (i - 4));
            }
            uint16_t temperature;
            int32_t ppg_ir;
            int32_t ppg_red;
            float eda;
            memcpy (&temperature, b + 54 + offset, 2);
            memcpy (&eda, b + 1 + offset, 4);
            memcpy (&ppg_red, b + 56 + offset, 4);
            memcpy (&ppg_ir, b + 60 + offset, 4);
            // ppg
            package[17] = (double)ppg_red;
            package[18] = (double)ppg_ir;
            // eda
            package[19] = (double)eda;
            // temperature
            package[20] = temperature / 100.0;
            // battery
            package[21] = (double)b[53 + offset];

            double timestamp_device;
            memcpy (&timestamp_device, b + 64 + offset, 8);
            timestamp_device /= 1e6; // convert usec to sec
            double timestamp = timestamp_device + start_time;
            streamer->stream_data (package, AuraXR::num_channels, timestamp);
            db->add_data (timestamp, package);
        }
    }
}
