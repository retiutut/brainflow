#include <string>

#include "board.h"
#include "board_controller.h"
#include "file_streamer.h"
#include "multicast_streamer.h"
#include "stub_streamer.h"

#include "spdlog/sinks/null_sink.h"

#define LOGGER_NAME "brainflow_logger"

#ifdef __ANDROID__
#include "spdlog/sinks/android_sink.h"
std::shared_ptr<spdlog::logger> Board::board_logger =
    spdlog::android_logger (LOGGER_NAME, "brainflow_ndk_logger");
#else
std::shared_ptr<spdlog::logger> Board::board_logger = spdlog::stderr_logger_mt (LOGGER_NAME);
#endif

int Board::set_log_level (int level)
{
    int log_level = level;
    if (level > 6)
    {
        log_level = 6;
    }
    if (level < 0)
    {
        log_level = 0;
    }
    try
    {
        Board::board_logger->set_level (spdlog::level::level_enum (log_level));
        Board::board_logger->flush_on (spdlog::level::level_enum (log_level));
    }
    catch (...)
    {
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int Board::set_log_file (char *log_file)
{
#ifdef __ANDROID__
    Board::board_logger->error ("For Android set_log_file is unavailable");
    return (int)BrainFlowExitCodes::GENERAL_ERROR;
#else
    try
    {
        spdlog::level::level_enum level = Board::board_logger->level ();
        Board::board_logger = spdlog::create<spdlog::sinks::null_sink_st> (
            "null_logger"); // to dont set logger to nullptr and avoid race condition
        spdlog::drop (LOGGER_NAME);
        Board::board_logger = spdlog::basic_logger_mt (LOGGER_NAME, log_file);
        Board::board_logger->set_level (level);
        Board::board_logger->flush_on (level);
        spdlog::drop ("null_logger");
    }
    catch (...)
    {
        return (int)BrainFlowExitCodes::GENERAL_ERROR;
    }
    return (int)BrainFlowExitCodes::STATUS_OK;
#endif
}

int Board::prepare_streamer (char *streamer_params)
{
    // to dont write smth like if (streamer) every time for all boards create dummy streamer which
    // does nothing and return an instance of this streamer if user dont specify streamer_params
    if (streamer_params == NULL)
    {
        safe_logger (spdlog::level::debug, "use stub streamer");
        streamer = new StubStreamer ();
    }
    else if (streamer_params[0] == '\0')
    {
        safe_logger (spdlog::level::debug, "use stub streamer");
        streamer = new StubStreamer ();
    }
    else
    {
        // parse string, sscanf doesnt work
        std::string streamer_params_str (streamer_params);
        size_t idx1 = streamer_params_str.find ("://");
        if (idx1 == std::string::npos)
        {
            safe_logger (
                spdlog::level::err, "format is streamer_type://streamer_dest:streamer_args");
            return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
        }
        std::string streamer_type = streamer_params_str.substr (0, idx1);
        size_t idx2 = streamer_params_str.find_last_of (":", std::string::npos);
        if ((idx2 == std::string::npos) || (idx1 == idx2))
        {
            safe_logger (
                spdlog::level::err, "format is streamer_type://streamer_dest:streamer_args");
            return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
        }
        std::string streamer_dest = streamer_params_str.substr (idx1 + 3, idx2 - idx1 - 3);
        std::string streamer_mods = streamer_params_str.substr (idx2 + 1);

        if (streamer_type == "file")
        {
            safe_logger (spdlog::level::trace, "File Streamer, file: {}, mods: {}",
                streamer_dest.c_str (), streamer_mods.c_str ());
            streamer = new FileStreamer (streamer_dest.c_str (), streamer_mods.c_str ());
        }
        if (streamer_type == "streaming_board")
        {
            int port = 0;
            try
            {
                port = std::stoi (streamer_mods);
            }
            catch (const std::exception &e)
            {
                safe_logger (spdlog::level::err, e.what ());
                return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
            }
            streamer = new MultiCastStreamer (streamer_dest.c_str (), port);
        }

        if (streamer == NULL)
        {
            safe_logger (
                spdlog::level::err, "unsupported streamer type {}", streamer_type.c_str ());
            return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
        }
    }

    int res = streamer->init_streamer ();
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        safe_logger (spdlog::level::err, "failed to init streamer");
        delete streamer;
        streamer = NULL;
    }

    return res;
}

int Board::get_current_board_data (int num_samples, double *data_buf, int *returned_samples)
{
    if (db && data_buf && returned_samples)
    {
        int num_data_channels = -1;
        int res = get_num_rows (board_id, &num_data_channels);
        if (res != (int)BrainFlowExitCodes::STATUS_OK)
        {
            return res;
        }
        num_data_channels--; // columns_size includes timestamp channel, which is a separated field
                             // in DataBuffer class

        double *buf = new double[num_samples * num_data_channels];
        double *ts_buf = new double[num_samples];
        int num_data_points = (int)db->get_current_data (num_samples, ts_buf, buf);
        reshape_data (num_data_points, buf, ts_buf, data_buf);
        delete[] buf;
        delete[] ts_buf;
        *returned_samples = num_data_points;
        return (int)BrainFlowExitCodes::STATUS_OK;
    }
    else
    {
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }
}

int Board::get_board_data_count (int *result)
{
    if (!db)
    {
        return (int)BrainFlowExitCodes::EMPTY_BUFFER_ERROR;
    }
    if (!result)
    {
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }

    *result = (int)db->get_data_count ();
    return (int)BrainFlowExitCodes::STATUS_OK;
}

int Board::get_board_data (int data_count, double *data_buf)
{
    if (!db)
    {
        return (int)BrainFlowExitCodes::EMPTY_BUFFER_ERROR;
    }
    if (!data_buf)
    {
        return (int)BrainFlowExitCodes::INVALID_ARGUMENTS_ERROR;
    }
    int num_data_channels = 0;
    int res = get_num_rows (board_id, &num_data_channels);
    if (res != (int)BrainFlowExitCodes::STATUS_OK)
    {
        return res;
    }
    num_data_channels--; // columns_size includes timestamp channel, which is a separated field
                         // in DataBuffer class

    double *buf = new double[data_count * num_data_channels];
    double *ts_buf = new double[data_count];
    int num_data_points = (int)db->get_data (data_count, ts_buf, buf);
    reshape_data (num_data_points, buf, ts_buf, data_buf);
    delete[] buf;
    delete[] ts_buf;
    return (int)BrainFlowExitCodes::STATUS_OK;
}

void Board::reshape_data (
    int data_count, const double *buf, const double *ts_buf, double *output_buf)
{
    int num_data_channels = 0;
    get_num_rows (board_id, &num_data_channels); // here we know that board id is valid
    num_data_channels--;                         // -1 because of timestamp

    for (int i = 0; i < data_count; i++)
    {
        for (int j = 0; j < num_data_channels; j++)
        {
            output_buf[j * data_count + i] = buf[i * num_data_channels + j];
        }
    }
    // add timestamp to resulting data table
    for (int i = 0; i < data_count; i++)
    {
        output_buf[num_data_channels * data_count + i] = ts_buf[i];
    }
}
