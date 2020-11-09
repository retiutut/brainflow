import glob
import argparse
import statistics
import os
import time
import pickle
import copy

import numpy as np
from sklearn import metrics
from sklearn.linear_model import LogisticRegression
from sklearn.dummy import DummyClassifier
from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
from metric_learn import LMNN
from sklearn.neighbors import KNeighborsClassifier
from sklearn.decomposition import PCA
from sklearn.model_selection import train_test_split, cross_val_score

import brainflow
from brainflow.board_shim import BoardShim, BrainFlowInputParams, LogLevels, BoardIds
from brainflow.data_filter import DataFilter, FilterTypes, AggOperations, WindowFunctions, DetrendOperations
from brainflow.ml_model import BrainFlowMetrics, BrainFlowClassifiers, MLModel, BrainFlowModelParams

from svm_classifier import train_brainflow_search_svm, train_brainflow_svm
from store_model import write_model, write_knn_model


def prepare_data ():
    # use different windows, its kinda data augmentation
    window_sizes = [4.0, 6.0, 8.0, 10.0]
    overlaps = [0.5, 0.45, 0.4, 0.35] # percentage of window_size
    dataset_x = list ()
    dataset_y = list ()
    for data_type in ('relaxed', 'focused'):
        for file in glob.glob (os.path.join ('data', data_type, '*', '*.csv')):
            print (file)
            board_id = os.path.basename (os.path.dirname (file))
            try:
                board_id = int (board_id)
                data = DataFilter.read_file (file)
                sampling_rate = BoardShim.get_sampling_rate (board_id)
                eeg_channels = get_eeg_channels (board_id)
                for num, window_size in enumerate (window_sizes):
                    if data_type == 'focused':
                        cur_pos = sampling_rate * 10 # skip a little more for focus
                    else:
                        cur_pos = sampling_rate * 3
                    while cur_pos + int (window_size * sampling_rate) < data.shape[1]:
                        data_in_window = data[:, cur_pos:cur_pos + int (window_size * sampling_rate)]
                        bands = DataFilter.get_avg_band_powers (data_in_window, eeg_channels, sampling_rate, True)
                        feature_vector = np.concatenate ((bands[0], bands[1]))
                        dataset_x.append (feature_vector)
                        if data_type == 'relaxed':
                            dataset_y.append (0)
                        else:
                            dataset_y.append (1)
                        cur_pos = cur_pos + int (window_size * overlaps[num] * sampling_rate)
            except Exception as e:
                print (str (e))

    print ('Class 1: %d Class 0: %d' % (len ([x for x in dataset_y if x == 1]), len ([x for x in dataset_y if x == 0])))

    with open ('dataset_x.pickle', 'wb') as f:
        pickle.dump (dataset_x, f, protocol = 3)
    with open ('dataset_y.pickle', 'wb') as f:
        pickle.dump (dataset_y, f, protocol = 3)

    return dataset_x, dataset_y

def get_eeg_channels (board_id):
    eeg_channels = BoardShim.get_eeg_channels (board_id)
    # optional: filter some channels we dont want to consider
    try:
        eeg_names = BoardShim.get_eeg_names (board_id)
        selected_channels = list ()
        # blacklisted_channels = {'O1', 'O2'}
        blacklisted_channels = set ()
        for i, channel in enumerate (eeg_names):
            if not channel in blacklisted_channels:
                selected_channels.append (eeg_channels[i])
        eeg_channels = selected_channels
    except Exception as e:
        print (str (e))
    print ('channels to use: %s' % str (eeg_channels))
    return eeg_channels

def train_lda (data):
    model = LinearDiscriminantAnalysis ()
    print ('#### Linear Discriminant Analysis ####')
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'f1_macro', n_jobs = 8)
    print ('f1 macro %s' % str (scores))
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'precision_macro', n_jobs = 8)
    print ('precision macro %s' % str (scores))
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'recall_macro', n_jobs = 8)
    print ('recall macro %s' % str (scores))
    model.fit (data[0], data[1])
    write_model (model.intercept_, model.coef_, 'lda')

