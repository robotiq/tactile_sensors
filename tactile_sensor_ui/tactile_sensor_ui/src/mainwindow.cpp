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
#include <QWidgetAction>
#include <QTimer>
#include <mgl2/qmathgl.h>

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    fingerData(4096),   // Note: 4096 is the FFT size, don't reduce!
    fingerDataForLog(1024),
    logFile(NULL),
    csvSeparator(";")   // Because French programs sometimes take , as fractional point.
{
    ui->setupUi(this);

    QWidgetAction *wa = new QWidgetAction(this);
    wa->setDefaultWidget(ui->about);
    ui->menuAbout->addAction(wa);

    ui->statusBar->addPermanentWidget(ui->status);

    ui->alltabs->setCurrentIndex(0);

    ui->logPath->setText("logs/finger_data.csv");

    initUiGraphs();

    // Connections
    qRegisterMetaType<Fingers>("Fingers");
    connect(ui->refreshPorts, &QPushButton::pressed, this, &MainWindow::refreshPorts);
    connect(ui->connect, &QPushButton::pressed, this, &MainWindow::openCloseConnection);
    connect(ui->staticBaselineReset, &QPushButton::pressed, this, &MainWindow::resetStaticBaseline);
    connect(ui->staticRawValues, &QCheckBox::toggled, this, &MainWindow::showStaticRaw);
    connect(ui->logBrowse, &QPushButton::pressed, this, &MainWindow::selectLogFile);
    connect(ui->log, &QPushButton::pressed, this, &MainWindow::startStopLog);
    connect(this, &MainWindow::closeConnectionSignal, this, &MainWindow::closeConnection);
    connect(this, &MainWindow::updateConnectionDataRateSignal, this, &MainWindow::updateConnectionDataRate);
    connect(this, &MainWindow::newFingerDataSignal, this, &MainWindow::newFingerData);

    QTimer *slowUiTicker = new QTimer(this);
    connect(slowUiTicker, &QTimer::timeout, this, &MainWindow::slowUiUpdate);
    connect(slowUiTicker, &QTimer::timeout, this, &MainWindow::log);
    slowUiTicker->start(20);

    QTimer *fftTicker = new QTimer(this);
    connect(fftTicker, &QTimer::timeout, this, &MainWindow::updateFFT);
    fftTicker->start(1000);

    connectionClosed("Not connected");
    refreshPortsAutoconnect(true);

    resetStaticBaseline();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::newFingerData(Fingers f)
{
    // Replicate data for users

    // Persistent data used for plotting
    fingerData.push(f);
    // Consumed data used for logging
    fingerDataForLog.push(f);
}
