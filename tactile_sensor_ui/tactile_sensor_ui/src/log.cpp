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
#include "finger_data.h"
#include <QFileDialog>

void MainWindow::log()
{
    if (logFile == NULL)
        return;

    std::vector<Fingers> fd;
    fingerDataForLog.extract(fd, true);

    for (size_t i = 0; i < fd.size(); ++i)
    {
        fprintf(logFile, "%lld", (long long)fd[i].timestamp);
        for (int f = 0; f < FINGER_COUNT; ++f)
            for (int s = 0; s < FINGER_DYNAMIC_TACTILE_COUNT; ++s)
                fprintf(logFile, "%s %d", csvSeparator, fd[i].finger[f].dynamicTactile[s]);
        for (int f = 0; f < FINGER_COUNT; ++f)
            for (int s = 0; s < FINGER_STATIC_TACTILE_COUNT; ++s)
                fprintf(logFile, "%s %d", csvSeparator, fd[i].finger[f].staticTactile[s]);
        for (int f = 0; f < FINGER_COUNT; ++f)
            for (int s = 0; s < 3; ++s)
                fprintf(logFile, "%s %d", csvSeparator, fd[i].finger[f].accelerometer[s]);
        for (int f = 0; f < FINGER_COUNT; ++f)
            for (int s = 0; s < 3; ++s)
                fprintf(logFile, "%s %d", csvSeparator, fd[i].finger[f].gyroscope[s]);
        fprintf(logFile, "\n");
    }
}

void MainWindow::selectLogFile()
{
    QString filename = QFileDialog::getOpenFileName(this, "Log file", ".", "Comma Separated Values (*.csv)");
    if (filename != "")
        ui->logPath->setText(filename);
}

void MainWindow::startStopLog()
{
    if (logFile)
        stopLog();
    else
        startLog();
}

void MainWindow::startLog()
{
    logFile = fopen(ui->logPath->text().toUtf8().data(), "w");
    if (logFile == NULL)
    {
        stopLog();
        ui->logPath->setStyleSheet("background-color: rgb(255, 63, 63);");
        return;
    }

    ui->logPath->setStyleSheet("");
    ui->log->setText("Stop Logging");
    ui->logPath->setEnabled(false);
    ui->logBrowse->setEnabled(false);

    fprintf(logFile, "Time(ms)");
    for (int f = 0; f < FINGER_COUNT; ++f)
        for (int i = 0; i < FINGER_DYNAMIC_TACTILE_COUNT; ++i)
            fprintf(logFile, "%s D%d_%d", csvSeparator, i, f);
    for (int f = 0; f < FINGER_COUNT; ++f)
        for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; ++i)
            fprintf(logFile, "%s S%d_%d", csvSeparator, i, f);
    for (int f = 0; f < FINGER_COUNT; ++f)
        fprintf(logFile, "%s Ax%d%s Ay%d%s Az%d", csvSeparator, f, csvSeparator, f, csvSeparator, f);
    for (int f = 0; f < FINGER_COUNT; ++f)
        fprintf(logFile, "%s Gx%d%s Gy%d%s Gz%d", csvSeparator, f, csvSeparator, f, csvSeparator, f);
    fprintf(logFile, "\n");
}

void MainWindow::stopLog()
{
    if (logFile)
    {
        fclose(logFile);
        logFile = NULL;
    }

    ui->log->setText("Start Logging");
    ui->logPath->setEnabled(true);
    ui->logBrowse->setEnabled(true);
}
