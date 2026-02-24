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

#include "mainwindow.h"
#include "ui_mainwindow.h"

void MainWindow::initUiGraphs()
{
    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        // TODO: see if commented-out graph settings are needed

        // Put placeholders for the graphs
        staticGraphs[f].widget = new QLabel(this);
        staticGraphs[f].widget->setAlignment(Qt::AlignCenter);
        ui->staticGraphs->addWidget(staticGraphs[f].widget);

        dynamicGraphs[f].widget = new QLabel(this);
        dynamicGraphs[f].fftWidget = new QLabel(
#if READ_DATA_PERIOD_MS != 1
                                                "FFT Not Supported for not-1KHz Acquisition",
#endif
                                                this);
        dynamicGraphs[f].widget->setAlignment(Qt::AlignCenter);
        dynamicGraphs[f].fftWidget->setAlignment(Qt::AlignCenter);
        ui->dynamicGraphs->addWidget(dynamicGraphs[f].widget, 0, f);
        ui->dynamicGraphs->addWidget(dynamicGraphs[f].fftWidget, 1, f);

        imuGraphs[f].widgetAccel = new QLabel(this);
        imuGraphs[f].widgetGyro = new QLabel(this);
        imuGraphs[f].widgetAccel->setAlignment(Qt::AlignCenter);
        imuGraphs[f].widgetGyro->setAlignment(Qt::AlignCenter);
        ui->accelGraphs->addWidget(imuGraphs[f].widgetAccel);
        ui->gyroGraphs->addWidget(imuGraphs[f].widgetGyro);

        // Allocate data and graph objects for the eventual rendering
        staticGraphs[f].data.Create(FINGER_STATIC_TACTILE_ROW + 2, FINGER_STATIC_TACTILE_COL + 2);
        staticGraphs[f].graph = new mglGraph(0, 600, 500);
        staticGraphs[f].graph->Rotate(60, 250);
        staticGraphs[f].graph->Light(true);
        //staticGraphs[f].graph->SetTuneTicks(true);
        staticGraphs[f].graph->SetTicks('x', 1, 0);
        staticGraphs[f].graph->Alpha(false);

        dynamicGraphs[f].data.Create(4000 / READ_DATA_PERIOD_MS);
        dynamicGraphs[f].fft.Create(2048);
        dynamicGraphs[f].timestamps.Create(4000 / READ_DATA_PERIOD_MS);
        dynamicGraphs[f].fftIn = new double[4096];
        dynamicGraphs[f].fftOut = new fftw_complex[4096];   // TODO: do we need 4096 or 2048?
        dynamicGraphs[f].fftPlan = fftw_plan_dft_r2c_1d(4096, dynamicGraphs[f].fftIn, dynamicGraphs[f].fftOut, FFTW_ESTIMATE);
        dynamicGraphs[f].graph = new mglGraph(0, 600, 250);
        dynamicGraphs[f].graph->SetTicks('x', 1, 0);
        //dynamicGraphs[f].graph->SetTicksVal(???);
        //dynamicGraphs[f].graph->SetLight(true);
        dynamicGraphs[f].fftGraph = new mglGraph(0, 600, 250);
        dynamicGraphs[f].fftGraph->SetTicks('x', 250, 0);

        imuGraphs[f].dataAccel.Create(2000 / READ_DATA_PERIOD_MS, 3);
        imuGraphs[f].dataGyro.Create(2000 / READ_DATA_PERIOD_MS, 3);
        imuGraphs[f].timestamps.Create(2000 / READ_DATA_PERIOD_MS);
        imuGraphs[f].graphAccel = new mglGraph(0, 600, 250);
        imuGraphs[f].graphAccel->SetTicks('x', 1, 0);
        //imuGraphs[f].graphAccel->SetTicksVal(???);
        //imuGraphs[f].graphAccel->SetLight(true);
        imuGraphs[f].graphGyro = new mglGraph(0, 600, 250);
        imuGraphs[f].graphGyro->SetTicks('x', 1, 0);
        //imuGraphs[f].graphGyro->SetTicksVal(???);
        //imuGraphs[f].graphGyro->SetLight(true);
    }
}

void MainWindow::slowUiUpdate()
{
    updateGraphs();
}

void MainWindow::updateGraphs()
{
    switch (ui->alltabs->currentIndex())
    {
    case 1:
        updateGraphStatic();
        break;
    case 2:
        updateGraphDynamic();
        break;
    case 3:
        updateGraphIMU();
        break;
    default:
        break;
    }
}

