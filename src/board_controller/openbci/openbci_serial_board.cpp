#include <string.h>

#include "openbci_helpers.h"
#include "openbci_serial_board.h"
#include "serial.h"

OpenBCISerialBoard::OpenBCISerialBoard (
    int num_channels, struct BrainFlowInputParams params, int board_id)
    : Board (board_id, params)
{
    this->num_channels = num_channels;
    serial = NULL;
    is_streaming = false;
    keep_alive = false;
    initialized = false;
}

OpenBCISerialBoard::~OpenBCISerialBoard ()
{
    skip_logs = true;
    release_session ();
}

int OpenBCISerialBoard::open_port ()
{
    if (serial->is_port_open ())
    {
        safe_logger (spdlog::level::err, "port {} already open", serial->get_port_name ());
        return PORT_ALREADY_OPEN_ERROR;
    }

    Board::board_logger->info ("openning port {}", serial->get_port_name ());
    int res = serial->open_serial_port ();
    if (res < 0)
    {
        return UNABLE_TO_OPEN_PORT_ERROR;
    }
    safe_logger (spdlog::level::trace, "port {} is open", serial->get_port_name ());
    return STATUS_OK;
}

int OpenBCISerialBoard::config_board (char *config)
{
    if (!initialized)
    {
        return BOARD_NOT_READY_ERROR;
    }
    int res = validate_config (config);
    if (res != STATUS_OK)
    {
        return res;
    }
    return send_to_board (config);
}

int OpenBCISerialBoard::send_to_board (char *msg)
{
    int lenght = strlen (msg);
    safe_logger (spdlog::level::debug, "sending {} to the board", msg);
    int res = serial->send_to_serial_port ((const void *)msg, lenght);
    if (res != lenght)
    {
        return BOARD_WRITE_ERROR;
    }

    return STATUS_OK;
}

int OpenBCISerialBoard::set_port_settings ()
{
    int res = serial->set_serial_port_settings ();
    if (res < 0)
    {
        safe_logger (spdlog::level::err, "Unable to set port settings, res is {}", res);
        return SET_PORT_ERROR;
    }
    safe_logger (spdlog::level::trace, "set port settings");
    return send_to_board ("v");
}

int OpenBCISerialBoard::status_check ()
{
    Board::board_logger->trace ("start status check");
    unsigned char buf[1];
    int count = 0;
    int max_empty_seq = 5;
    int num_empty_attempts = 0;

    for (int i = 0; i < 500; i++)
    {
        int res = serial->read_from_serial_port (buf, 1);
        if (res > 0)
        {
            num_empty_attempts = 0;
            // board is ready if there are '$$$'
            if (buf[0] == '$')
            {
                count++;
            }
            else
            {
                count = 0;
            }
            if (count == 3)
            {
                return STATUS_OK;
            }
        }
        else
        {
            num_empty_attempts++;
            if (num_empty_attempts > max_empty_seq)
            {
                safe_logger (spdlog::level::err, "board doesnt send welcome characters!");
                return BOARD_NOT_READY_ERROR;
            }
        }
    }
    return BOARD_NOT_READY_ERROR;
}

int OpenBCISerialBoard::prepare_session ()
{
    if (initialized)
    {
        safe_logger (spdlog::level::info, "Session already prepared");
        return STATUS_OK;
    }
    if (params.serial_port.empty ())
    {
        safe_logger (spdlog::level::err, "serial port is empty");
        return INVALID_ARGUMENTS_ERROR;
    }
    serial = new Serial (params.serial_port.c_str ());
    int port_open = open_port ();
    if (port_open != STATUS_OK)
    {
        return port_open;
    }

    int set_settings = set_port_settings ();
    if (set_settings != STATUS_OK)
    {
        return set_settings;
    }

    int initted = status_check ();
    if (initted != STATUS_OK)
    {
        return initted;
    }

    initialized = true;
    Board::board_logger->trace ("Session is ready");
    return STATUS_OK;
}

int OpenBCISerialBoard::start_stream (int buffer_size, char *streamer_params)
{
    if (is_streaming)
    {
        Board::board_logger->error ("Streaming thread already running");
        return STREAM_ALREADY_RUN_ERROR;
    }
    if (buffer_size <= 0 || buffer_size > MAX_CAPTURE_SAMPLES)
    {
        Board::board_logger->error ("invalid array size");
        return INVALID_BUFFER_SIZE_ERROR;
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
    if (res != STATUS_OK)
    {
        return res;
    }
    db = new DataBuffer (num_channels, buffer_size);
    if (!db->is_ready ())
    {
        safe_logger (spdlog::level::err, "unable to prepare buffer");
        delete db;
        db = NULL;
        return INVALID_BUFFER_SIZE_ERROR;
    }

    // start streaming
    int send_res = send_to_board ("b");
    if (send_res != STATUS_OK)
    {
        return send_res;
    }
    keep_alive = true;
    streaming_thread = std::thread ([this] { this->read_thread (); });
    is_streaming = true;
    return STATUS_OK;
}

int OpenBCISerialBoard::stop_stream ()
{
    if (is_streaming)
    {
        keep_alive = false;
        is_streaming = false;
        if (streaming_thread.joinable ())
        {
            streaming_thread.join ();
        }
        if (streamer)
        {
            delete streamer;
            streamer = NULL;
        }
        return send_to_board ("s");
    }
    else
        return STREAM_THREAD_IS_NOT_RUNNING;
}

int OpenBCISerialBoard::release_session ()
{
    if (initialized)
    {
        if (is_streaming)
        {
            stop_stream ();
        }
        initialized = false;
    }
    if (serial)
    {
        serial->close_serial_port ();
        delete serial;
        serial = NULL;
    }
    return STATUS_OK;
}
