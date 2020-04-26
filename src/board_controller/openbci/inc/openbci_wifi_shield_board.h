#pragma once

#include <thread>

#include "board.h"
#include "board_controller.h"
#include "socket_client.h"
#include "socket_server.h"


struct http_t; // forward declaration cause include "http.h" here leads to strange errors

class OpenBCIWifiShieldBoard : public Board
{

private:
    int http_timeout;
    int wait_for_http_resp (http_t *request);

protected:
    volatile bool keep_alive;
    bool initialized;
    std::thread streaming_thread;

    SocketServer *server_socket;
    int num_channels;

    virtual void read_thread () = 0;

public:
    OpenBCIWifiShieldBoard (int num_channels, struct BrainFlowInputParams params, int board_id);
    virtual ~OpenBCIWifiShieldBoard ();

    int prepare_session ();
    int start_stream (int buffer_size, char *streamer_params);
    int stop_stream ();
    int release_session ();
    int config_board (char *config);

    static constexpr int package_size = 33;
    static constexpr int num_packages_per_transaction =
        6; // should be even for correct parsing in daisy
    static constexpr int transaction_size =
        OpenBCIWifiShieldBoard::package_size * OpenBCIWifiShieldBoard::num_packages_per_transaction;
};