void MainWindow::updateGraphStatic()
{
    for (int f = 0; f < FINGER_COUNT; ++f)
        staticGraphs[f].graph->Clf();

    if (fingerData.empty())
        return;
    Fingers fd = fingerData.back();

    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        if (staticGraphs[f].shouldResetBaseline)
        {
            staticGraphs[f].shouldResetBaseline = false;
            for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; ++i)
                staticGraphs[f].baseline[i] = fd.finger[f].staticTactile[i];
        }

        // Slowly reduce maxRange to zoom back in
        staticGraphs[f].maxRange *= 0.95;
        if (staticGraphs[f].maxRange < 1000)
            staticGraphs[f].maxRange = 1000;

        // Take latest data
        for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; ++i)
        {
            uint16_t d = fd.finger[f].staticTactile[i];

            if (!ui->staticRawValues->isChecked())
            {
                // Remove baseline.  If going lower than baseline, show as 0
                if (d < staticGraphs[f].baseline[i])
                    d = 0;
                else
                    d -= staticGraphs[f].baseline[i];
            }

            // Keep maximum data for
            if (d > staticGraphs[f].maxRange)
                staticGraphs[f].maxRange = d;

            int r = i / FINGER_STATIC_TACTILE_ROW;
            int c = i % FINGER_STATIC_TACTILE_ROW;
            staticGraphs[f].data.a[(r + 1) * (FINGER_STATIC_TACTILE_ROW + 2) + (c + 1)] = d;
        }

        mglGraph *g = staticGraphs[f].graph;
        g->SetRanges(0, 6, 0, 4, -200, staticGraphs[f].maxRange + 200);

        // Interpolate the data for the graph to look nicer
        mglData interpolated = staticGraphs[f].data.Resize((FINGER_STATIC_TACTILE_ROW + 2) * 4, (FINGER_STATIC_TACTILE_COL + 2) * 4);
        g->Surf(interpolated, "#, {B,0}{b,0.17}{c,0.25}{y,0.35}{r,0.55}{R,0.85}", "meshnum 15");
        g->Axis();
        staticGraphs[f].widget->setPixmap(QPixmap::fromImage(
              QImage(g->GetRGBA(), g->GetWidth(), g->GetHeight(), QImage::Format_RGBA8888)));
    }
}

void MainWindow::updateGraphDynamic()
{
    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        dynamicGraphs[f].graph->Clf();
        if (dynamicGraphs[f].shouldUpdateFFTGraph)
            dynamicGraphs[f].fftGraph->Clf();
    }

    if (fingerData.empty())
        return;

    std::vector<Fingers> fd;
    fingerData.extract(fd);

    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        // If not enough data, don't calculate FFT
        if (fd.size() < 4096)
            dynamicGraphs[f].shouldUpdateFFTGraph = false;

        int64_t oldestTime, newestTime;

        // Take data and store for plotting
        size_t graphDataCount = dynamicGraphs[f].data.GetNx();
        size_t start = graphDataCount < fd.size()?fd.size() - graphDataCount:0;
        size_t end = fd.size();

        oldestTime = fd[start].timestamp;
        newestTime = fd[end - 1].timestamp;
        int64_t lastTimestamp = oldestTime - 1;
        for (size_t i = start; i < end; ++i)
        {
            int16_t d = fd[i].finger[f].dynamicTactile[0];
            dynamicGraphs[f].data.a[i - start] = d * 1.024 / 32767;     // Note: 1.024 is voltage applied to sensor
            int64_t t = fd[i].timestamp;
            // Non-realtime operating systems may read multiple messages in the same millisecond, so this just spaces them out
            if (t <= lastTimestamp)
                t = lastTimestamp + 1;
            dynamicGraphs[f].timestamps.a[i - start] = t / 1000.0f;
            lastTimestamp = t;
        }

        mglGraph *g = dynamicGraphs[f].graph;
        g->SetRanges(oldestTime / 1000.0f, newestTime / 1000.0f, -1, 1);

        g->Axis();
        g->Label('y',"mV",0);
        g->Label('x',"s",0);
        g->Plot(mglData(dynamicGraphs[f].timestamps.a, end - start), mglData(dynamicGraphs[f].data.a, end - start));
        dynamicGraphs[f].widget->setPixmap(QPixmap::fromImage(
              QImage(g->GetRGBA(), g->GetWidth(), g->GetHeight(), QImage::Format_RGBA8888)));

        // If time to do FFT, do it
        if (dynamicGraphs[f].shouldUpdateFFTGraph)
        {
            dynamicGraphs[f].shouldUpdateFFTGraph = false;

            double maxPower = 0;

            start = fd.size() > 4096?fd.size() - 4096:0;
            end = fd.size();
            for (size_t i = start; i < end; ++i)
                dynamicGraphs[f].fftIn[i - start] = fd[i].finger[f].dynamicTactile[0];
            fftw_execute(dynamicGraphs[f].fftPlan);
            for (size_t i = 0; i < 2048; ++i)
            {
                fftw_complex &c = dynamicGraphs[f].fftOut[i];
                double power = sqrt(c[0] * c[0] + c[1] * c[1]);     // The amplitude of the Fourier Transform for each frequency
                dynamicGraphs[f].fft.a[i] = power;
                if (power > maxPower)
                    maxPower = power;
            }

            if (maxPower > 1000000)
                maxPower = 1000000;

            mglGraph *g = dynamicGraphs[f].fftGraph;
            mreal xvalues[4] = {512, 1024, 1536, 2048};
            g->SetRanges(0, 2048, 0, maxPower);
            g->SetTicksVal('x', mglData(4, xvalues), "\\125\n\\250\n\\375\n\\500");

            g->Axis();
            g->Label('x',"Hz",0);
            g->Plot(dynamicGraphs[f].fft);
            dynamicGraphs[f].fftWidget->setPixmap(QPixmap::fromImage(
                  QImage(g->GetRGBA(), g->GetWidth(), g->GetHeight(), QImage::Format_RGBA8888)));

        }
    }
}

