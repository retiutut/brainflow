import argparse
import time
import numpy as np
from brainflow.board_shim import BoardShim, BrainFlowInputParams, BoardIds
from brainflow.data_filter import DataFilter, WindowOperations, DetrendOperations

channel_index = 0
data_stream_time = 5
channel_chars = ["1", "2", "3", "4", "5", "6", "7", "8", "Q", "W", "E", "R", "T", "Y", "U", "I"]

def main():
    BoardShim.enable_dev_board_logger()
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial-port', type=str, help='serial port', required=True)
    args = parser.parse_args()
    params = BrainFlowInputParams()
    params.serial_port = args.serial_port
    board_id = BoardIds.CYTON_DAISY_BOARD
    board_descr = BoardShim.get_board_descr(board_id)
    sampling_rate = BoardShim.get_sampling_rate(board_id)
    eeg_channels = BoardShim.get_eeg_channels(board_id)
    board = BoardShim(board_id, params)
    board.prepare_session()
    config_string = "x1060000X"
    board.config_board(config_string)
    time.sleep(1)
    board.start_stream()
    time.sleep(data_stream_time)
    data_old = board.get_board_data()
    board.stop_stream()
    board.release_session()
    board.prepare_session()
    config_string = "x1000000X"
    board.config_board(config_string)
    time.sleep(1)
    board.start_stream()
    time.sleep(data_stream_time)
    data_new = board.get_board_data()
    board.stop_stream()
    board.release_session()
    print(np.mean(data_old[eeg_channels[channel_index]][100:]))
    print(np.mean(data_new[eeg_channels[channel_index]][100:]))
    DataFilter.write_file(data_old, 'test_old.csv', 'w')  # use 'a' for append mode
    DataFilter.write_file(data_new, 'test_new.csv', 'w')  # use 'a' for append mode
    std_dev_old = DataFilter.calc_stddev(data_old[eeg_channels[channel_index]])
    std_dev_new = DataFilter.calc_stddev(data_new[eeg_channels[channel_index]])
    print(std_dev_old)
    print(std_dev_new)
if __name__ == "__main__":
    main()