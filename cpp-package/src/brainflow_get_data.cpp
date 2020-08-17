#include <fstream>
#include <iostream>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "board_controller.h"
#include "board_shim.h"
#include "data_handler.h"

using namespace std;

void check_error (int ec)
{
    if (ec != STATUS_OK)
    {
        cout << "exit code is " << ec << endl;
        exit (ec);
    }
}

void write_csv (const char *filename, double **data_buf, int data_count, int total_channels)
{
    ofstream output_file;
    output_file.open (filename);
    for (int i = 0; i < data_count; i++)
    {
        for (int j = 0; j < total_channels; j++)
        {
            output_file << data_buf[i][j] << ",";
        }
        output_file << endl;
    }
    output_file.close ();
}

int main (int argc, char *argv[])
{
    if (argc != 2)
    {
        cout << "port name is required" << endl;
        return -1;
    }

    BoardShim *cyton = new BoardShim (CYTON_BOARD, argv[1]);
    DataHandler *dh = new DataHandler (CYTON_BOARD);
    int buffer_size = 250 * 60;
    double **data_buf = (double **)malloc (sizeof (double *) * buffer_size);
    for (int i = 0; i < buffer_size; i++)
    {
        data_buf[i] = (double *)malloc (sizeof (double) * cyton->total_channels);
    }
    int res = (int)BrainFlowExitCodes::STATUS_OK;
    int data_count;

    res = cyton->prepare_session ();
    check_error (res);
    res = cyton->start_stream (buffer_size);
    check_error (res);

#ifdef _WIN32
    Sleep (5000);
#else
    sleep (5);
#endif

    res = cyton->stop_stream ();
    check_error (res);
    res = cyton->get_board_data_count (&data_count);
    check_error (res);
    res = cyton->get_board_data (data_count, data_buf);
    check_error (res);
    res = cyton->release_session ();
    check_error (res);
    dh->preprocess_data (data_buf, data_count);

    write_csv ("board_data.csv", data_buf, data_count, cyton->total_channels);

    for (int i = 0; i < buffer_size; i++)
        free (data_buf[i]);
    free (data_buf);

    delete dh;
    delete cyton;

    return 0;
}