/*
 * Robotiq Tactile Sensor UI
 * Copyright (C) 2016  Shahbaz Youssefi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <stdio.h>
#include <mgl2/qt.h>
#include <fftw3.h>
#include "circular_buffer.h"
#include "finger_data.h"

namespace Ui {
class MainWindow;
}

#define READ_DATA_PERIOD_MS 1

class MainWindow: public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void openCloseConnection();
    void refreshPorts();
    void updateConnectionDataRate(unsigned int bs);
    void newFingerData(Fingers f);
    void slowUiUpdate();
    void updateFFT();
    void resetStaticBaseline();
    void showStaticRaw();
    void log();
    void selectLogFile();
    void startStopLog();

signals:
    void closeConnectionSignal(const char *status);
    void updateConnectionDataRateSignal(unsigned int bs);
    void newFingerDataSignal(Fingers f);

private:
    void refreshPortsAutoconnect(bool allowAutoconnect);
    void openConnection();
    void closeConnection();

    void connectionFailed(const char *status);
    void connectionClosed(const char *status);
    void connectionOpened(const char *status);

    void initUiGraphs();
    void updateGraphs();
    void updateGraphStatic();
    void updateGraphDynamic();
    void updateGraphIMU();

    void startLog();
    void stopLog();

private:
    Ui::MainWindow *ui;

    // Communication and data gathering
    SafeCircularBuffer<Fingers> fingerData, fingerDataForLog;
    class Communicator *communicator;

    // Graphics
    struct StaticGraph
    {
        mglData data;
        mglGraph *graph;
        QLabel *widget;

        bool shouldResetBaseline;
        uint16_t baseline[FINGER_STATIC_TACTILE_COUNT];
        unsigned int maxRange;
    };
    struct DynamicGraph
    {
        mglData data, fft, timestamps;
        mglGraph *graph, *fftGraph;
        QLabel *widget, *fftWidget;

        bool shouldUpdateFFTGraph;
        double *fftIn;
        fftw_complex *fftOut;
        fftw_plan fftPlan;
    };
    struct IMUGraph
    {
        mglData dataAccel, dataGyro, timestamps;
        mglGraph *graphAccel, *graphGyro;
        QLabel *widgetAccel, *widgetGyro;
    };

    StaticGraph staticGraphs[FINGER_COUNT];
    DynamicGraph dynamicGraphs[FINGER_COUNT];
    IMUGraph imuGraphs[FINGER_COUNT];

    FILE *logFile;
    const char *csvSeparator;
};

#endif // MAINWINDOW_H
