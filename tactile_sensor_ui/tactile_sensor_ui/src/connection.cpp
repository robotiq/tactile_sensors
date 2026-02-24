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
#include "communicator.h"
#include <QSerialPortInfo>
#include <QDir>
#include <QFileInfo>

void MainWindow::refreshPorts()
{
    refreshPortsAutoconnect(true);
}

void MainWindow::refreshPortsAutoconnect(bool allowAutoconnect)
{
    int index = 0;
    ui->availablePorts->clear();
    bool foundFinger = false;

    auto addPortToList = [&](const QString &portName, const QString &description, bool markAsFinger) {
        QString desc = description;
        if (desc.length() >= 20)
            desc = desc.left(17) + "...";

        if (desc.isEmpty())
            ui->availablePorts->addItem(portName);
        else
            ui->availablePorts->addItem(portName + " (" + desc + ")");

        if (!foundFinger && markAsFinger)
        {
            ui->availablePorts->setCurrentIndex(index);
            foundFinger = true;
        }

        ++index;
    };

    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        const QString desc = info.description();
        const bool matchesFinger = (desc == "Robotiq Tactile Sensor" || desc == "CoRo Tactile Sensor" || desc == "Cypress USB UART");
        addPortToList(info.portName(), desc, matchesFinger);
    }

    QDir devDir("/dev");
    devDir.setNameFilters(QStringList() << "rq_tsf85_*");
    devDir.setFilter(QDir::AllEntries | QDir::System | QDir::Readable | QDir::NoDotAndDotDot | QDir::Hidden);
    const QFileInfoList symlinks = devDir.entryInfoList();
    for (const QFileInfo &fi : symlinks)
    {
        const QString entryName = fi.fileName();
        QString details = fi.isSymLink() ? QString("symlink") : QString("device");

        if (fi.isSymLink())
        {
            QString target = fi.symLinkTarget();
            if (!target.isEmpty())
                target = QFileInfo(target).fileName();
            if (!target.isEmpty())
                details = QString("-> %1").arg(target);
        }

        addPortToList(entryName, details, true);
    }

    if (foundFinger && ui->autoconnect->isChecked() && allowAutoconnect)
        openConnection();
}

void MainWindow::openCloseConnection()
{
    if (communicator)
        closeConnection();
    else
        openConnection();
}

void MainWindow::openConnection()
{
    if (ui->availablePorts->count() < 0)
    {
        connectionFailed("No serial ports detected");
        return;
    }

    QString port = ui->availablePorts->currentText();
    port = port.left(port.indexOf(' '));

    communicator = new Communicator(this, port.toUtf8().data(), READ_DATA_PERIOD_MS);
    if (communicator->portError())
    {
        switch (communicator->portError())
        {
        case QSerialPort::PermissionError:
            connectionFailed("Insufficient permission to open port");
            break;
        case QSerialPort::OpenError:
            connectionFailed("Port is already opened by another application");
            break;
        default:
            connectionFailed("Could not open port");
            break;
        }
        delete communicator;
        communicator = NULL;
        return;
    }

    connectionOpened(port.toUtf8().data());
    communicator->start();
}

void MainWindow::closeConnection()
{
    delete communicator;
    communicator = nullptr;

    connectionClosed("Not connected");
    refreshPortsAutoconnect(false);
}

void MainWindow::connectionFailed(const char *status)
{
    connectionClosed(status);
    refreshPortsAutoconnect(false);

    stopLog();
    ui->log->setEnabled(false);
}

void MainWindow::connectionClosed(const char *status)
{
    ui->connectionStatus->setText(status);
    ui->connectionDataRate->hide();
    ui->connectionStatusSeparator->hide();

    for (int i = 1; i < ui->alltabs->count(); ++ i)
        ui->alltabs->setTabEnabled(i, false);

    ui->connect->setText("Connect");

    fingerData.clear();

    stopLog();
    ui->log->setEnabled(false);
}

void MainWindow::connectionOpened(const char *status)
{
    ui->connectionStatus->setText(status);
    ui->connectionDataRate->setText("0 KB/s");
    ui->connectionDataRate->show();
    ui->connectionStatusSeparator->show();

    for (int i = 1; i < ui->alltabs->count(); ++ i)
        ui->alltabs->setTabEnabled(i, true);

    ui->connect->setText("Disconnect");

    // switch to static data
    ui->alltabs->setCurrentIndex(1);

    ui->log->setEnabled(true);
}

void MainWindow::updateConnectionDataRate(unsigned int bs)
{
    ui->connectionDataRate->setText(QString().asprintf("%u.%03u KB/s", bs / 1000, bs % 1000));
}
