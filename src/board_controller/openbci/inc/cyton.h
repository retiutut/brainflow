#pragma once

#include <math.h>

#include "openbci_serial_board.h"

#define ADS1299_Vref 4.5
#define ADS1299_gain 24.0

class Cyton : public OpenBCISerialBoard
{
    float eeg_scale = ADS1299_Vref / float((pow (2, 23) - 1)) / ADS1299_gain * 1000000.;
    float accel_scale = 0.002 / (pow (2, 4));

protected:
    void read_thread ();

public:
    Cyton (struct BrainFlowInputParams params)
        : OpenBCISerialBoard (22, params, (int)BoardIds::CYTON_BOARD)
    {
    }
};