void MainWindow::updateGraphIMU()
{
    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        imuGraphs[f].graphAccel->Clf();
        imuGraphs[f].graphGyro->Clf();
    }

    if (fingerData.empty())
        return;

    std::vector<Fingers> fd;
    fingerData.extract(fd);

    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        int64_t oldestTime, newestTime;
        double maxAccel = 1, minAccel = -1, maxGyro = 1, minGyro = -1;

        // Take acceleration and gyro data and store for plotting
        size_t graphDataCount = imuGraphs[f].dataAccel.GetNx();
        if (graphDataCount > imuGraphs[f].dataGyro.GetNx())
            graphDataCount = imuGraphs[f].dataGyro.GetNx();
        size_t start = graphDataCount < fd.size()?fd.size() - graphDataCount:0;
        size_t end = fd.size();

        oldestTime = fd[start].timestamp;
        newestTime = fd[end - 1].timestamp;
        int64_t lastTimestamp = oldestTime - 1;
        for (size_t i = start; i < end; ++i)
        {
            int64_t t = fd[i].timestamp;
            // Again, for non-realtime operating systems because they may read multiple messages in the same millisecond
            if (t <= lastTimestamp)
                t = lastTimestamp + 1;
            lastTimestamp = t;

            imuGraphs[f].timestamps.a[i - start] = t / 1000.0f;

            for (int j = 0; j < 3; ++j)
            {
                int16_t a = fd[i].finger[f].accelerometer[j];
                int16_t g = fd[i].finger[f].gyroscope[j];
                imuGraphs[f].dataAccel.a[j * graphDataCount + i - start] = a;
                imuGraphs[f].dataGyro.a[j * graphDataCount + i - start] = g;

                if (a > maxAccel) maxAccel = a;
                if (a < minAccel) minAccel = a;
                if (g > maxGyro) maxGyro = g;
                if (g < minGyro) minGyro = g;
            }
        }

        mglGraph *g = imuGraphs[f].graphAccel;
        g->SetRanges(oldestTime / 1000.0f, newestTime / 1000.0f, minAccel, maxAccel);

        g->Axis();
        g->Label('x',"s",0);
        for (int j = 0; j < 3; ++j)
            g->Plot(mglData(imuGraphs[f].timestamps.a, end - start), mglData(imuGraphs[f].dataAccel.a + j * graphDataCount, end - start));
        imuGraphs[f].widgetAccel->setPixmap(QPixmap::fromImage(
              QImage(g->GetRGBA(), g->GetWidth(), g->GetHeight(), QImage::Format_RGBA8888)));

        g = imuGraphs[f].graphGyro;
        g->SetRanges(oldestTime / 1000.0f, newestTime / 1000.0f, minGyro, maxGyro);

        g->Axis();
        g->Label('x',"s",0);
        for (int j = 0; j < 3; ++j)
            g->Plot(mglData(imuGraphs[f].timestamps.a, end - start), mglData(imuGraphs[f].dataGyro.a + j * graphDataCount, end - start));
        imuGraphs[f].widgetGyro->setPixmap(QPixmap::fromImage(
              QImage(g->GetRGBA(), g->GetWidth(), g->GetHeight(), QImage::Format_RGBA8888)));
    }
}


void MainWindow::updateFFT()
{
#if READ_DATA_PERIOD_MS == 1
    for (int f = 0; f < FINGER_COUNT; ++f)
        dynamicGraphs[f].shouldUpdateFFTGraph = true;
#endif
}

void MainWindow::resetStaticBaseline()
{
    for (int f = 0; f < FINGER_COUNT; ++f)
    {
        staticGraphs[f].shouldResetBaseline = true;
        staticGraphs[f].maxRange = 0;
    }
}

void MainWindow::showStaticRaw()
{
    ui->staticBaselineReset->setEnabled(!ui->staticRawValues->isChecked());
}