def train_regression (data):
    model = LogisticRegression (class_weight = 'balanced', solver = 'liblinear',
        max_iter = 4000, penalty = 'l2', random_state = 1)
    print('#### Logistic Regression ####')
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'f1_macro', n_jobs = 8)
    print ('f1 macro %s' % str (scores))
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'precision_macro', n_jobs = 8)
    print ('precision macro %s' % str (scores))
    scores = cross_val_score (model, data[0], data[1], cv = 5, scoring = 'recall_macro', n_jobs = 8)
    print ('recall macro %s' % str (scores))
    model.fit (data[0], data[1])
    write_model (model.intercept_, model.coef_, 'regression')

def train_knn (data):
    model = KNeighborsClassifier (n_neighbors = 5)
    print ('#### KNN ####')
    data_x = copy.deepcopy (data[0])
    for i, x in enumerate (data_x):
        for j in range (5, 10):
            data_x[i][j] = data_x[i][j] / 5 # idea to make stddev less important than avg, 5 random value
    scores = cross_val_score (model, data_x, data[1], cv = 5, scoring = 'f1_macro', n_jobs = 8)
    print ('f1 macro %s' % str (scores))
    scores = cross_val_score (model, data_x, data[1], cv = 5, scoring = 'precision_macro', n_jobs = 8)
    print ('precision macro %s' % str (scores))
    scores = cross_val_score (model, data_x, data[1], cv = 5, scoring = 'recall_macro', n_jobs = 8)
    print ('recall macro %s' % str (scores))
    write_knn_model (data)

def test_brainflow_lr (data):
    print ('Test BrainFlow LR')
    params = BrainFlowModelParams (BrainFlowMetrics.CONCENTRATION.value, BrainFlowClassifiers.REGRESSION.value)
    model = MLModel (params)
    start_time = time.time ()
    model.prepare ()
    predicted = [model.predict (x) > 0.5 for x in data[0]]
    model.release ()
    stop_time = time.time ()
    print ('Total time %f' % (stop_time - start_time))
    print (metrics.classification_report (data[1], predicted))

def test_brainflow_knn (data):
    print ('Test BrainFlow KNN')
    params = BrainFlowModelParams (BrainFlowMetrics.CONCENTRATION.value, BrainFlowClassifiers.KNN.value)
    model = MLModel (params)
    start_time = time.time ()
    model.prepare ()
    predicted = [model.predict (x) >= 0.5 for x in data[0]]
    model.release ()
    stop_time = time.time ()
    print ('Total time %f' % (stop_time - start_time))
    print (metrics.classification_report (data[1], predicted))

def test_brainflow_lda (data):
    print ('Test BrainFlow LDA')
    params = BrainFlowModelParams (BrainFlowMetrics.CONCENTRATION.value, BrainFlowClassifiers.LDA.value)
    model = MLModel (params)
    start_time = time.time ()
    model.prepare ()
    predicted = [model.predict (x) >= 0.5 for x in data[0]]
    model.release ()
    stop_time = time.time ()
    print ('Total time %f' % (stop_time - start_time))
    print (metrics.classification_report (data[1], predicted))
   
def test_brainflow_svm (data):
    print ('Test BrainFlow SVM')
    params = BrainFlowModelParams (BrainFlowMetrics.CONCENTRATION.value, BrainFlowClassifiers.SVM.value)
    model = MLModel (params)
    start_time = time.time ()
    model.prepare ()
    predicted = [model.predict (x) >= 0.5 for x in data[0]]
    model.release ()
    stop_time = time.time ()
    print ('Total time %f' % (stop_time - start_time))
    print (metrics.classification_report (data[1], predicted))


def main ():
    parser = argparse.ArgumentParser ()
    parser.add_argument ('--test', action = 'store_true')
    parser.add_argument('--reuse-dataset', action='store_true')
    parser.add_argument('--grid-search',action='store_true')
    args = parser.parse_args ()

    if args.reuse_dataset:
        with open ('dataset_x.pickle', 'rb') as f:
            dataset_x = pickle.load (f)
        with open ('dataset_y.pickle', 'rb') as f:
            dataset_y = pickle.load (f)
        data = dataset_x, dataset_y
    else:
        data = prepare_data ()
    if args.test:
        # since we port models from python to c++ we need to test it as well
        test_brainflow_knn (data)
        test_brainflow_lr (data)
        test_brainflow_svm (data)
        test_brainflow_lda (data)
    else:
        train_regression (data)
        # Don't use grid search method unless you have to as it takes a while to complete
        train_brainflow_search_svm (data) if args.grid_search else train_brainflow_svm (data)
        train_lda (data)
        train_knn (data)


if __name__ == '__main__':
    main ()
