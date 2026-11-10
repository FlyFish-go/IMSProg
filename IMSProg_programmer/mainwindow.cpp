/*
 * Copyright (C) 2023 - 2026 Mikhail Medvedev <e-ink-reader@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QErrorMessage>
#include <QDragEnterEvent>
#include <QtGui>
#include <QComboBox>
#include <QStandardItemModel>
#include <QFileInfo>
#include <QStandardPaths>
#include <QInputMethod>
#include <QKeyEvent>
#include <QInputMethod>
#include <QActionGroup>
#include <QCloseEvent>
#include <QStyleFactory>
#include <QScrollBar>
#include <QSettings>
#include "qhexedit.h"
#include "dialogsp.h"
#include "dialogrp.h"
#include "dialogsetaddr.h"
#include "dialogsecurity.h"
#include "dialognandsecurity.h"
#include "dialognandsr.h"
#include "dialogbbm.h"
#include "dialogfill.h"
#include "hexutility.h"
#include "driverinstaller.h"
#include <stddef.h>
#include <stdint.h>
#include <QSignalBlocker>
#include <limits>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QClipboard>
#include <vector>
#include <QFormLayout>
#include <QHBoxLayout>
#ifdef _WIN32
#include "windows/ch347_win32.h"
#endif


// Called by the NOR/NAND core between pages. Long NAND blocks (64 pages)
// used to keep the event loop blocked for the whole block; pumping here at
// ~30 ms keeps clicks, Stop and repaints responsive at negligible cost.
static void imsprogUiPump()
{
    static QElapsedTimer last;
    if (!last.isValid() || last.elapsed() >= 30) {
        QCoreApplication::processEvents();
        last.restart();
    }
}

static QString formatRate(double bytesPerSecond)
{
    if (bytesPerSecond >= 1024.0 * 1024.0)
        return QString::number(bytesPerSecond / (1024.0 * 1024.0), 'f', 2) + " MB/s";
    return QString::number(bytesPerSecond / 1024.0, 'f', 1) + " KB/s";
}

static QString formatSize(quint64 bytes)
{
    if (bytes >= 1024ull * 1024ull)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + " MB";
    return QString::number(bytes / 1024.0, 'f', 1) + " KB";
}

static QString formatDuration(qint64 seconds)
{
    if (seconds < 0) seconds = 0;
    if (seconds >= 3600)
        return QString("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QChar('0'))
                                  .arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}

void MainWindow::opSpeedStart()
{
    opLabel = ui->statusMessage->text();
    opLastShownMs = -1;
    opBytesDone = 0;
    opTimer.start();
}

void MainWindow::opSpeedUpdate(quint64 blocksDone, quint64 totalBlocks, quint64 bytesPerBlock)
{
    if (!opTimer.isValid() || !totalBlocks) return;
    const qint64 ms = opTimer.elapsed();
    opBytesDone = blocksDone * bytesPerBlock;
    // Refresh twice per second; skip the first 300 ms while the rate settles.
    if (ms < 300 || (opLastShownMs >= 0 && ms - opLastShownMs < 500)) return;
    opLastShownMs = ms;
    const double rate = opBytesDone * 1000.0 / double(ms);
    const quint64 total = totalBlocks * bytesPerBlock;
    const qint64 left = rate > 0 ? qint64((total - opBytesDone) / rate + 0.5) : 0;
    ui->statusMessage->setText(opLabel + "    " +
        tr("%1, %2 left").arg(formatRate(rate), formatDuration(left)));
}

void MainWindow::opSpeedFinish()
{
    if (opTimer.isValid() && opBytesDone) {
        const qint64 ms = qMax<qint64>(opTimer.elapsed(), 1);
        ui->statusMessage->setText(tr("Done: %1 in %2, average %3")
            .arg(formatSize(opBytesDone), formatDuration((ms + 500) / 1000),
                 formatRate(opBytesDone * 1000.0 / double(ms))));
    } else {
        ui->statusMessage->setText("");
    }
    opTimer.invalidate();
    opBytesDone = 0;
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
 ui->setupUi(this);
 setupAppearance();
 imsprog_progress_hook = imsprogUiPump;

 max_rec = 0;
 isHalted = false;
 lastDirectory = QDir::homePath(); //"/home/";
 grnKeyStyle = "QPushButton{color:#fff;background-color: rgb(120, 183, 140);border-radius: 20px;border: 2px solid #094065;border-radius:8px;font-weight:600;}QPushButton::pressed{background-color: rgb(115, 210, 22);}";
 redKeyStyle = "QPushButton{color:#fff;background-color:#f66;border-radius: 20px;border: 2px solid #094065;border-radius:8px;font-weight:600;}";
 timer = new QTimer();
 connect(timer, SIGNAL(timeout()), this, SLOT(slotTimerAlarm()));
 // Bundle startup verification must not probe attached programmers.
 if (!qApp->property("app/portableSmokeTest").toBool())
     timer->start(2000); //10000

 ui->actionSetProgrammerType->setExclusive(true);
 ui->statusMessage->setText("");
 ui->actionStop->setDisabled(true);
 ui->statusBar->addPermanentWidget(ui->statusMessage,1);
 ui->statusBar->addPermanentWidget(ui->lStatus,0);
 ui->statusBar->addPermanentWidget(ui->eStatus,0);
 ui->statusBar->addPermanentWidget(ui->jLabel,0);
 ui->statusBar->addPermanentWidget(ui->jedecEdit,0);
 ui->statusBar->addPermanentWidget(ui->cLabel,0);
 ui->statusBar->addPermanentWidget(ui->crcEdit,0);
 setupProgrammerMenu();
 ui->progressBar->setStyle(QStyleFactory::create("Fusion"));
 ui->progressBar->setValue(0);
 ui->comboBox_name->addItems({""});
 ui->comboBox_man->addItems({""});

 ui->comboBox_vcc->addItem(" ", 0);
 ui->comboBox_vcc->addItem("3.3 V", 1);
 ui->comboBox_vcc->addItem("1.8 V", 2);
 ui->comboBox_vcc->addItem("2.5 V", 3);
 ui->comboBox_vcc->addItem("5.0 V", 4);

 ui->comboBox_type->addItem("SPI_FLASH", 0);
 ui->comboBox_type->addItem("24_EEPROM", 1);
 ui->comboBox_type->addItem("93_EEPROM", 2);
 ui->comboBox_type->addItem("25_EEPROM", 3);
 ui->comboBox_type->addItem("95_EEPROM", 4);
 ui->comboBox_type->addItem("45_EEPROM", 5);
 ui->comboBox_type->addItem("SPI_NAND ", 6);

 ui->comboBox_addr4bit->addItem("No", 0);
 ui->comboBox_addr4bit->addItem("Yes", 0x01);
 ui->comboBox_addr4bit->addItem("Winbond", 0x11);
 ui->comboBox_addr4bit->addItem("Spansion", 0x21);

 ui->comboBox_page->addItem(" ", 0);
 ui->comboBox_page->addItem("256", 256);

 ui->comboBox_block->addItem(" ", 0);
 ui->comboBox_block->addItem("64 K",   64 * 1024);
 ui->comboBox_block->addItem("128 K", 128 * 1024);
 ui->comboBox_block->addItem("256 K", 256 * 1024);

 ui->comboBox_ECC->addItem(" ",   0);
 ui->comboBox_ECC->addItem("64",  1);
 ui->comboBox_ECC->addItem("128", 2);
 ui->comboBox_ECC->addItem("256", 3);

 ui->comboBox_i2cSpeed->addItem("       ",   0);
 ui->comboBox_i2cSpeed->addItem("20 kHz ",  20);
 ui->comboBox_i2cSpeed->addItem("100 kHz", 100);
 ui->comboBox_i2cSpeed->addItem("400 kHz", 400);
 ui->comboBox_i2cSpeed->addItem("750 kHz", 750);
 ui->comboBox_i2cSpeed->setCurrentIndex(2);
 currentI2CBusSpeed = static_cast<uint16_t>(ui->comboBox_i2cSpeed->currentData().toUInt());

 ui->comboBox_raw->addItem(tr("Main only"), 0);
 ui->comboBox_raw->addItem(tr("Main + OOB"), 1);
 ui->comboBox_onDieEcc->addItem(tr("On (corrected read)"), 1);
 ui->comboBox_onDieEcc->addItem(tr("Off (raw read)"), 0);

 current_programmer = 0;

 currentChipSize = 0;
 currentNumBlocks = 0;
 currentBlockSize = 0;
 currentPageSize = 0;
 currentAlgorithm = 0;
 currentChipType = 0;
 currentDelay = 1000;
 blockStartAddr = 0;
 blockLen = 0;
 currentAddr4bit = 0;
 currentECCsize = 0;
 filled = 0;
 numberOfReads = 0;
 nandSettings = 0;
 cmdStarted = false;
 chipData.reserve(512 * 1024 *1024 + 2048);
 chipData.resize(256);
 chipData.fill(char(0xff));
 oldChipData.reserve(512 * 1024 *1024 + 2048);
 oldChipData.resize(256);
 oldChipData.fill(char(0xff));
 nandRaw = false;

 //Reading ini file
 QString iniPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("config.ini");
 qDebug() << "Using config file " << iniPath;
 if (QFileInfo(iniPath).exists())
 {
     QSettings settings(iniPath, QSettings::IniFormat);
     settings.beginGroup("Chip");
       lastDirectory = settings.value("ChipDirectory").toString();
     settings.endGroup();
     settings.beginGroup("Device");
       current_programmer =  static_cast<unsigned char>(settings.value("ProgrammerType").toInt());
       if (current_programmer == 0) ui->actionCH341A_B_v1_2->setChecked(true);
       if (current_programmer == 1) ui->actionCH341A_v1_7->setChecked(true);
       if (current_programmer == 2)
          {
              ui->actionCH347T->setChecked(true);
              SetItemStatus("comboBox_type", 2, true);
          }
       if (current_programmer == 3)
          {
              ui->actionCH347T_v1_1->setChecked(true);
              SetItemStatus("comboBox_type", 2, true);
          }
       if (current_programmer == 4) ui->actionFT232H_v1_2->setChecked(true);
       if (current_programmer > PROG_CH347F_SAFE) current_programmer = 0;
       // Menu v2: one entry per programmer family plus a fixed SPI clock.
       // Map the earlier board/preset entries to the equivalent clock.
       if (settings.contains("SpiClockKHz"))
           spiClockChoice = static_cast<uint16_t>(settings.value("SpiClockKHz").toUInt());
       else if (current_programmer == 3) spiClockChoice = 15000;            // CH347T v1.1
       else if (current_programmer == PROG_CH347F_FAST) spiClockChoice = 60000;
       else if (current_programmer == PROG_CH347F_SAFE) spiClockChoice = 7500;
       if (current_programmer == 1 || current_programmer == 4) current_programmer = 0; // FT232H removed
       if (current_programmer == 3) current_programmer = 2;
       if (current_programmer == PROG_CH347F_FAST || current_programmer == PROG_CH347F_SAFE)
           current_programmer = PROG_CH347F_STD;
       if (PROG_IS_CH347F(current_programmer))
          {
              ui->actionCH347F->setChecked(current_programmer == PROG_CH347F_STD);
              ui->actionCH347F_fast->setChecked(current_programmer == PROG_CH347F_FAST);
              ui->actionCH347F_safe->setChecked(current_programmer == PROG_CH347F_SAFE);
              SetItemStatus("comboBox_type", 2, true);
          }
     settings.endGroup();
     settings.beginGroup("FormPosition");
     if (settings.contains("geometry"))
     {
         restoreGeometry(settings.value("geometry").toByteArray());
     }
     settings.endGroup();
 }
 applyPlatformRestrictions();
 if (programmerFamily() == FamilyCH341) spiClockChoice = 0;
 applySpiClockChoice();
 // connect and status check
 updateProgrammerMenu();
 refreshDriverStatus();
 statusCH341 = -1;
 if (!qApp->property("app/portableSmokeTest").toBool())
 {
     statusCH341 = ProgDeviceInit(current_programmer, currentChipType,
         currentChipType == 1 ? currentI2CBusSpeed : getInterfaceSpeed(current_programmer, currentChipType, currentDelay));
     ProgDeviceClose(current_programmer);
 }
 ch341StatusFlashing();
 QFont heFont;
 heFont = QFont("Monospace", 10);
 heFont.setStyleHint(QFont::TypeWriter);
 heFont.setFixedPitch(true);
 hexEdit = new QHexEdit(ui->frame);
 hexEdit->setData(chipData);
 hexEdit->setHexCaps(true);
 hexEdit->setFont(heFont);
 QStringList commandLineParams = QCoreApplication::arguments();
 QString commandLineFileName ="";
 if (commandLineParams.count() > 1)
   {
        commandLineFileName = commandLineParams[1];
        QFileInfo commandLine(commandLineFileName);
        if ((commandLine.exists()) && !(QString::compare(commandLine.suffix(), "bin", Qt::CaseInsensitive)))
        {
            lastDirectory = commandLineFileName;
            cmdStarted = true;
        }
   }
 progInit();
 defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
 uiReady = true;
 updateNandControlVisibility();
 updateNandSelectionInfo();
 if (cmdStarted) on_actionOpen_triggered();
}

void MainWindow::setupAppearance()
{
    // Keep the original IMSProg layout and colours; only add NAND tooltips
    // and the chip database updater entry.
    ui->comboBox_ECC->setToolTip(tr("Spare (OOB) bytes per page."));
    ui->comboBox_raw->setToolTip(tr("Image layout: main data only, or main data followed by each page's OOB."));
    ui->comboBox_onDieEcc->setToolTip(tr("On: the chip corrects bit errors; pages it cannot correct are still read and reported.\nOff: raw read. Use Off for chips written by a host controller with its own ECC (on-die ECC disabled)."));
    ui->label_nandInfo->setWordWrap(true);
    ui->eStatus->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->eStatus->setMinimumWidth(ui->eStatus->fontMetrics().boundingRect(tr("Not connected")).width() + 24);
    QAction *updateDatabase = new QAction(tr("Update chip database…"), this);
    updateDatabase->setObjectName("actionUpdateChipDatabase");
    ui->menuFile->insertAction(ui->actionEdit_chips_Database, updateDatabase);
    // Read icon with a small verify badge, so the toolbar button differs from Read.
    QPixmap readVerifyIcon(":/res/img/read64.png");
    {
        QPainter painter(&readVerifyIcon);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const int badge = readVerifyIcon.width() * 11 / 20;
        painter.drawPixmap(readVerifyIcon.width() - badge, readVerifyIcon.height() - badge, badge, badge,
                           QPixmap(":/res/img/verify64.png"));
    }
    actionReadVerify = new QAction(QIcon(readVerifyIcon), tr("Verified read (read twice)"), this);
    actionReadVerify->setObjectName("actionReadVerify");
    actionReadVerify->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    actionReadVerify->setStatusTip(tr("Read the chip twice and compare. Blocks that differ are re-read at lower clocks; blocks that never agree are set by majority vote."));
    ui->menuProgrammer->insertAction(ui->actionErase, actionReadVerify);
    actionReadVerify->setToolTip(tr("Verified read (read twice)") + " (Ctrl+Shift+R)\n" + actionReadVerify->statusTip());
    ui->mainToolBar->insertAction(ui->actionErase, actionReadVerify);
    connect(actionReadVerify, &QAction::triggered, this, &MainWindow::readAndVerify);
    connect(updateDatabase, &QAction::triggered, this, [this] {
        if (ui->actionStop->isEnabled()) return;
#ifdef Q_OS_WIN
        const QString executable = "IMSProg_database_update.exe";
#else
        const QString executable = "IMSProg_database_update";
#endif
        QString path = QDir(QCoreApplication::applicationDirPath()).filePath(executable);
        if (!QFileInfo::exists(path)) path = QStandardPaths::findExecutable(executable);
        if (path.isEmpty() || !QProcess::startDetached(path, QStringList()))
            QMessageBox::warning(this, tr("Error"), tr("Cannot start the chip database updater."));
        else
            QMessageBox::information(this, tr("Chip database"), tr("Complete the update in the updater window, then restart IMSProg to load the updated chip database."));
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
  if (!validateNandOperation(false, false)) return;
  //Reading data from chip
  newFileName = ui->comboBox_name->currentText();
  int res = 0;
  bool readCompleted = false;
  uint32_t numBlocks, step, sectorsPerBlock;
  defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
  if (!ensureClockTuned()) return;

  if (currentChipType != 1)
  {
      statusCH341 = ProgDeviceInit(current_programmer, currentChipType, defaultSpeed);
      qDebug() << "Interface speed" << defaultSpeed << " KHZ\n";
  }
  else
  {
      statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
      qDebug() << "Interface speed" << currentI2CBusSpeed << " KHZ\n";
  }

  if (statusCH341 == 0)
  {
    ui->crcEdit->setText("");
    if (((currentNumBlocks > 0) && (currentBlockSize >0) && (currentChipType == 0)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 1)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 2)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 3)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 4)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 5)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 6)))
    {
       doNotDisturb();
       ch341StatusFlashing();
       if (numberOfReads > 0)
       {
           oldChipData = chipData;
           oldFileName = ui->comboBox_name->currentText();
       }
       uint32_t addr = 0;
       uint32_t curBlock = 0;
       uint32_t k;
       switch (currentChipType)
          {
          case 0:             //SPI
             step = currentBlockSize;
             numBlocks = currentNumBlocks;
          break;
          case 1:             //I2C
             step = 128;
             //step = currentPageSize;
             numBlocks = currentChipSize / step;
          break;
          case 2:             //MicroWire
          case 3:             //25xxx
          case 4:             //95xxx
          case 5:             //45xx
             step = currentPageSize;
             numBlocks = currentChipSize / step;
          break;
          case 6:             //NAND 35xx, GD5xx, W25xx             
             numBlocks = currentChipSize / currentBlockSize;
             sectorsPerBlock = currentBlockSize / currentPageSize;
             if (nandRaw == true) step = currentBlockSize + currentECCsize * sectorsPerBlock;
             else step = currentBlockSize;
             if (!prepareNandOperation(tr("Read")))
                                {
                                    ProgDeviceClose(current_programmer);
                                    doNotDisturbCancel();
                                    clearCheckboxes();
                                    return;
                                }
          break;
          default:
             //Unsupport
             QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
             doNotDisturbCancel();
             ProgDeviceClose(current_programmer);
             ui->checkBox_3->setStyleSheet("");
       return;
          }
       if (currentChipType == 6)
       {
           // Geometry may have changed since the last buffer allocation.
           // Clear stale bytes so interrupted reads cannot mix two different dumps.
           chipData.resize(static_cast<int>(nandImageSize()));
           chipData.fill(char(0xff));
           nandReadIncomplete = true;
           filled = false;
           hexEdit->setData(chipData);
       }
       //progerssbar settings
       int currentPercent = 0;
       int lastPercent = -1;
       //ui->progressBar->setRange(0, static_cast<int>(numBlocks));
       ui->progressBar->setRange(0, 100);
       ui->progressBar->setValue(0);
       std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
       ui->pushButton->setStyleSheet(redKeyStyle);
       ui->statusMessage->setText(tr("Reading data from ") + ui->comboBox_name->currentText());
       opSpeedStart();
       for (k = 0; k < numBlocks; k++)
       {
           switch (currentChipType)
              {
              case 0:            //SPI
                 res = snor_read_param(buf.get(), curBlock * step, step, step, currentAddr4bit, current_programmer);
              break;
              case 1:            //I2C
                   if (current_programmer != 4) res = ch34xi2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm, current_programmer);
                   else res = ft232I2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm);
                   if (res==0) res = 1;
              break;
              case 2:
                 //MicroWire
                   if (current_programmer != 4) res = Read_EEPROM_3wire_param(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), static_cast<int>(currentChipSize), currentAlgorithm);
                   else res = ft232MWReadBlock(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), currentAlgorithm);
                   if (res==0) res = 1;
              break;
              case 3:
                 //25xxx
              case 4:
                 //95xxx
                 res = s95_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
              break;
              case 5:
                 //45xx
                 res = at45_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
              break;
           case 6:
              //NAND
               if (nandRaw == true) res = nand_block_read(buf.get(), currentPageSize + currentECCsize, k, sectorsPerBlock, current_programmer);
               else res = nand_block_read(buf.get(), currentPageSize, k, sectorsPerBlock, current_programmer);
              if (res==0) res = 1;
           break;
              default:
                 //Unsupport
                 QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                 doNotDisturbCancel();
                 ProgDeviceClose(current_programmer);
              return;
              }
          // if res=-1 - error, stop
          if (statusCH341 != 0)
            {
                showConnectionError();
                doNotDisturbCancel();
                ProgDeviceClose(current_programmer);
                return;
            }
          if (res <= 0)
            {
               if (currentChipType == 6)
               {
                   hexEdit->setData(chipData);
                   ui->label_nandInfo->setText(tr("Read failed at block %1 (error %2). Buffer is incomplete; unread bytes are FF.").arg(curBlock).arg(res));
                   qWarning().noquote() << ui->label_nandInfo->text();
               }
               QMessageBox::about(this, tr("Error"), tr("Error reading block ") + QString::number(curBlock));
               ProgDeviceClose(current_programmer);
               ui->pushButton->setStyleSheet(grnKeyStyle);
               doNotDisturbCancel();
               return;
            }
          memcpy(chipData.data() + addr, buf.get() + (addr - k * step), step);
          addr = addr + step;
          if (curBlock * step < 0x500) hexEdit->setData(chipData); //show buffer in hehedit while chip data is visible
          curBlock++;
          qApp->processEvents();
          //ui->progressBar->setValue(static_cast<int>(curBlock));
          opSpeedUpdate(curBlock, numBlocks, step);
          currentPercent = static_cast<int>((curBlock * 100.0) / numBlocks);
          if (currentPercent != lastPercent)
          {
              ui->progressBar->setValue(currentPercent);
              lastPercent = currentPercent;
          }
          if (isHalted)
          {
              isHalted = false;
              ProgDeviceClose(current_programmer);
              doNotDisturbCancel();
              return;
          }
       }
    }
    else
    {
       //Not correct Number found size of blocks
       ProgDeviceClose(current_programmer);
       if ((currentChipType == 0) || (currentChipType > 4))
            QMessageBox::about(this, tr("Error"), tr("Before reading from chip please press 'Detect' button."));
       else QMessageBox::about(this, tr("Error"), tr("Please select the chip parameters - manufacture and chip name"));
       return;
    }
    hexEdit->setData(chipData);
    nandReadIncomplete = false;
    readCompleted = true;
    opSpeedFinish();
    ui->progressBar->setValue(0);
    ui->pushButton->setStyleSheet(grnKeyStyle);
    ui->crcEdit->setText(getCRC32(chipData));
    newFileName = ui->comboBox_name->currentText();
  }
  else
  {
      ch341StatusFlashing();
      showConnectionError();
  }
  ProgDeviceClose(current_programmer);
  doNotDisturbCancel();
  filled = 0;
  numberOfReads++;
  if (readCompleted) finishNandRead(tr("Read complete"));
}

void MainWindow::on_pushButton_2_clicked()
{
    timer->stop();
    //Detect button - searching the connected chip in database
    tunedClock = 0;
    tunedKey.clear();
    applySpiClockChoice();
    u8 sr;
    statusCH341 = ProgDeviceInit(current_programmer, currentChipType,
        currentChipType == 1 ? currentI2CBusSpeed : getInterfaceSpeed(current_programmer, currentChipType, currentDelay));
    ch341StatusFlashing();
    if (statusCH341 != 0)
      {
        showConnectionError();
        timer->start();
        return;
      }

    ui->pushButton_2->setStyleSheet(redKeyStyle);
    ui->crcEdit->setText("");
    int i, index, idResult = 0;
    // print JEDEC info
    unsigned char bufid[5] = {0xff,0xff,0xff,0xff,0xff};
    if (currentChipType != 6)
    {
       idResult = snor_read_devid(bufid, 5, current_programmer);
    }
    else
    {
        idResult = nand_read_devid(bufid, 5, current_programmer);
    }

    if (idResult != 0)
    {
        QMessageBox::warning(this, tr("Error"), tr("NAND identification failed. Operation stopped."));
        ui->pushButton_2->setStyleSheet(grnKeyStyle);
        ProgDeviceClose(current_programmer);
        timer->start();
        return;
    }

    if ((bufid[0] == 0xff) && (bufid[1] == 0xff) && (bufid[2] == 0xff) && (currentChipType != 5))
    {
        QMessageBox::about(this, tr("Error"), tr("The chip is not connect or missing!"));
        ui->pushButton_2->setStyleSheet(grnKeyStyle);
        ProgDeviceClose(current_programmer);
        timer->start();
        return;
    }

    if (((bufid[0] == 0xff) || (bufid[0] == 0x00)) && (bufid[1] != 0xff)) ui->comboBox_type->setCurrentIndex(6);

    if ((currentChipType == 6) && ((bufid[0] == 0xff) ||(bufid[0] == 0x00)))
    {
        idResult = nand_read_devid(bufid, 5, current_programmer);
        if (idResult != 0)
        {
            QMessageBox::warning(this, tr("Error"), tr("NAND identification failed. Operation stopped."));
            ui->pushButton_2->setStyleSheet(grnKeyStyle);
            ProgDeviceClose(current_programmer);
            timer->start();
            return;
        }
    }

    ui->jedecEdit->setText(bytePrint(bufid[0]) + " " + bytePrint(bufid[1]) + " " + bytePrint(bufid[2]));
    for (i = 0; i< max_rec; i++)
    {
        if ((bufid[0] == chips[i].chipJedecIDMan) && (bufid[1] == chips[i].chipJedecIDDev) && (bufid[2] == chips[i].chipJedecIDCap))
        {
            index = ui->comboBox_type->findText(chips[i].chipTypeTxt);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_type->setCurrentIndex(index);
            }
            index = ui->comboBox_man->findText(chips[i].chipManuf);
            if ( index != -1 )
            { // -1 for not found
                ui->comboBox_man->setCurrentIndex(index);
            }
            index = ui->comboBox_name->findText(chips[i].chipName);
            if ( index != -1 )
            { // -1 for not found
                ui->comboBox_name->setCurrentIndex(index);
            }
            index = ui->comboBox_size->findData(chips[i].chipSize);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_size->setCurrentIndex(index);
            }
            index = ui->comboBox_page->findData(chips[i].sectorSize);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_page->setCurrentIndex(index);
            }
            index = ui->comboBox_block->findData(chips[i].blockSize);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_block->setCurrentIndex(index);
            }
            index = ui->comboBox_addr4bit->findData(chips[i].addr4bit);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_addr4bit->setCurrentIndex(index);
            }
            index = ui->comboBox_vcc->findText(chips[i].chipVCC);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_vcc->setCurrentIndex(index);
            }
            index = ui->comboBox_ECC->findData(chips[i].eepromPages);
            if ( index != -1 )
            { // -1 for not found
               ui->comboBox_ECC->setCurrentIndex(index);
            }
            ui->pushButton_2->setStyleSheet(grnKeyStyle);
            break;
        }
    }

    // 45xxx without JEDEC info
    if ((currentChipType == 5) && (bufid[0] == 0xff) && (bufid[1] == 0xff) && (bufid[2] == 0xff) )
    {
        //calculating capacity
        int capacityIndex = 0;
        if (!at45_read_sr(&sr))
        {
            if ((sr & 0xC0) == 0x80) //45xx chip found
            {
                sr = sr & 0x38;
                switch (sr)
                   {
                   case 0x08:
                      //132K
                      capacityIndex = 1;
                   break;
                   case 0x10:
                      //264K
                      capacityIndex = 2;
                   break;
                   case 0x18:
                      //528K
                      capacityIndex = 3;
                   break;
                   case 0x20:
                      //1056K
                      capacityIndex = 4;
                   break;
                   case 0x28:
                      //2112K
                      capacityIndex = 5;
                   break;
                   case 0x30:
                      //4224K
                      capacityIndex = 6;
                   break;
                   }
                ui->comboBox_size->setCurrentIndex(capacityIndex);
                for (i = 0; i< max_rec; i++)
                {
                    if ((chips[i].chipTypeHex == 0x05) && (chips[i].chipSize == ui->comboBox_size->currentData().toUInt()))
                    {
                       if (!QString::compare(chips[i].chipManuf, "ATMEL", Qt::CaseInsensitive))
                       {
                           index = ui->comboBox_type->findText(chips[i].chipTypeTxt);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_type->setCurrentIndex(index);
                           }
                           index = ui->comboBox_man->findText(chips[i].chipManuf);
                           if ( index != -1 )
                           { // -1 for not found
                               ui->comboBox_man->setCurrentIndex(index);
                           }
                           index = ui->comboBox_name->findText(chips[i].chipName);
                           if ( index != -1 )
                           { // -1 for not found
                               ui->comboBox_name->setCurrentIndex(index);
                           }
                           index = ui->comboBox_size->findData(chips[i].chipSize);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_size->setCurrentIndex(index);
                           }
                           index = ui->comboBox_page->findData(chips[i].sectorSize);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_page->setCurrentIndex(index);
                           }
                           index = ui->comboBox_block->findData(chips[i].blockSize);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_block->setCurrentIndex(index);
                           }
                           index = ui->comboBox_addr4bit->findData(chips[i].addr4bit);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_addr4bit->setCurrentIndex(index);
                           }
                           index = ui->comboBox_vcc->findText(chips[i].chipVCC);
                           if ( index != -1 )
                           { // -1 for not found
                              ui->comboBox_vcc->setCurrentIndex(index);
                           }

                           ui->pushButton_2->setStyleSheet(grnKeyStyle);
                           break;
                       }
                    }
                }
            }
        }

    }

    if ((currentChipType == 5) && (ui->comboBox_size->currentIndex() > 0))
    {
        //calculate buffer size
        if (!at45_read_sr(&sr))
        {
            if ((sr & 0x01) == 1)
            {
                if ( ui->comboBox_page->currentData() == 264)
                {
                    index = ui->comboBox_page->findData(256);
                    if ( index != -1 )
                    { // -1 for not found
                       ui->comboBox_page->setCurrentIndex(index);
                    }
                }
                if ( ui->comboBox_page->currentData() == 528)
                {
                    index = ui->comboBox_page->findData(512);
                    if ( index != -1 )
                    { // -1 for not found
                       ui->comboBox_page->setCurrentIndex(index);
                    }
                }
            }
        }
    }

    ui->pushButton_2->setStyleSheet(grnKeyStyle);
    ui->crcEdit->setText(getCRC32(chipData));
    ProgDeviceClose(current_programmer);
    timer->start();
}

void MainWindow::on_comboBox_size_currentIndexChanged(int index)
{
    currentChipSize = ui->comboBox_size->currentData().toUInt();
    currentBlockSize = ui->comboBox_block->currentData().toUInt();
    currentPageSize = ui->comboBox_page->currentData().toUInt();
    currentAddr4bit = ui->comboBox_addr4bit->currentData().toUInt();
    currentECCsize = (ui->comboBox_ECC->currentData().toUInt()) * 64;
    if ((currentChipSize !=0) && (currentBlockSize!=0) && (currentChipType == 0))
    {
        currentNumBlocks = currentChipSize / currentBlockSize;
        preparingToCompare(1);
        numberOfReads = 0;
        chipData.resize(static_cast<int>(currentChipSize));
        chipData.fill(char(0xff));
        filled = 1;
        hexEdit->setData(chipData);
    }
    if ((currentChipSize !=0) && (currentPageSize!=0)  && (currentChipType > 0))
    {
    currentNumBlocks = currentChipSize / currentPageSize;
    preparingToCompare(1);
    numberOfReads = 0;
    if ((currentChipType == 6) && (nandRaw == true)) chipData.resize(static_cast<int>(currentChipSize + currentChipSize/currentPageSize * currentECCsize));
    else chipData.resize(static_cast<int>(currentChipSize));
    chipData.fill(char(0xff));
    filled = 1;
    nandReadIncomplete = false;
    hexEdit->setData(chipData);
    }
    updateNandSelectionInfo();
    index = index + 0;
}

void MainWindow::on_comboBox_page_currentIndexChanged(int index)
{
    currentChipSize = ui->comboBox_size->currentData().toUInt();
    currentBlockSize = ui->comboBox_block->currentData().toUInt();
    currentPageSize = ui->comboBox_page->currentData().toUInt();
    currentAddr4bit = ui->comboBox_addr4bit->currentData().toUInt();
    currentECCsize = (ui->comboBox_ECC->currentData().toUInt()) * 64;
    if ((currentChipSize !=0) && (currentBlockSize!=0) && (currentChipType ==0))
    {
        currentNumBlocks = currentChipSize / currentBlockSize;
    }
    if ((currentChipSize !=0) && (currentPageSize!=0)  && (currentChipType > 0))
    {
    currentNumBlocks = currentChipSize / currentPageSize;
    }
    updateNandSelectionInfo();
    index = index + 0;
}

void MainWindow::on_actionDetect_triggered()
{
   MainWindow::on_pushButton_2_clicked();
}

void MainWindow::on_actionSave_triggered()
{
    if (nandReadIncomplete && QMessageBox::warning(this, tr("Incomplete NAND read"),
            tr("The last NAND read did not complete. Unread bytes are FF. Save this incomplete buffer anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    lastDirectory.replace(".cap", ".bin");
    lastDirectory.replace(".CAP", ".bin");
    lastDirectory.replace(".hex", ".bin");
    lastDirectory.replace(".HEX", ".bin");

    ui->statusMessage->setText(tr("Saving file"));
    fileName = QFileDialog::getSaveFileName(this,
                                QString(tr("Save file")),
                                lastDirectory,
                                tr("Data Images") + " (*.bin *.BIN);;" + tr("All files") + " (*.*)");
    if (fileName.isEmpty()) return;
    QFileInfo info(fileName);
    lastDirectory = info.filePath();

    if (QString::compare(info.suffix(), "bin", Qt::CaseInsensitive)) fileName = fileName + ".bin";
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::about(this, tr("Error"), tr("Error saving file!"));
        return;
    }
    file.write(hexEdit->data());
    file.close();
    ui->statusMessage->setText("");
}

void MainWindow::on_actionErase_triggered()
{
    lastOperationSucceeded = false;
    if (!validateNandOperation(false, false)) return;
    if (!ensureClockTuned()) return;
    if (currentChipType == 6 && QMessageBox::warning(this, tr("Erase NAND"),
            tr("This erases the whole selected NAND chip, including main data and OOB. It is not a selected-block repair. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    //statusCH341 = ch341a_spi_init();
    int ret;
    uint32_t curBlock, numBlocks, step;

    if (currentChipType != 1)
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, defaultSpeed);
        qDebug() << "Interface speed" << defaultSpeed << " KHZ\n";
    }
    else
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
        qDebug() << "Interface speed" << currentI2CBusSpeed << " KHZ\n";
    }

    if (statusCH341 != 0)
      {
        showConnectionError();
        return;
      }
    ui->statusMessage->setText(tr("Erasing the ") + ui->comboBox_name->currentText());
    ui->checkBox->setStyleSheet("QCheckBox{font-weight:600;}");
    ui->centralWidget->repaint();
    ui->progressBar->setRange(0, 100);
    doNotDisturb();
    if (currentChipType == 0)
    {
       snorUnprotect(current_programmer);
       if (currentNumBlocks > 0)
       {
           ui->progressBar->setRange(0, static_cast<int>(currentNumBlocks));
           for (uint32_t curBlock = 0; curBlock < currentNumBlocks; curBlock++)
           {
               //config_stream(2);
               ret = snor_block_erase( curBlock,  currentBlockSize, static_cast<u8>(currentAddr4bit), current_programmer);
               if (ret != 0)
                 {
                   QMessageBox::about(this, tr("Error"), tr("Error erasing sector ") + QString::number(curBlock));
                   ProgDeviceClose(current_programmer);
                   doNotDisturbCancel();
                   clearCheckboxes();
                   return;
                 }
               qApp->processEvents();
               ui->progressBar->setValue( static_cast<int>(curBlock));
               if (isHalted)
               {
                   isHalted = false;
                   ProgDeviceClose(current_programmer);
                   doNotDisturbCancel();
                   return;
               }
           }
       }

    }
    if ((currentChipType == 4) || ((currentChipType == 3) && ((currentAlgorithm & 0x20) == 0)))
    {
        uint32_t k;
        int res = 0;
        step = currentPageSize;
        numBlocks = currentChipSize / step;
        std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
        config_stream(2);
        ui->progressBar->setRange(0, static_cast<int>(numBlocks));
        for (k = 0; k < step; k++)
        {
            buf[k] = 0xff;
        }
        for (curBlock = 0; curBlock < currentNumBlocks; curBlock++)
        {
            res =  s95_write_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
            qApp->processEvents();
            ui->progressBar->setValue( static_cast<int>(curBlock));
        if (isHalted)
        {
            isHalted = false;
            ProgDeviceClose(current_programmer);
            doNotDisturbCancel();
            return;
        }
            if (res <= 0)
              {
                QMessageBox::about(this, tr("Error"), tr("Error erasing sector ") + QString::number(curBlock));
                ProgDeviceClose(current_programmer);
                doNotDisturbCancel();
                clearCheckboxes();
                return;
              }
        }
    }
    if ((currentChipType == 3) && ((currentAlgorithm & 0x20) > 0))
    {
        ui->progressBar->setValue(50);
        s95_full_erase(current_programmer);
        imsprog_sleep(1);
    }
    if (currentChipType == 2)
    {
        config_stream(1);
        mw_gpio_init();
        ui->progressBar->setValue(50);
        if (current_programmer != 4) Erase_EEPROM_3wire_param(currentAlgorithm);
        else ft232hMWEraseAll(currentAlgorithm);
        imsprog_sleep(1);
    }
    if (currentChipType == 1)
    {
        uint32_t curBlock = 0;
        uint32_t k;
        int res = 0;
        step = 128;
        //step = currentPageSize;
        numBlocks = currentChipSize / step;
        std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
        config_stream(2);
        ui->progressBar->setRange(0, static_cast<int>(numBlocks));
        for (k = 0; k < step; k++)
        {
            buf[k] = 0xff;
        }
        for (curBlock = 0; curBlock < numBlocks; curBlock++)
        {
            if (current_programmer != 4) res = ch34xi2cBlockWrite(buf.get(), curBlock * step, step, currentPageSize, currentAlgorithm, current_programmer);
            else res = ft232I2cBlockWrite(buf.get(), curBlock * step, step, currentPageSize, currentAlgorithm);
            if (res==0) res = 1;
            qApp->processEvents();
            ui->progressBar->setValue( static_cast<int>(curBlock));
        if (isHalted)
        {
            isHalted = false;
            ProgDeviceClose(current_programmer);
            doNotDisturbCancel();
            return;
        }
            if (res <= 0)
              {
                QMessageBox::about(this, tr("Error"), tr("Error erasing sector ") + QString::number(curBlock));
                ProgDeviceClose(current_programmer);
                doNotDisturbCancel();
                clearCheckboxes();
                return;
              }
        }

    }
    if ((currentChipType == 5) && ((currentAlgorithm & 0x10) > 0))
    {
        ui->progressBar->setValue(50);
        at45_full_erase(current_programmer);
        imsprog_sleep(1);
    }
    if ((currentChipType == 5) && ((currentAlgorithm & 0x10) == 0))
    {
        numBlocks = currentChipSize / currentPageSize / 8;
        ui->progressBar->setRange(0, static_cast<int>(numBlocks));
        uint32_t curBlock = 0;
        for (curBlock = 0; curBlock < numBlocks; curBlock++)
        {
            at45_sector_erase(curBlock,  currentPageSize, current_programmer);
            ui->progressBar->setValue(static_cast<int>(curBlock));
            if (isHalted)
            {
               isHalted = false;
               ProgDeviceClose(current_programmer);
               doNotDisturbCancel();
               return;
            }
        }
    }
    if (currentChipType == 6)
    {
       if (!prepareNandOperation(tr("Erase")))
       {
           ProgDeviceClose(current_programmer);
           doNotDisturbCancel();
           clearCheckboxes();
           return;
       }
       uint32_t sectInBlock;
       int retval;
       numBlocks = currentChipSize / currentBlockSize;
       sectInBlock = currentBlockSize / currentPageSize;
       if (numBlocks > 0)
       {
           ui->progressBar->setRange(0, static_cast<int>(numBlocks));
           for (uint32_t curBlock = 0; curBlock < numBlocks; curBlock++)
           {
               if ((nandSettings & 0xf0) == 0x00)
               {
                   retval = nand_checkBadBlock(static_cast<uint32_t>(curBlock), static_cast<uint32_t>(currentPageSize), static_cast<uint32_t>(sectInBlock), current_programmer);
               }
               else retval = 0;
               if (retval < 0)
               {
                   QMessageBox::warning(this, tr("Error"), tr("Bad-block check failed. Erase stopped."));
                   ProgDeviceClose(current_programmer);
                   doNotDisturbCancel();
                   clearCheckboxes();
                   return;
               }

               if (retval == 0) //skip bad sectors, if checkbox enable
               {
                   ret = nand_block_erase(curBlock, currentBlockSize / currentPageSize, current_programmer);
                   if (ret != 0)
                     {
                       QMessageBox::about(this, tr("Error"), tr("Error erasing sector ") + QString::number(curBlock));
                       ProgDeviceClose(current_programmer);
                       doNotDisturbCancel();
                       clearCheckboxes();
                       return;
                     }
               }
               qApp->processEvents();
               ui->progressBar->setValue( static_cast<int>(curBlock));
               if (isHalted)
               {
                   isHalted = false;
                   ProgDeviceClose(current_programmer);
                   doNotDisturbCancel();
                   return;
               }
           }
       }

    }

    lastOperationSucceeded = true;
    doNotDisturbCancel();
    ui->checkBox->setStyleSheet("");
    ui->statusMessage->setText("");
    ui->progressBar->setValue(0);
    ui->centralWidget->repaint();
    ProgDeviceClose(current_programmer);
}

void MainWindow::on_actionUndo_triggered()
{
    hexEdit->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    hexEdit->redo();
}

void MainWindow::on_actionOpen_triggered()
{
    QByteArray buf;
    ui->statusMessage->setText(tr("Opening file"));
    if (numberOfReads == 0) oldFileName = fileName;
    else oldFileName = ui->comboBox_name->currentText();
    if (!cmdStarted)
    {
        fileName = QFileDialog::getOpenFileName(this,
                                    QString(tr("Open file")),
                                    lastDirectory,
                                    tr("Data Images") + " (*.bin *.BIN *.rom *.ROM);;" + tr("All files") + " (*.*)");
    }
   else fileName = lastDirectory;
   cmdStarted = false;
    if (fileName.isEmpty())
    {
        ui->statusMessage->setText("");
        return;
    }

    QFileInfo info(fileName);
    uint32_t arraySize;
    lastDirectory = info.filePath();
    // if ChipSze = 0 (Chip is not selected) IMSProg using at hexeditor only. chipsize -> hexedit.data
    // if ChipSize < FileSize - showing error message
    // if Filesize <= ChipSize - filling fileArray to hexedit.Data, the end of the array chipData remains filled 0xff
    QFile file(fileName);
    ui->statusMessage->setText("");

    if (currentChipType == 6) arraySize = static_cast<uint32_t>(nandImageSize());
    else arraySize = currentChipSize;

    if ((info.size() > arraySize) && (currentChipSize != 0))
    {
      QMessageBox::about(this, tr("Error"), tr("The file size exceeds the chip size. Please select another chip or file or use `Save part` to split the file."));
      return;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    ui->statusMessage->setText(tr("Current file: ") + info.fileName());
    preparingToCompare(0);
    filled = 0;
    nandReadIncomplete = false;
    buf = file.readAll();
    if (currentChipSize == 0)
    {
        chipData.resize(static_cast<int>(info.size()));
    }

    if (currentChipType == 6)
        chipData = buf; // Keep the exact file length; never silently pad/reuse an old NAND tail.
    else
        chipData.replace(0, static_cast<int>(info.size()), buf);
    hexEdit->setData(chipData);

    file.close();

    ui->crcEdit->setText(getCRC32(chipData));
}

void MainWindow::on_actionExtract_from_ASUS_CAP_triggered()
{
    QByteArray buf;
    ui->statusMessage->setText(tr("Opening file"));
    if (numberOfReads == 0) oldFileName = fileName;
    else oldFileName = ui->comboBox_name->currentText();
    fileName = QFileDialog::getOpenFileName(this,
                                QString(tr("Open file")),
                                lastDirectory,
                                "ASUS Data Images (*.cap *.CAP);;All files (*.*)");
    ui->statusMessage->setText("");
    if (fileName.isEmpty()) return;
    QFileInfo info(fileName);
    lastDirectory = info.filePath();
    if (info.size() < 0x800) return;   // shorter than the CAP header: nothing to extract
    if ((info.size() - 0x800 > currentChipSize) && (currentChipSize != 0))
    {
      QMessageBox::about(this, tr("Error"), tr("The file size exceeds the chip size. Please select another chip or file or use `Save part` to split the file."));
      return;
    }
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
    {

        return;
    }
    ui->statusMessage->setText(tr("Current file: ") + info.fileName());
    preparingToCompare(0);
    filled = 0;
    buf.resize(int(info.size()));
    buf = file.readAll();
    file.close();
    fileName.clear();
    buf.remove(0,0x800);
    if (currentChipSize == 0)
    {
        chipData.resize(static_cast<int>(info.size()) - 0x800);
    }
    chipData.replace(0, static_cast<int>(info.size()) - 0x800, buf);
    hexEdit->setData(chipData);
    ui->crcEdit->setText(getCRC32(chipData));
}

void MainWindow::on_actionWrite_triggered()
{
    lastOperationSucceeded = false;
    if (!validateNandOperation(true, true)) return;
    if (!nandWriteModeConfirmed && !confirmNandWriteMode()) return;
    if (!ensureClockTuned()) return;
    //Writting data to chip
    int res = 0, badResult = 0;
    uint32_t numBlocks, step, sectorsPerBlock;

    if (currentChipType != 1)
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, defaultSpeed);
        qDebug() << "Interface speed" << defaultSpeed << " KHZ\n";
    }
    else
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
        qDebug() << "Interface speed" << currentI2CBusSpeed << " KHZ\n";
    }

    if (statusCH341 == 0)
    {
    chipData = hexEdit->data();
    if (((currentNumBlocks > 0) && (currentBlockSize >0) && (currentChipType == 0)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 1)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 2)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 3)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 4)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 5)) ||
         ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 6)))
        {
        doNotDisturb();
        switch (currentChipType)
                      {
                      case 0:                 //SPI
                         step = currentBlockSize;
                         numBlocks = currentNumBlocks;
                         snorUnprotect(current_programmer);
                      break;
                      case 1:                 //I2C
                         step = 128;
                         //step = currentPageSize;
                         numBlocks = currentChipSize / step;
                      break;
                      case 2:                 //MicroWire
                      case 3:                 //25xxx
                      case 4:                 //95xxx
                      case 5:                 //45xx
                         step = currentPageSize;
                         numBlocks = currentChipSize / step;
                      break;
                      case 6:             //NAND 35xx, GD5xx, W25xx
                         numBlocks = currentChipSize / currentBlockSize;
                         sectorsPerBlock = currentBlockSize / currentPageSize;
                         step = currentBlockSize + (nandRaw ? currentECCsize * sectorsPerBlock : 0);
                         if (!prepareNandOperation(tr("Write")) || nand_unprotect(current_programmer) != 0)
                         {
                             QMessageBox::warning(this, tr("Error"), tr("NAND setup failed. No data was programmed."));
                             ProgDeviceClose(current_programmer);
                             doNotDisturbCancel();
                             clearCheckboxes();
                             return;
                         }
                      break;
                      default:
                         //Unsupport
                         QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                         doNotDisturbCancel();
                         ProgDeviceClose(current_programmer);
                         ui->checkBox_2->setStyleSheet("");
                      return;
                      }
    ch341StatusFlashing();
    uint32_t addrSrc = 0, addrDest = 0;
    uint32_t curBlock = 0;
    uint32_t k;
    ui->statusMessage->setText(tr("Writing data to ") + ui->comboBox_name->currentText());
    opSpeedStart();
    //progerssbar settings
    int currentPercent = 0;
    int lastPercent = -1;
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->checkBox_2->setStyleSheet("QCheckBox{font-weight:800;}");
    chipData = hexEdit->data();
    if (currentChipType != 6 && quint64(chipData.size()) < quint64(numBlocks) * step)
    {
        // Bytes were removed in the editor: programming would read past the buffer.
        QMessageBox::warning(this, tr("Error"), tr("The buffer is smaller than the chip. Reload the image or select the chip again."));
        opSpeedFinish();
        ProgDeviceClose(current_programmer);
        doNotDisturbCancel();
        clearCheckboxes();
        return;
    }
    std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
    for (k = 0; k < numBlocks; k++)
      {
         memcpy(buf.get() + addrDest - k * step, chipData.constData() + addrSrc, step);
         switch (currentChipType)
                       {
                       case 0:                           //SPI
                          addrSrc = addrSrc + step;
                          res = snor_write_param(buf.get(), addrDest, step, step, currentAddr4bit, current_programmer);

                       break;
                       case 1:                           //I2C
                          addrSrc = addrSrc + step;
                           if (current_programmer != 4) res = ch34xi2cBlockWrite(buf.get(), curBlock * step, step, currentPageSize, currentAlgorithm, current_programmer);
                           else res = ft232I2cBlockWrite(buf.get(), curBlock * step, step, currentPageSize, currentAlgorithm);
                          if (res==0) res = 1;
                       break;
                       case 2:                           //MicroWire
                          addrSrc = addrSrc + step;
                          if (current_programmer != 4) res = Write_EEPROM_3wire_param(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), static_cast<int>(currentChipSize), currentAlgorithm);
                          else res = ft232MWWriteBlock(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), currentAlgorithm);
                          if (res==0) res = 1;
                       break;
                       case 3:                           //25xxx
                       case 4:                           //M95xx
                          addrSrc = addrSrc + step;
                          res = s95_write_param(buf.get(), addrDest, step, step, currentAlgorithm, current_programmer);
                       break;
                       case 5:
                          //AT45DBxx
                          addrSrc = addrSrc + step;
                          res = at45_write_param(buf.get(), addrDest, step, step, currentAlgorithm, current_programmer);
                       break;
                       case 6:
                          //NAND
                          if ((nandSettings & 0x0f) == 0x01)
                          {
                              badResult = nand_checkBadBlock(k, currentPageSize, sectorsPerBlock, current_programmer);
                              if (badResult < 0) { res = badResult; break; }
                              if (badResult == 1)
                              {
                                  // Preserve physical addresses for main+OOB images.
                                  // Main-only retains the upstream logical skip/compact policy.
                                  if (nandRaw) addrSrc += step;
                                  res = 1;
                                  break;
                              }
                          }
                          res = nand_block_write(buf.get(), currentPageSize + (nandRaw ? currentECCsize : 0), k, sectorsPerBlock, current_programmer);
                          if (res == 0) { addrSrc += step; res = 1; }
                       break;
                       default:
                          //Unsupport
                          QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                          doNotDisturbCancel();
                          ProgDeviceClose(current_programmer);
                          ui->checkBox_2->setStyleSheet("");
                       return;
                       }
         // if res=-1 - error, stop
         if (statusCH341 != 0)
           {
             showConnectionError();
             doNotDisturbCancel();
             ProgDeviceClose(current_programmer);
             return;
           }
         if (res <= 0)
           {
             QMessageBox::about(this, tr("Error"), tr("Error writing sector ") + QString::number(curBlock));
             doNotDisturbCancel();
             clearCheckboxes();
             ProgDeviceClose(current_programmer);
             return;
           }
         addrDest = addrDest + step;
         curBlock++;
         qApp->processEvents();
         if (isHalted)
         {
             isHalted = false;
             ProgDeviceClose(current_programmer);
             doNotDisturbCancel();
             return;
         }
            opSpeedUpdate(curBlock, numBlocks, step);
            currentPercent = static_cast<int>((curBlock * 100.0) / numBlocks);
            if (currentPercent != lastPercent)
            {
                ui->progressBar->setValue(currentPercent);
                lastPercent = currentPercent;
            }
      }
      if (currentChipType == 6 && quint64(addrSrc) != nandImageSize())
      {
          QMessageBox::warning(this, tr("Incomplete NAND write"),
              tr("Bad-block skipping exhausted chip capacity before all source bytes were programmed. Some blocks were already written."));
          ProgDeviceClose(current_programmer);
          doNotDisturbCancel();
          clearCheckboxes();
          return;
      }
      lastOperationSucceeded = true;
    }
    else
    {
    //Not correct Number fnd size of blocks
     QMessageBox::about(this, tr("Error"), tr("Before reading from chip please press 'Detect' button."));
    }
    doNotDisturbCancel();
    ui->progressBar->setValue(0);
    ui->checkBox_2->setStyleSheet("");
    opSpeedFinish();
    }
    else
    {
        ch341StatusFlashing();
        showConnectionError();
    }
    ProgDeviceClose(current_programmer);
}

void MainWindow::on_actionRead_triggered()
{
    MainWindow::on_pushButton_clicked();
}

void MainWindow::on_actionExit_triggered()
{
    MainWindow::close();
}

void MainWindow::on_comboBox_man_currentIndexChanged(int index)
{
    int i, index2;
    QString txt="";
    if (max_rec > 0)
    {
       txt = ui->comboBox_man->currentText().toUtf8();
       ui->comboBox_name->clear();
       ui->comboBox_name->addItem("");
       for (i = 0; i<max_rec; i++)
       {
           //replacing items to combobox chip Name
           if (txt.compare(chips[i].chipManuf)==0 && (currentChipType == chips[i].chipTypeHex))
           {
           index2 = ui->comboBox_name->findText(chips[i].chipName);
                    if ( index2 == -1 ) ui->comboBox_name->addItem(chips[i].chipName);
           }
       }
        ui->comboBox_name->setCurrentIndex(0);
        ui->comboBox_vcc->setCurrentIndex(0);
        ui->comboBox_page->setCurrentIndex(0);
        ui->comboBox_block->setCurrentIndex(0);
        ui->comboBox_size->setCurrentIndex(0);
        ui->comboBox_addr4bit->setCurrentIndex(0);
        ui->comboBox_ECC->setCurrentIndex(0);
        ui->statusMessage->setText("");
   }
 index = index + 0;
}

void MainWindow::on_comboBox_name_currentTextChanged(const QString &arg1)
{
    int i, index;
    oldFileName = fileName;
    QString manName = ui->comboBox_man->currentText();
    if (arg1.compare("") !=0)
    {

       for (i = 0; i < max_rec; i++)
       {
           if ((manName.compare(chips[i].chipManuf)==0) && (arg1.compare(chips[i].chipName)==0))
           {
               index = ui->comboBox_size->findData(chips[i].chipSize);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_size->setCurrentIndex(index);
               }
               index = ui->comboBox_page->findData(chips[i].sectorSize);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_page->setCurrentIndex(index);
               }
               index = ui->comboBox_block->findData(chips[i].blockSize);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_block->setCurrentIndex(index);
               }
               index = ui->comboBox_addr4bit->findData(chips[i].addr4bit);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_addr4bit->setCurrentIndex(index);
               }
               index = ui->comboBox_vcc->findText(chips[i].chipVCC);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_vcc->setCurrentIndex(index);
               }
               index = ui->comboBox_ECC->findData(chips[i].eepromPages);
               if ( index != -1 )
               { // -1 for not found
                  ui->comboBox_ECC->setCurrentIndex(index);
               }
               currentAlgorithm = chips[i].algorithmCode;
               currentDelay = chips[i].delay;

               defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
               index = ui->comboBox_i2cSpeed->findData(defaultSpeed);
               if ( index != -1 )
               { // -1 for not found
                   ui->comboBox_i2cSpeed->setCurrentIndex(index);
               }
           }
       }
       currentChipSize = ui->comboBox_size->currentData().toUInt();
       currentBlockSize = ui->comboBox_block->currentData().toUInt();
       currentPageSize = ui->comboBox_page->currentData().toUInt();
       currentAddr4bit = ui->comboBox_addr4bit->currentData().toUInt();
       currentECCsize = (ui->comboBox_ECC->currentData().toUInt()) * 64;
       preparingToCompare(1);

       if ((currentChipSize !=0) && (currentBlockSize!=0) && (currentChipType == 0))
       {
           currentNumBlocks = currentChipSize / currentBlockSize;
           chipData.resize(static_cast<int>(currentChipSize));
           chipData.fill(char(0xff));
           filled = 1;
           hexEdit->setData(chipData);
       }
       if ((currentChipSize !=0) && (currentPageSize!=0)  && (currentChipType > 0) &&(currentChipType != 6))
       {
           currentNumBlocks = currentChipSize / currentPageSize;
           chipData.resize(static_cast<int>(currentChipSize));
           chipData.fill(char(0xff));
           filled = 1;
           hexEdit->setData(chipData);
       }
       if ((currentChipSize !=0) && (currentPageSize!=0)  && (currentChipType == 6))
       {
           currentNumBlocks = currentChipSize / currentPageSize;
           if (nandRaw == true) chipData.resize(static_cast<int>(currentChipSize + currentChipSize/currentPageSize * currentECCsize));
           else chipData.resize(static_cast<int>(currentChipSize));
           chipData.fill(char(0xff));
           filled = 1;
           hexEdit->setData(chipData);
       }

    }
}

void MainWindow::on_actionVerify_triggered()
{
    lastOperationSucceeded = false;
    if (!validateNandOperation(false, true)) return;
    if (!ensureClockTuned()) return;
    //Reading and veryfying data from chip
    int res = 0;
    uint32_t step, numBlocks, sectorsPerBlock;
    // A mismatch is re-checked at lower clocks before it is reported,
    // so a marginal read clock is not mistaken for a failed write.
    const uint16_t verifyStartClock = currentChipType != 1 ? defaultSpeed : currentI2CBusSpeed;
    uint16_t verifyClock = verifyStartClock;
    uint32_t verifyRereadBlocks = 0;
    QStringList verifySkippedBadBlocks;
    // Main-only Write with bad-block skipping writes the data of a skipped
    // block into the next good block (see on_actionWrite_triggered()).
    const bool verifyCompactsAfterBad = currentChipType == 6 && !nandRaw && (nandSettings & 0x0f) == 0x01;
    //statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);//ch341a_init(currentChipType, currentI2CBusSpeed);

    if (currentChipType != 1)
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, defaultSpeed);
        qDebug() << "Interface speed" << defaultSpeed << " KHZ\n";
    }
    else
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
        qDebug() << "Interface speed" << currentI2CBusSpeed << " KHZ\n";
    }

    if (statusCH341 == 0)
    {
       if (((currentNumBlocks > 0) && (currentBlockSize >0) && (currentChipType == 0)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 1)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 2)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 3)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 4)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 5)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 6)))
           {
               ui->crcEdit->setText("");
               doNotDisturb();
               switch (currentChipType)
                             {
                             case 0:                 //SPI
                                step = currentBlockSize;
                                numBlocks = currentNumBlocks;
                             break;
                             case 1:                 //I2C
                                //step = currentPageSize;
                                step = 128;
                                numBlocks = currentChipSize / step;
                             break;
                             case 2:                 //MicroWire
                             case 3:                 //25xxx
                             case 4:                 //95xxx
                             case 5:                 //45xx
                                step = currentPageSize;
                                numBlocks = currentChipSize / step;
                             break;
                             case 6:             //NAND 35xx, GD5xx, W25xx
                                numBlocks = currentChipSize / currentBlockSize;
                                sectorsPerBlock = currentBlockSize / currentPageSize;
                                if (nandRaw == true) step = currentBlockSize + currentECCsize * sectorsPerBlock;
                                else step = currentBlockSize;
                                if (!prepareNandOperation(tr("Verify")))
                                {
                                    ProgDeviceClose(current_programmer);
                                    doNotDisturbCancel();
                                    clearCheckboxes();
                                    return;
                                }
                             break;
                             default:
                                //Unsupport
                                QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                                doNotDisturbCancel();
                                ProgDeviceClose(current_programmer);
                                ui->checkBox_3->setStyleSheet("");
                             return;
                             }
               ch341StatusFlashing();
               uint32_t addr = 0;
               uint32_t curBlock = 0;
               uint32_t j, k;
               //progerssbar settings
               int currentPercent = 0;
               int lastPercent = -1;
               ui->progressBar->setRange(0, 100);
               ui->progressBar->setValue(0);
               std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
               chipData = hexEdit->data();
               if (currentChipType != 6 && quint64(chipData.size()) < quint64(numBlocks) * step)
               {
                   // Bytes were removed in the editor: comparing would read past the buffer.
                   QMessageBox::warning(this, tr("Error"), tr("The buffer is smaller than the chip. Reload the image or select the chip again."));
                   ui->checkBox_3->setStyleSheet("");
                   ProgDeviceClose(current_programmer);
                   doNotDisturbCancel();
                   clearCheckboxes();
                   return;
               }
               ui->checkBox_3->setStyleSheet("QCheckBox{font-weight:800;}");
               ui->statusMessage->setText(tr("Veryfing data from ") + ui->comboBox_name->currentText());
               opSpeedStart();
               for (k = 0; k < numBlocks; k++)
               {
                   // Blocks marked bad are left alone by Erase (and by Write when
                   // skipping is on), so their contents cannot match the image.
                   if (currentChipType == 6 && sectorsPerBlock &&
                       ((nandSettings & 0xf0) == 0x00 || (nandSettings & 0x0f) == 0x01))
                   {
                       const int bad = nand_checkBadBlock(k, currentPageSize, sectorsPerBlock, current_programmer);
                       if (bad < 0)
                       {
                           QMessageBox::warning(this, tr("Error"), tr("Bad-block check failed. Verify stopped."));
                           ui->checkBox_3->setStyleSheet("");
                           ProgDeviceClose(current_programmer);
                           doNotDisturbCancel();
                           clearCheckboxes();
                           return;
                       }
                       // A main + OOB image that itself carries a non-FF marker for
                       // this block (host ECC layouts keep data there) was written
                       // like any other block, so it is compared, not skipped.
                       const bool imageUsesMarker = nandRaw &&
                           static_cast<uint8_t>(chipData.at(static_cast<int>(addr + currentPageSize))) != 0xff;
                       if (bad == 1 && !imageUsesMarker)
                       {
                           verifySkippedBadBlocks << QString::number(k);
                           // Main-only Write with skipping moves the data to the
                           // next good block, so the image position stays put.
                           if (!verifyCompactsAfterBad) addr = addr + step;
                           curBlock++;
                           continue;
                       }
                   }
                   switch (currentChipType)
                      {
                      case 0:
                         //SPI
                         res = snor_read_param(buf.get(),curBlock * step, step, step, currentAddr4bit, current_programmer);
                      break;
                      case 1:
                         //I2C
                          if (current_programmer != 4) res = ch34xi2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm, current_programmer);
                          else res = ft232I2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm);
                         if (res==0) res = 1;
                      break;
                      case 2:
                         //MicroWire
                         if (current_programmer != 4) res = Read_EEPROM_3wire_param(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), static_cast<int>(currentChipSize), currentAlgorithm);
                         else res = ft232MWReadBlock(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), currentAlgorithm);
                         if (res==0) res = 1;
                      break;
                      case 3:
                         //25xxx
                      case 4:
                         //95xxx
                         res = s95_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
                      break;
                      case 5:
                         res = at45_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
                      break;
                      case 6:
                         //NAND
                          if (nandRaw == true) res = nand_block_read(buf.get(), currentPageSize + currentECCsize, k, sectorsPerBlock, current_programmer);
                          else res = nand_block_read(buf.get(), currentPageSize, k, sectorsPerBlock, current_programmer);
                          if (res==0) res = 1;
                      break;
                      default:
                         //Unsupport
                         QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                         doNotDisturbCancel();
                         ProgDeviceClose(current_programmer);
                         ui->checkBox_3->setStyleSheet("");
                      return;
                      }
                    // if res=-1 - error, stop
                    if (statusCH341 != 0)
                    {
                       showConnectionError();
                       doNotDisturbCancel();
                       ProgDeviceClose(current_programmer);
                       return;
                    }
                    if (res <= 0)
                    {
                        if (currentChipType == 6)
                        {
                            ui->label_nandInfo->setText(tr("NAND operation failed at block/page %1 (error %2).").arg(curBlock).arg(res));
                        }
                        QMessageBox::about(this, tr("Error"), tr("Error reading block ") + QString::number(curBlock));
                        ProgDeviceClose(current_programmer);
                        ui->pushButton->setStyleSheet(grnKeyStyle);
                        doNotDisturbCancel();
                        clearCheckboxes();
                        return;
                    }
                    {
                      const uint8_t *expected = reinterpret_cast<const uint8_t *>(chipData.constData()) + addr;
                      auto firstDifference = [&]() -> uint32_t {
                          for (uint32_t n = 0; n < step; n++) if (expected[n] != buf[n]) return n;
                          return step;
                      };
                      j = firstDifference();
                      if (j < step)
                      {
                          const QVector<uint16_t> lower = verifiedReadSpeeds(verifyClock);
                          for (int si = 1; si < lower.size() && j < step; ++si)
                          {
                              ProgDeviceClose(current_programmer);
                              if (!openForVerifiedRead(lower[si], tr("Verify")))
                              {
                                  ui->checkBox_3->setStyleSheet("");
                                  doNotDisturbCancel();
                                  clearCheckboxes();
                                  return;
                              }
                              verifyClock = lower[si];
                              ui->statusMessage->setText(tr("Verify: block %1 differs, re-reading at %2").arg(k).arg(formatClock(verifyClock)));
                              qApp->processEvents();
                              res = readChipUnit(buf.get(), k, step, sectorsPerBlock);
                              if (statusCH341 != 0 || res <= 0)
                              {
                                  if (statusCH341 != 0) showConnectionError();
                                  else QMessageBox::about(this, tr("Error"), tr("Error reading block ") + QString::number(curBlock));
                                  ProgDeviceClose(current_programmer);
                                  ui->checkBox_3->setStyleSheet("");
                                  doNotDisturbCancel();
                                  clearCheckboxes();
                                  return;
                              }
                              j = firstDifference();
                          }
                          if (j == step) ++verifyRereadBlocks;   // read error, data is correct
                      }
                      if (j < step)
                      {
                            //error compare
                            QString message = tr("Error comparing data!\nAddress:   ") + hexiAddr(addr + j) + tr("\nBuffer: ") + bytePrint(expected[j]) + tr("    Chip: ") + bytePrint(buf[j]);
                            if (verifyClock != verifyStartClock)
                                message += "\n\n" + tr("The block was re-read down to %1 and still differs, so this is a real write error, not a read error.").arg(formatClock(verifyClock));
                            QMessageBox::about(this, tr("Error"), message);
                            ui->statusMessage->setText("");
                            ui->checkBox_3->setStyleSheet("");
                            ProgDeviceClose(current_programmer);
                            doNotDisturbCancel();
                            clearCheckboxes();
                            return;
                      }
                    }
                     addr = addr + step;
                     curBlock++;
                     qApp->processEvents();
                     if (isHalted)
                     {
                         isHalted = false;
                         ProgDeviceClose(current_programmer);
                         doNotDisturbCancel();
                         return;
                     }
                     opSpeedUpdate(curBlock, numBlocks, step);
                     currentPercent = static_cast<int>((curBlock * 100.0) / numBlocks);
                     if (currentPercent != lastPercent)
                     {
                         ui->progressBar->setValue(currentPercent);
                         lastPercent = currentPercent;
                     }
                 }
             }
             else
             {
                //Not correct Number fnd size of blocks
               // Nothing was read, so this must not end as a successful operation.
               if ((currentChipType == 0) || (currentChipType > 4)) QMessageBox::about(this, tr("Error"), tr("Before reading from chip please press 'Detect' button."));
               else QMessageBox::about(this, tr("Error"), tr("Please select the chip parameters - manufacture and chip name."));
               ProgDeviceClose(current_programmer);
               clearCheckboxes();
               return;
             }
             doNotDisturbCancel();
             opSpeedFinish();
             ui->progressBar->setValue(0);
             ui->checkBox_3->setStyleSheet("");
             ui->crcEdit->setText(getCRC32(chipData));
             ProgDeviceClose(current_programmer);
             lastOperationSucceeded = true;
             finishNandRead(tr("Verify complete"));
             QString badNote;
             if (!verifySkippedBadBlocks.isEmpty())
             {
                 badNote = "\n\n" + tr("%1 bad block(s) were skipped (Erase/Write leave them untouched): %2")
                                          .arg(verifySkippedBadBlocks.size()).arg(verifySkippedBadBlocks.join(", "));
                 if (verifyCompactsAfterBad)
                     badNote += "\n" + tr("The data was moved past them, so the last %1 block(s) of the image did not fit on the chip and were not compared.")
                                          .arg(verifySkippedBadBlocks.size());
             }
             if (verifyRereadBlocks || !badNote.isEmpty())
             {
                 QString note;
                 if (verifyRereadBlocks)
                 {
                     note = "\n\n" + tr("%1 block(s) read differently at %2 but matched the buffer at %3. The data on the chip is correct; %2 is too fast for reading this chip. Use %3 or lower next time.")
                                          .arg(verifyRereadBlocks).arg(formatClock(verifyStartClock)).arg(formatClock(verifyClock));
                     ui->statusMessage->setText(tr("Verify OK after lowering the clock to %1").arg(formatClock(verifyClock)));
                 }
                 QMessageBox::information(this, tr("Ok!"), tr("The operation was successful!") + note + badNote);
             }
             else QMessageBox::about(this, tr("Ok!"), tr("The operation was successful!"));
    }
      else
      {
          ch341StatusFlashing();
          showConnectionError();
      }
}

void MainWindow::on_pushButton_3_clicked()
{
    const bool erase = ui->checkBox->isChecked();
    const bool blankCheck = ui->checkBox_1->isChecked();
    const bool write = ui->checkBox_2->isChecked();
    const bool verify = ui->checkBox_3->isChecked();
    if (!validateNandOperation(write, write || verify)) return;
    // Ask before the sequence starts, not after the chip has been erased.
    if (write && !confirmNandWriteMode()) return;
    nandWriteModeConfirmed = write;
    ui->pushButton_3->setStyleSheet(redKeyStyle);
    const auto run = [this](bool selected, void (MainWindow::*action)()) {
        if (!selected) return true;
        (this->*action)();
        return lastOperationSucceeded;
    };
    if (run(erase, &MainWindow::on_actionErase_triggered) &&
        run(blankCheck, &MainWindow::on_actionCheck_erase_triggered) &&
        run(write, &MainWindow::on_actionWrite_triggered))
        run(verify, &MainWindow::on_actionVerify_triggered);
    nandWriteModeConfirmed = false;
    ui->pushButton_3->setStyleSheet(grnKeyStyle);
}

void MainWindow::receiveAddr(QString addressData)
{
    uint32_t blockEndAddr = 0;
    int e,t;
    QString endType;
    e = addressData.indexOf("-");
    t = addressData.length();
    blockStartAddr = 0;
    blockLen = 0;
    endType = addressData.mid(t - 1, 1);
    blockStartAddr = hexToInt(addressData.mid(0, e));
    if (endType.compare("*")==0)
    {
        blockEndAddr = hexToInt(addressData.mid(e + 1, t - e - 2));
        if (blockEndAddr < blockStartAddr)
        {
            QMessageBox::about(this, tr("Error"), tr("The end address must be greater than the starting address."));
            return;
        }
        blockLen = blockEndAddr - blockStartAddr + 1;
    }
    else blockLen = hexToInt(addressData.mid(e + 1, t - e - 2));
    chipData = hexEdit->data();
    if (quint64(blockStartAddr) + blockLen > quint64(chipData.size()))
    {
       QMessageBox::about(this, tr("Error"), tr("The ending address must not exceed the buffer size."));
       return;
    }
    block = chipData.mid(static_cast<int>(blockStartAddr), static_cast<int>(blockLen));
    ui->statusMessage->setText(tr("Saving block"));
    fileName = QFileDialog::getSaveFileName(this,
                                QString(tr("Save block")),
                                lastDirectory,
                                "Data Images (*.bin *.BIN);;All files (*.*)");
    ui->statusMessage->setText("");
    if (fileName.isEmpty()) return;   // cancelled: do not create a file named ".bin"
    QFileInfo info(fileName);
    lastDirectory = info.filePath();
    if (QString::compare(info.suffix(), "bin", Qt::CaseInsensitive)) fileName = fileName + ".bin";
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::about(this, tr("Error"), tr("Error saving file!"));
        return;
    }
    file.write(block);
    file.close();
    ui->statusMessage->setText("");
}

void MainWindow::receiveAddr2(QString addressData)
{
    uint32_t ee;
    QString endType;
    blockStartAddr = 0;
    blockLen = 0;
    blockStartAddr = hexToInt(addressData);
    ui->statusMessage->setText(tr("Opening block"));
    fileName = QFileDialog::getOpenFileName(this,
                                QString(tr("Open block")),
                                lastDirectory,
                                "Data Images (*.bin *.BIN);;All files (*.*)");
    if (fileName.isEmpty())
    {
        ui->statusMessage->setText("");
        return;
    }
    ui->statusMessage->setText(tr("Current file: ") + fileName);
    QFileInfo info(fileName);
    lastDirectory = info.filePath();
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {

        return;
    }
    block = file.readAll();
    blockLen = static_cast<uint32_t>(block.size());
    chipData = hexEdit->data();
    if (quint64(blockStartAddr) + blockLen > quint64(chipData.size()))
    {
        QMessageBox::about(this, tr("Error"), tr("The end address out of image size!"));
        return;
    }
    for (ee=0; ee < blockLen; ee++)
    {
        chipData[ee + blockStartAddr] = block[ee];
    }
    hexEdit->setData(chipData);
    file.close();
    ui->statusMessage->setText("");
}

void MainWindow::on_actionSave_Part_triggered()
{
    DialogSP* savePartDialog = new DialogSP(this);
    savePartDialog->show();

    connect(savePartDialog, SIGNAL(sendAddr(QString)), this, SLOT(receiveAddr(QString)));
}

void MainWindow::on_actionLoad_Part_triggered()
{
    DialogRP* loadPartDialog = new DialogRP(this);
    loadPartDialog->show();

    connect(loadPartDialog, SIGNAL(sendAddr2(QString)), this, SLOT(receiveAddr2(QString)));
}

void MainWindow::on_actionFind_Replace_triggered()
{
    SearchDialog* searchDialog = new SearchDialog(hexEdit);
    searchDialog->show();
}

void MainWindow::ch341StatusFlashing()
{
    if (statusCH341 == 0)
    {
        ui->eStatus->setText(tr("Connected"));
        ui->eStatus -> setStyleSheet("QLineEdit {border: 2px solid gray;border-radius: 5px;color:#000;background:#9f0;font-weight:600;border-style:inset;}");
    }
    else
    {
        ui->eStatus->setText(tr("Not connected"));
        ui->eStatus -> setStyleSheet("QLineEdit {border: 2px solid gray;border-radius: 5px;color:#fff;background:#f00;font-weight:600;border-style:inset;}");
    }
}

void MainWindow::on_comboBox_type_currentIndexChanged(int index)
{
    int i, index2;
    ui->comboBox_man->clear();
    ui->comboBox_name->clear();
    ui->comboBox_man->addItem("");
    ui->comboBox_name->addItem("");
    ui->jedecEdit->setText("");
    currentChipType = static_cast<uint8_t>(ui->comboBox_type->itemData(index).toInt());
    updateNandControlVisibility();

    ui->comboBox_size->clear();
    ui->comboBox_page->clear();
    ui->comboBox_size->addItem(" ", 0);
    ui->comboBox_page->addItem(" ", 0);

    switch (currentChipType)
       {
       case 0:
          //SPI
          ui->comboBox_size->addItem("64 K", 64 * 1024);
          ui->comboBox_size->addItem("128 K", 128 * 1024);
          ui->comboBox_size->addItem("256 K", 256 * 1024);
          ui->comboBox_size->addItem("512 K", 512 * 1024);
          ui->comboBox_size->addItem("1 M", 1024 * 1024);
          ui->comboBox_size->addItem("2 M", 2048 * 1024);
          ui->comboBox_size->addItem("4 M", 4096 * 1024);
          ui->comboBox_size->addItem("8 M", 8192 * 1024);
          ui->comboBox_size->addItem("16 M", 16384 * 1024);
          ui->comboBox_size->addItem("32 M", 32768 * 1024);
          ui->comboBox_size->addItem("64 M", 65536 * 1024);
          ui->comboBox_size->addItem("128 M", 65536 * 2048);
          ui->comboBox_size->addItem("256 M", 65536 * 4096);
          ui->comboBox_page->addItem("256", 256);
       break;
       case 1:
          //I2C
          ui->comboBox_size->addItem("128 B", 128);
          ui->comboBox_size->addItem("256 B", 256);
          ui->comboBox_size->addItem("512 B", 512);
          ui->comboBox_size->addItem("1 K", 1 * 1024);
          ui->comboBox_size->addItem("2 K", 2 * 1024);
          ui->comboBox_size->addItem("4 K", 4 * 1024);
          ui->comboBox_size->addItem("8 K", 8 * 1024);
          ui->comboBox_size->addItem("16 K", 16 * 1024);
          ui->comboBox_size->addItem("32 K", 32 * 1024);
          ui->comboBox_size->addItem("64 K", 64 * 1024);
          ui->comboBox_size->addItem("128 K", 128 * 1024);
          ui->comboBox_size->addItem("256 K", 256 * 1024);
          ui->comboBox_page->addItem("8", 8);
          ui->comboBox_page->addItem("16", 16);
          ui->comboBox_page->addItem("32", 32);
       break;
       case 2:
          //MicroWire
          ui->comboBox_size->addItem("128 B", 128);
          ui->comboBox_size->addItem("256 B", 256);
          ui->comboBox_size->addItem("512 B", 512);
          ui->comboBox_size->addItem("1 K", 1 * 1024);
          ui->comboBox_size->addItem("2 K", 2 * 1024);
          ui->comboBox_page->addItem("16", 16);
       break;
       case 3:
          //25xxx
       case 4:
          //95xxx
          ui->comboBox_size->addItem("128 B", 128);
          ui->comboBox_size->addItem("256 B", 256);
          ui->comboBox_size->addItem("512 B", 512);
          ui->comboBox_size->addItem("1 K", 1 * 1024);
          ui->comboBox_size->addItem("2 K", 2 * 1024);
          ui->comboBox_size->addItem("4 K", 4 * 1024);
          ui->comboBox_size->addItem("8 K", 8 * 1024);
          ui->comboBox_size->addItem("16 K", 16 * 1024);
          ui->comboBox_size->addItem("32 K", 32 * 1024);
          ui->comboBox_size->addItem("64 K", 64 * 1024);
          ui->comboBox_size->addItem("128 K", 128 * 1024);
          ui->comboBox_size->addItem("256 K", 256 * 1024);
          ui->comboBox_size->addItem("512 K", 512 * 1024);
          ui->comboBox_page->addItem("16", 16);
          ui->comboBox_page->addItem("32", 32);
          ui->comboBox_page->addItem("64", 64);
          ui->comboBox_page->addItem("128", 128);
          ui->comboBox_page->addItem("256", 256);
       break;
       case 5:
          //45xxx
          ui->comboBox_size->addItem("132 K",   132 * 1024);
          ui->comboBox_size->addItem("264 K",   264 * 1024);
          ui->comboBox_size->addItem("528 K",   528 * 1024);
          ui->comboBox_size->addItem("1056 K", 1056 * 1024);
          ui->comboBox_size->addItem("2112 K", 2112 * 1024);
          ui->comboBox_size->addItem("4224 K", 4224 * 1024);
          ui->comboBox_size->addItem("8448 K", 8448 * 1024);
          ui->comboBox_page->addItem("256", 256);
          ui->comboBox_page->addItem("264", 264);
          ui->comboBox_page->addItem("512", 512);
          ui->comboBox_page->addItem("528", 528);
       break;
       case 6:
        // SPI NAND FLASH
          ui->comboBox_size->addItem("64 M", 65536 * 1024);
          ui->comboBox_size->addItem("128 M", 65536 * 2048);
          ui->comboBox_size->addItem("256 M", 65536 * 4096);
          ui->comboBox_size->addItem("512 M", 65536 * 8192);
          ui->comboBox_page->addItem("1024", 1024);
          ui->comboBox_page->addItem("2048", 2048);
          ui->comboBox_page->addItem("4096", 4096);
       break;
       default:
          //Unsupport
       return;
       }

    for (i = 0; i < max_rec; i++)
    {
        //replacing items to combobox Manufacture
        index2 = ui->comboBox_man->findText(chips[i].chipManuf);
        if (( index2 == -1 ) && (chips[i].chipTypeHex == currentChipType)) ui->comboBox_man->addItem(chips[i].chipManuf);
    }
     ui->comboBox_man->setCurrentIndex(0);
     ui->statusMessage->setText("");
     ui->comboBox_man->setCurrentIndex(0);
     ui->comboBox_vcc->setCurrentIndex(0);
     ui->comboBox_name->setCurrentIndex(0);
     ui->comboBox_page->setCurrentIndex(0);
     ui->comboBox_block->setCurrentIndex(0);
     ui->comboBox_size->setCurrentIndex(0);
     ui->comboBox_addr4bit->setCurrentIndex(0);
     ui->comboBox_ECC->setCurrentIndex(0);
     if ((currentChipType > 0) && (currentChipType < 3))
     {
         ui->pushButton_2->hide();
         ui->comboBox_block->hide();
         ui->comboBox_addr4bit->hide();
         ui->label_8->hide();
         ui->label_9->hide();
         ui->actionDetect->setDisabled(true);
         ui->actionChip_info->setDisabled(true);

     }
     if (currentChipType != 0) ui->actionSecurity_registers->setEnabled(false);

     if (currentChipType == 0)
     {
         ui->pushButton_2->show();
         ui->comboBox_addr4bit->show();
         ui->label_8->show();
         ui->label_9->show();
         ui->comboBox_block->show();
         ui->actionDetect->setEnabled(true);
         ui->actionChip_info->setEnabled(true);
         ui->actionSecurity_registers->setEnabled(true);
     }
     if (currentChipType != 1)
     {
         ui->comboBox_i2cSpeed->hide();
         ui->label_10->hide();
     }
     else
     {
         ui->comboBox_i2cSpeed->show();
         ui->label_10->show();
     }
     if (currentChipType > 2)
     {
         ui->pushButton_2->hide();
         ui->comboBox_block->hide();
         ui->comboBox_addr4bit->hide();
         ui->label_8->hide();
         ui->label_9->hide();
         ui->actionDetect->setDisabled(true);
         ui->actionChip_info->setEnabled(true);
     }
     if (currentChipType == 5)
     {
          ui->pushButton_2->show();
          ui->actionDetect->setEnabled(true);
     }
     if (currentChipType == 6)
     {
         ui->pushButton_2->show();
         ui->label_8->hide();
         ui->label_9->show();
         ui->label_11->show();
         ui->label_12->show();
         ui->comboBox_block->show();
         ui->comboBox_ECC->show();
         ui->comboBox_raw->show();
         ui->comboBox_onDieEcc->show();
         ui->label_onDieEcc->show();
         ui->label_nandInfo->show();
         ui->actionDetect->setEnabled(true);
         ui->actionChip_info->setEnabled(true);
         ui->actionSecurity_registers->setEnabled(true);
         ui->actionBad_block_management->setEnabled(true);
     }
     if (currentChipType != 6)
     {
         ui->label_11->hide();
         ui->comboBox_ECC->hide();
         ui->comboBox_raw->hide();
         ui->comboBox_onDieEcc->hide();
         ui->label_onDieEcc->hide();
         ui->label_nandInfo->hide();
         ui->label_12->hide();
         ui->actionBad_block_management->setEnabled(false);

     }
    updateNandControlVisibility();
    updateNandSelectionInfo();
}

void MainWindow::on_comboBox_addr4bit_currentIndexChanged(int index)
{
   currentAddr4bit = ui->comboBox_addr4bit->currentData().toUInt();
   index++;
}

void MainWindow::on_comboBox_i2cSpeed_currentIndexChanged(int index)
{
   currentI2CBusSpeed = static_cast<uint16_t>(ui->comboBox_i2cSpeed->currentData().toUInt());
   index++;
}

void MainWindow::on_comboBox_ECC_currentIndexChanged(int index)
{
    currentECCsize = (ui->comboBox_ECC->currentData().toUInt()) * 64;
    if (uiReady && currentChipType == 6) on_comboBox_size_currentIndexChanged(index);
    updateNandSelectionInfo();
    index++;
}

void MainWindow::on_actionAbout_triggered()
{
    DialogAbout* aboutDialog = new DialogAbout(this);
    aboutDialog->show();
}

void MainWindow::on_actionChecksum_calculate_triggered()
{
   //Refreshing CRC32
    ui->crcEdit->setText(getCRC32(chipData));
}

void MainWindow::on_actionEdit_chips_Database_triggered()
{
    #ifdef _WIN32
        QString programPath = QDir(QCoreApplication::applicationDirPath()).filePath("IMSProg_editor.exe");
    #else
    QString programPath = "./IMSProg_editor";
    #endif
    bool programExists = QFileInfo::exists(programPath) && !QDir(programPath).exists();

    if (!programExists)
    {
        // if does not exists on realive path check another
        QString systemProgramPath = QStandardPaths::findExecutable(programPath);

        if (!systemProgramPath.isEmpty())
        {
            // exists on system path
            programPath = systemProgramPath;
            programExists = true;
        }
    }

    if (programExists)
    {
        QProcess::execute(programPath, QStringList());
        progInit();
    }
    else
    {
       QMessageBox::about(this, tr("Error"), tr("File 'IMSProg_editor' not found!"));
    }
}

void MainWindow::doNotDisturb()
{
   findChild<QAction *>("actionUpdateChipDatabase")->setDisabled(true);
   for (QMenu *menu : {menuCH341, menuCH347T, menuCH347F}) if (menu) menu->setEnabled(false);
   if (actionDriver) actionDriver->setEnabled(false);
   if (driverStatusButton) driverStatusButton->setEnabled(false);
   ui->actionDetect->setDisabled(true);
   ui->actionOpen->setDisabled(true);
   ui->actionSave->setDisabled(true);
   ui->actionLoad_Part->setDisabled(true);
   ui->actionSave_Part->setDisabled(true);
   ui->actionExport_to_Intel_HEX->setDisabled(true);
   ui->actionImport_from_Intel_HEX->setDisabled(true);
   ui->actionExtract_from_ASUS_CAP->setDisabled(true);
   ui->actionEdit_chips_Database->setDisabled(true);
   ui->actionExit->setDisabled(true);

   ui->actionRead->setDisabled(true);
   if (actionReadVerify) actionReadVerify->setDisabled(true);
   ui->actionWrite->setDisabled(true);
   ui->actionErase->setDisabled(true);
   ui->actionCheck_erase->setDisabled(true);
   ui->actionVerify->setDisabled(true);

   ui->actionFind_Replace->setDisabled(true);
   ui->actionUndo->setDisabled(true);
   ui->actionRedo->setDisabled(true);
   ui->actionCopy->setDisabled(true);
   ui->actionPaste->setDisabled(true);
   ui->actionChecksum_calculate->setDisabled(true);
   ui->actionGoto_address->setDisabled(true);
   ui->actionCompare_files->setDisabled(true);
   ui->actionFill_with_code->setDisabled(true);
   ui->actionFill_test_image->setDisabled(true);

   ui->actionChip_info->setDisabled(true);
   ui->actionSecurity_registers->setDisabled(true);
   ui->actionBad_block_management->setDisabled(true);
   ui->actionStop->setDisabled(false);
   ui->checkBox->setEnabled(false);
   ui->checkBox_1->setEnabled(false);
   ui->checkBox_2->setEnabled(false);
   ui->checkBox_3->setEnabled(false);

   ui->actionCH341A_B_v1_2->setDisabled(true);
   ui->actionCH341A_v1_7->setDisabled(true);
   ui->actionCH347T->setDisabled(true);
   ui->actionCH347T_v1_1->setDisabled(true);
   ui->actionCH347F->setDisabled(true);
   ui->actionCH347F_fast->setDisabled(true);
   ui->actionCH347F_safe->setDisabled(true);
   ui->actionShow_programmer_version->setDisabled(true);
   ui->actionConnection_diagnostics->setDisabled(true);

   ui->pushButton->blockSignals(true);
   ui->pushButton_2->blockSignals(true);
   ui->pushButton_3->blockSignals(true);

   ui->comboBox_type->setDisabled(true);
   ui->comboBox_man->setDisabled(true);
   ui->comboBox_name->setDisabled(true);
   ui->comboBox_size->setDisabled(true);
   ui->comboBox_page->setDisabled(true);
   ui->comboBox_block->setDisabled(true);
   ui->comboBox_vcc->setDisabled(true);
   ui->comboBox_addr4bit->setDisabled(true);
   ui->comboBox_i2cSpeed->setDisabled(true);
   ui->comboBox_ECC->setDisabled(true);
   ui->comboBox_raw->setDisabled(true);
   ui->comboBox_onDieEcc->setDisabled(true);
   ui->actionFT232H_v1_2->setDisabled(true);

   hexEdit->blockSignals(true);
   timer->stop();
   updateNandControlVisibility();
}

void MainWindow::doNotDisturbCancel()
{
   findChild<QAction *>("actionUpdateChipDatabase")->setEnabled(true);
   for (QMenu *menu : {menuCH341, menuCH347T, menuCH347F}) if (menu) menu->setEnabled(true);
   if (actionDriver) actionDriver->setEnabled(true);
   if (driverStatusButton) driverStatusButton->setEnabled(true);
   if ((currentChipType == 0) || (currentChipType == 5) || (currentChipType == 6)) ui->actionDetect->setDisabled(false);
   ui->actionOpen->setDisabled(false);
   ui->actionSave->setDisabled(false);
   ui->actionLoad_Part->setDisabled(false);
   ui->actionSave_Part->setDisabled(false);
   ui->actionExport_to_Intel_HEX->setDisabled(false);
   ui->actionImport_from_Intel_HEX->setDisabled(false);
   ui->actionExtract_from_ASUS_CAP->setDisabled(false);
   ui->actionEdit_chips_Database->setDisabled(false);
   ui->actionExit->setDisabled(false);

   ui->actionRead->setDisabled(false);
   if (actionReadVerify) actionReadVerify->setDisabled(false);
   ui->actionWrite->setDisabled(false);
   ui->actionErase->setDisabled(false);
   ui->actionCheck_erase->setDisabled(false);
   ui->actionVerify->setDisabled(false);

   ui->actionFind_Replace->setDisabled(false);
   ui->actionUndo->setDisabled(false);
   ui->actionRedo->setDisabled(false);
   ui->actionCopy->setDisabled(false);
   ui->actionPaste->setDisabled(false);
   ui->actionChecksum_calculate->setDisabled(false);
   ui->actionGoto_address->setDisabled(false);
   ui->actionCompare_files->setDisabled(false);
   ui->actionFill_with_code->setDisabled(false);
   ui->actionFill_test_image->setDisabled(false);

   ui->actionCH341A_B_v1_2->setDisabled(false);
   ui->actionCH341A_v1_7->setDisabled(false);
   ui->actionCH347T->setDisabled(false);
   ui->actionCH347T_v1_1->setDisabled(false);
   ui->actionCH347F->setDisabled(false);
   ui->actionCH347F_fast->setDisabled(false);
   ui->actionCH347F_safe->setDisabled(false);
   ui->actionShow_programmer_version->setDisabled(false);
   ui->actionConnection_diagnostics->setDisabled(false);
   if ((currentChipType == 0) || (currentChipType == 6) || (currentChipType > 2)) ui->actionChip_info->setDisabled(false);
   if ((currentChipType == 0) || (currentChipType == 6)) ui->actionSecurity_registers->setDisabled(false);
   if (currentChipType == 6) ui->actionBad_block_management->setDisabled(false);
   ui->actionStop->setDisabled(true);
   ui->checkBox->setEnabled(true);
   ui->checkBox_1->setEnabled(true);
   ui->checkBox_2->setEnabled(true);
   ui->checkBox_3->setEnabled(true);

   ui->pushButton->blockSignals(false);
   ui->pushButton_2->blockSignals(false);
   ui->pushButton_3->blockSignals(false);

   ui->comboBox_type->setDisabled(false);
   ui->comboBox_man->setDisabled(false);
   ui->comboBox_name->setDisabled(false);
   ui->comboBox_size->setDisabled(false);
   ui->comboBox_page->setDisabled(false);
   ui->comboBox_block->setDisabled(false);
   ui->comboBox_vcc->setDisabled(false);
   ui->comboBox_addr4bit->setDisabled(false);
   ui->comboBox_i2cSpeed->setDisabled(false);
   ui->comboBox_ECC->setDisabled(false);
   ui->comboBox_raw->setDisabled(false);
   ui->comboBox_onDieEcc->setDisabled(false);
   ui->actionFT232H_v1_2->setDisabled(false);
   applyPlatformRestrictions();

   hexEdit->blockSignals(false);
   timer->start();
   updateNandControlVisibility();
}

void MainWindow::on_actionStop_triggered()
{

  hexEdit->setData(chipData);
  ui->crcEdit->setText(getCRC32(chipData));
  isHalted = true;
  if (nandReadIncomplete)
      ui->label_nandInfo->setText(tr("NAND read stopped. Buffer is incomplete; unread bytes are FF."));
  QMessageBox::about(this, tr("Stop"), tr("Operation aborted!"));
  clearCheckboxes();
  return;
}

void MainWindow::clearCheckboxes()
{
  ui->pushButton->setStyleSheet(grnKeyStyle);
  ui->checkBox->setStyleSheet("");
  ui->checkBox_1->setStyleSheet("");
  ui->checkBox_2->setStyleSheet("");
  ui->checkBox_3->setStyleSheet("");
  ui->checkBox->setChecked(false);
  ui->checkBox_1->setChecked(false);
  ui->checkBox_2->setChecked(false);
  ui->checkBox_3->setChecked(false);
  ui->pushButton_3->setStyleSheet(grnKeyStyle);
  ui->statusMessage->setText("");
  return;
}

void MainWindow::on_pushButton_4_clicked()
{
    //info form showing
    DialogInfo* infoDialog = new DialogInfo(this);
    infoDialog->show();
    infoDialog->setProgrammer(current_programmer);
    if ((currentChipType == 0) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(2); //NOR_FLASH 3.3
    if ((currentChipType == 0) && (ui->comboBox_vcc->currentIndex() == 2)) infoDialog->setChip(3); //NOR FLASH 1.8
    if ((currentChipType == 0) && (ui->comboBox_vcc->currentIndex() == 3)) infoDialog->setChip(8); //NOR FLASH 2.5
    if ((currentChipType == 1) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(1); //24xxx 3.3
    if ((currentChipType == 1) && (ui->comboBox_vcc->currentIndex() == 4)) infoDialog->setChip(9); //24xxx 5.0
    if ((currentChipType == 2) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(4); //93xxx 3.3
    if ((currentChipType == 3) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(2); //25xxx 3.3
    if ((currentChipType == 3) && (ui->comboBox_vcc->currentIndex() == 2)) infoDialog->setChip(3); //25xxx 1.8
    if ((currentChipType == 4) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(2); //95xxx 3.3
    if ((currentChipType == 5) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(5); //45xxx 3.3
    if ((currentChipType == 6) && (ui->comboBox_vcc->currentIndex() == 1)) infoDialog->setChip(6); //NAND_FLASH 3.3
    if ((currentChipType == 6) && (ui->comboBox_vcc->currentIndex() == 2)) infoDialog->setChip(7); //NAND_FLASH 1.8
}

void MainWindow::on_actionChip_info_triggered()
{
     timer->stop();
     if (currentChipType == 0)
     {
        DialogSFDP* sfdpDialog = new DialogSFDP(this);
        connect(sfdpDialog, SIGNAL(closeRequestHasArrived()), this, SLOT(closeSFDP()));
        sfdpDialog->show();
        sfdpDialog->setDeviceType(current_programmer);
     }

     if (currentChipType == 6)
     {
         DialogNANDSr* nandSRDialog = new DialogNANDSr(this);
         connect(nandSRDialog, SIGNAL(closeRequestHasArrived()), this, SLOT(closeSR()));
         nandSRDialog->show();
         nandSRDialog->setPattern(currentAlgorithm);
         nandSRDialog->setDeviceType(current_programmer);
     }

     if ((currentChipType > 2) && (currentChipType != 6))
     {
         DialogSR* srDialog = new DialogSR(this);
         connect(srDialog, SIGNAL(closeRequestHasArrived()), this, SLOT(closeSR()));
         srDialog->show();
         srDialog->setChipType(currentChipType);
         srDialog->setDeviceType(current_programmer);
     }

}

void MainWindow::progInit()
{
    int index2;
    ui->statusMessage->setText(tr("Opening DAT file"));
    QFile datfile(qApp->property("app/userChipDatabaseFile").toString());

    if (!datfile.exists()) {
        datfile.setFileName(qApp->property("app/systemChipDatabaseFile").toString());
    }
    if (!datfile.exists()) {
        QString msg = tr("Cannot find a chip database file.\nYou may want to run IMSProg_database_update");
        QMessageBox::about(this, tr("Error"), msg);
        return;
    }

    qDebug() << "Using chip database file " << datfile.fileName();
    QByteArray dataChips;
    if (!datfile.open(QIODevice::ReadOnly))
    {
        QString msg = tr("Error loading chip database file!\nFile: %1\nError Code: %2\nReason: %3")
                      .arg(datfile.fileName())
                      .arg(static_cast<int>(datfile.error()))
                      .arg(datfile.errorString());
        QMessageBox::about(this, tr("Error"), msg);
        return;
    }
    dataChips = datfile.readAll();
    datfile.close();
    //parsing dat file
    ui->statusMessage->setText(tr("Parsing DAT file"));
    //parsing qbytearray
    char txtBuf[0x30];
    int i, j, recNo, dataPoz, dataSize, delay;
    uint32_t chipSize;
    uint16_t blockSize;
    unsigned char tmpBuf;
    dataPoz = 0;
    recNo = 0;
    QStringList verticalHeader;
    dataSize = dataChips.length();
    const int maxRecords = static_cast<int>(sizeof(chips) / sizeof(chips[0]));
    while (dataPoz + 0x44 <= dataSize && recNo < maxRecords)
    {
        for (j=0; j<0x30; j++)
             {
                 txtBuf[j] = 0;
             }
        j = 0;
             while ((j < 0x10) && (dataChips[recNo * 0x44 + j] != ',')) // ASCII data reading
             {
                 txtBuf[j] = dataChips[recNo * 0x44 + j];
                 j++;
             }
             if (txtBuf[1] == 0x00) break;
             chips[recNo].chipTypeTxt = QByteArray::fromRawData(txtBuf, j);
         for (i=0; i<0x30; i++)
             {
                 txtBuf[i] = 0;
             }
         j++;
         i = 0;
         while ((i < 0x20) && (dataChips[recNo * 0x44 + j] != ',')) // ASCII data reading
         {
             txtBuf[i] = dataChips[recNo * 0x44 + j];
             j++;
             i++;
         }
             chips[recNo].chipManuf = QByteArray::fromRawData(txtBuf, i);
             for (i=0; i<0x30; i++)
                 {
                     txtBuf[i] = 0;
                 }
             j++;
             i = 0;
             while ((i < 0x30) && (dataChips[recNo * 0x44 + j] != '\0')) // ASCII data reading
             {
                 txtBuf[i] = dataChips[recNo * 0x44 + j];
                 j++;
                 i++;
             }
             chips[recNo].chipName = QByteArray::fromRawData(txtBuf, i);
             chips[recNo].chipJedecIDMan = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x32]);
             chips[recNo].chipJedecIDDev = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x31]);
             chips[recNo].chipJedecIDCap = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x30]);
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x34]);
             chipSize = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x35]);
             chipSize = chipSize + tmpBuf * 256;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x36]);
             chipSize = chipSize + tmpBuf * 256 * 256;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x37]);
             chipSize = chipSize + tmpBuf * 256 * 256 * 256;
             chips[recNo].chipSize = chipSize;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x38]);
             blockSize = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x39]);
             blockSize = blockSize + tmpBuf * 256;
             chips[recNo].sectorSize = blockSize;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3a]);
             chips[recNo].chipTypeHex = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3b]);
             chips[recNo].algorithmCode = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3c]);
             delay = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3d]);
             delay = delay + tmpBuf * 256;
             chips[recNo].delay = delay;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3e]);
             chips[recNo].addr4bit = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3f]);
             chips[recNo].blockSize = tmpBuf * 256 * 1024;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x40]);
             chips[recNo].blockSize = chips[recNo].blockSize + tmpBuf * 1024;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x42]);
             chips[recNo].eepromPages = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x43]);
             if (tmpBuf == 0x00) chips[recNo].chipVCC = "3.3 V";
             if (tmpBuf == 0x01) chips[recNo].chipVCC = "1.8 V";
             if (tmpBuf == 0x02) chips[recNo].chipVCC = "5.0 V";
             if (tmpBuf == 0x03) chips[recNo].chipVCC = "2.5 V";
             dataPoz = dataPoz + 0x44; //next record
             verticalHeader.append(QString::number(recNo));
             recNo++;
    }
    max_rec = recNo;
    for (i = 0; i<max_rec; i++)
    {
        //replacing items to combobox Manufacture
        index2 = ui->comboBox_man->findText(chips[i].chipManuf);
        if ((index2 == -1) && (chips[i].chipTypeHex == 0)) ui->comboBox_man->addItem(chips[i].chipManuf);
    }
     ui->comboBox_man->setCurrentIndex(0);
     ui->statusMessage->setText("");
     currentChipType = 0;
     ui->comboBox_type->setCurrentIndex(0);
}

void MainWindow::slotTimerAlarm()
{
    statusCH341 = ProgDeviceInit(current_programmer, 0, getInterfaceSpeed(current_programmer, 0, 1000));
    ch341StatusFlashing();
    ProgDeviceClose(current_programmer);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
   QMainWindow::resizeEvent(event);
   hexEdit->setGeometry(0,0,ui->frame->width(),ui->frame->height());
}

void MainWindow::closeSFDP()
{
   timer->start();
}

void MainWindow::closeSR()
{
   timer->start();
}

void MainWindow::closeNandSR()
{
    timer->start();
}

void MainWindow::on_actionGoto_address_triggered()
{
    //HExEditor --> goto address
    DialogSetAddr* gotoAddrDialog = new DialogSetAddr(this);
    gotoAddrDialog->show();
    connect(gotoAddrDialog, SIGNAL(sendAddr3(qint64)), this, SLOT(receiveAddr3(qint64)));

}

void MainWindow::receiveAddr3(qint64 gotoAddr)
{
    hexEdit->setCursorPosition(gotoAddr * 2);
    hexEdit->ensureVisible();
}

void MainWindow::on_actionSecurity_registers_triggered()
{
    if (currentChipSize == 0)
    {
        QMessageBox::about(this, tr("Error"), tr("Before working with the security registers, click the 'Detect' button"));
        return;
    }
    if (currentChipType == 0)
    {
        if (currentAlgorithm > 0)
        {
            timer->stop();
            DialogSecurity* securityDialog = new DialogSecurity(this);
            connect(securityDialog, SIGNAL(closeRequestHasArrived()), this, SLOT(closeSR()));
            securityDialog->setAlgorithm(currentAlgorithm);
            securityDialog->setPath(lastDirectory);
            securityDialog->show();
            securityDialog->setDeviceType(current_programmer);

        }
        else QMessageBox::about(this, tr("Error"), tr("There are no security registers in this chip or the current version of IMSProg does not support this algorithm."));
    }
    if (currentChipType == 6)
    {

            DialogNandSecurity* securityNandDialog = new DialogNandSecurity(this);
            connect(securityNandDialog, SIGNAL(closeRequestHasArrived()), this, SLOT(closeNandSR()));
            securityNandDialog->setAlgorithm(currentAlgorithm);
            securityNandDialog->setSectorSize(currentPageSize);
            securityNandDialog->setPath(lastDirectory);
            securityNandDialog->setDeviceType(current_programmer);
            securityNandDialog->show();
    }
}

void MainWindow::on_actionExport_to_Intel_HEX_triggered()
{
    int addr = 0, hi_addr =0;
         QString result = "";
         chipData = hexEdit->data();
         int currSize = chipData.size();
         uint8_t i, counter = 0;
         int ostatok = 0;
         ui->progressBar->setRange(0,chipData.size());
         lastDirectory.replace(".bin", ".hex");
         ui->statusMessage->setText(tr("Saving file"));
         fileName = QFileDialog::getSaveFileName(this,
                                     QString(tr("Save file")),
                                     lastDirectory,
                                     tr("Intel HEX Images") + " (*.hex *.HEX);;" + tr("All files") + " (*.*)");
         ui->statusMessage->setText("");
         if (fileName.isEmpty()) return;   // cancelled: do not create a file named ".hex"
         QFileInfo info(fileName);
         lastDirectory = info.filePath();
         if (QString::compare(info.suffix(), "hex", Qt::CaseInsensitive)) fileName = fileName + ".hex";
         ui->statusMessage->setText(tr("Current file: ") + info.fileName());
         QFile file(fileName);
         QTextStream stream(&file);
         if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text))
         {

             return;
         }
         stream.seek(file.size());
         while ((addr + hi_addr * 0x10000) < (chipData.size()))
         {

             if (addr < currSize - 0x20) ostatok = 0x20;
             else  ostatok =currSize - addr;

                result.append(":");
                result.append(bytePrint(static_cast<unsigned char>(ostatok)));              //number of bytes per string
                result.append(bytePrint(static_cast<unsigned char>((addr & 0xff00) >> 8))); //one address byte
                result.append(bytePrint(static_cast<unsigned char>(addr & 0xff)));          //zero address byte
                result.append("00");                                                        //type 00
                counter = counter + static_cast<unsigned char>(ostatok) + uint8_t((addr & 0xff00) >> 8) + uint8_t(addr & 0xff) ;
                for (i = 0; i < ostatok; i++)
                {
                    result.append(bytePrint(static_cast<unsigned char>((chipData[hi_addr * 256 * 256 + addr + i]))));
                    counter = counter + static_cast<uint8_t>((chipData[hi_addr * 256 * 256 + addr + i]));
                }
             result.append(bytePrint(0xff - counter + 1));
             counter = 0;
             stream << result << "\n";
             result.clear();

             addr = addr + 0x20;
             if (addr >= 0x10000)                                                            //command 04 - setting the high address
             {
                 hi_addr ++;
                 addr = addr - 0x10000;
                 result.append(":02000004");
                 result.append(bytePrint(uint8_t((hi_addr & 0xff00) >> 8)));
                 result.append(bytePrint(uint8_t(hi_addr & 0xff)));
                 counter = 0x06 + uint8_t((hi_addr & 0xff00) >> 8) + uint8_t(hi_addr & 0xff);
                 result.append(bytePrint(0xff - counter + 1));
                 counter = 0;
                 stream << result << "\n";
                 result.clear();
                 ui->progressBar->setValue(hi_addr * 256 * 256);
             }
         }
         result = ":00000001FF\n";                                                          //end string
         stream << result;
         file.close();
         fileName.clear();
         ui->progressBar->setValue(0);
}

void MainWindow::on_actionImport_from_Intel_HEX_triggered()
{
    chipData = hexEdit->data();
    // Records are stored into the buffer, so its size is the limit (this also
    // covers "no chip selected" and NAND images with OOB).
    int chipSize = static_cast<int>(chipData.size());
    uint_fast32_t lineLen, lo_addr, hi_addr, command, i;
    unsigned char currByte;
    uint8_t counter, checkSUM;
    QString currStr ="", strVal = "";
    ui->statusMessage->setText(tr("Opening file"));
    if (numberOfReads == 0) oldFileName = fileName;
    else oldFileName = ui->comboBox_name->currentText();
    fileName = QFileDialog::getOpenFileName(this,
                                QString(tr("Open file")),
                                lastDirectory,
                                tr("Intel HEX Images") + " (*.hex *.HEX);;" + tr("All files") + " (*.*)");
    QFileInfo info(fileName);
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    ui->statusMessage->setText(tr("Current file: ") + info.fileName());
    lastDirectory = info.filePath();
    preparingToCompare(0);
    filled = 0;
    hi_addr = 0;
    lo_addr = 0;
    ui->progressBar->setRange(0, chipSize);
    while (!file.atEnd())
    {
        currStr = file.readLine();
        counter = 0;
        //parsing string
        if (currStr[0] != ':')
        {
            QMessageBox::about(this, tr("Error"), tr("Not valid HEX format!"));
            file.close();
            fileName.clear();
            hexEdit->setData(chipData);
            ui->progressBar->setValue(0);
            ui->crcEdit->setText(getCRC32(chipData));
            return;
        }
        strVal = currStr.mid(1,2); //Length of data in current string
        lineLen = hexToInt(strVal);
        counter = counter + static_cast<unsigned char>(lineLen);

        strVal.clear();            //low address
        strVal = currStr.mid(3,4);
        lo_addr = hexToInt(strVal);
        counter = counter + static_cast<unsigned char>((lo_addr) >> 8) + static_cast<unsigned char>(lo_addr & 0x00ff);
        strVal.clear();            //command
        strVal = currStr.mid(7,2);
        command = hexToInt(strVal);
        counter = counter + static_cast<unsigned char>(command);

        if (command == 0) //reading bytes from current string
        {
            for (i = 0; i < lineLen; i++)
            {

                strVal.clear();            //get current byte of string
                strVal = currStr.mid(int(i) * 2 + 9, 2);
                currByte = static_cast<unsigned char>(hexToInt(strVal));
                // Checking valid address
                if (quint64(hi_addr) * 256 * 256 + lo_addr + i >= quint64(chipSize))
                {
                    QMessageBox::about(this, tr("Error"), tr("The address is larger than the size of the chip!"));
                    file.close();
                    fileName.clear();
                    hexEdit->setData(chipData);
                    ui->progressBar->setValue(0);
                    ui->crcEdit->setText(getCRC32(chipData));
                    return;
                }

                chipData.data()[hi_addr * 256 * 256 + lo_addr + i] = char(currByte);
                counter = counter + static_cast<unsigned char>(hexToInt(strVal));

            }
                counter = 255 - counter + 1;
                checkSUM = static_cast<unsigned char>(hexToInt( currStr.mid(int(i) * 2 + 9, 2)));

                if (counter != checkSUM)
                {
                    QMessageBox::about(this, tr("Error"), tr("Checksum error!"));
                    file.close();
                    fileName.clear();
                    hexEdit->setData(chipData);
                    ui->progressBar->setValue(0);
                    ui->crcEdit->setText(getCRC32(chipData));
                    return;
                }
        }
        if (command == 4) //Changing the high address
        {
            strVal.clear();            //low address
            strVal = currStr.mid(9,4);
            hi_addr = hexToInt(strVal);
            counter = counter + static_cast<unsigned char>((hi_addr) >> 8) + static_cast<unsigned char>(hi_addr & 0x00ff);
            counter = 255 - counter + 1;
            checkSUM = static_cast<unsigned char>(hexToInt( currStr.mid(13, 2)));
            ui->progressBar->setValue(int(hi_addr * 256 * 256));
            if (counter != checkSUM)
            {
                QMessageBox::about(this, tr("Error"), tr("Checksum error!"));
                file.close();
                fileName.clear();
                hexEdit->setData(chipData);
                ui->progressBar->setValue(0);
                ui->crcEdit->setText(getCRC32(chipData));
                return;
            }
        }


    }
    file.close();
    fileName.clear();
    hexEdit->setData(chipData);
    ui->progressBar->setValue(0);
    ui->crcEdit->setText(getCRC32(chipData));
}

void MainWindow::on_actionFill_test_image_triggered()
{
    int addrSize, txtSize, i, j, divider;
    uint64_t curPos, fileSize, jj, hiDigit;
    char k;
    fileSize = static_cast<uint64_t>(chipData.size());
    if (fileSize > 512) divider = 9;
    else divider = 0;
    ui->progressBar->setValue(0);
    ui->progressBar->setRange(0, (static_cast<int>(fileSize >> divider)));
    addrSize = 0;
    jj = fileSize;
    hiDigit = 1;
    while (jj > 1)
    {
        jj = jj >> 4;
        addrSize ++;
        hiDigit = hiDigit << 4;
    }
    char digits[16];
    txtSize = 16 - addrSize - 4;
    curPos = 0;
    chipData.resize(0);
           k = 0x40;
           while (curPos < fileSize)
           {
              //String
              chipData.append('<');
              chipData.append('0');
              chipData.append('x');

              //calculate digits
              i = static_cast<int>(hiDigit >> 4);
              for (j=addrSize - 1; j >= 0; j--)
              {
                  digits[j] = (curPos / static_cast<uint64_t>(i)) % 16;
                  i = i >> 4;
              }
              for (j = addrSize -1; j >=0; j--)
              {
                 if (digits[j] < 10) digits[j] = digits[j] + 0x30;
                 else digits[j] = digits[j] + 0x37;
                 chipData.append(digits[j]);
              }
              chipData.append('>');
              for (i = 0; i < txtSize; i++)
              {
                  chipData.append(k);
                  k ++;
                  if (k > 0x7e) k = 0x40;
              }
              curPos = curPos + 16;
              if (curPos % 512 == 0) ui->progressBar->setValue(static_cast<int>(curPos >> divider));
           }
    hexEdit->setData(chipData);
    ui->crcEdit->setText(getCRC32(chipData));
    ui->progressBar->setValue(0);
}

void MainWindow::on_actionCompare_files_triggered()
{
    DialogCompare* compDialog = new DialogCompare(this);
    compDialog->showArrays(&chipData, &oldChipData, &newFileName, &oldFileName);
    compDialog->show();
}

void MainWindow::preparingToCompare(bool type)
{
    // For comparing function
    // type = 0 - file reading
    // type = 1 - chip reading
    if (filled == 0) oldChipData = hexEdit->data();
    if (type == 0)
    {
        if (numberOfReads > 0)
        {
           numberOfReads = 0;
        }
        newFileName = fileName;
    }
    else
    {
        newFileName = ui->comboBox_name->currentText();
    }
}


void MainWindow::on_actionCopy_triggered()
{
        QKeyEvent* keyPress = new QKeyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, "C");
        QKeyEvent* keyRelease = new QKeyEvent(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, "C");

        QCoreApplication::postEvent(hexEdit, keyPress);
        QCoreApplication::postEvent(hexEdit, keyRelease);
}

void MainWindow::on_actionPaste_triggered()
{
        QKeyEvent* keyPress = new QKeyEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier, "V");
        QKeyEvent* keyRelease = new QKeyEvent(QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier, "V");

        QCoreApplication::postEvent(hexEdit, keyPress);
        QCoreApplication::postEvent(hexEdit, keyRelease);
}

void MainWindow::on_actionBad_block_management_triggered()
{
    if ((currentChipSize != 0) && (currentPageSize != 0) && (currentBlockSize !=0))
    {
        DialogBBM* bbmDialog = new DialogBBM(this);
        bbmDialog->show();
        bbmDialog->getSectorSize(currentPageSize);
        bbmDialog->getBlockSize(currentBlockSize);
        bbmDialog->getTotalBlocks(currentChipSize / currentBlockSize);
        bbmDialog->getSettings(nandSettings);
        bbmDialog->setDeviceType(current_programmer);
        connect(bbmDialog, SIGNAL(sendNandParam(uint8_t)), this, SLOT(receiveNandStatus(uint8_t)));
    }
    else QMessageBox::about(this, tr("Error"), tr("Please select the chip parameters - manufacture and chip name."));

}

void MainWindow::receiveNandStatus(uint8_t setParam)
{
    nandSettings = setParam;
}

void MainWindow::on_actionCH341A_B_v1_2_triggered()
{
    current_programmer = 0;
    ui->lStatus->setText("CH341A/B");
    SetItemStatus("comboBox_type", 2, false);
    SetItemStatus("comboBox_vcc", 3, true);
    applyPlatformRestrictions();
}

void MainWindow::on_actionCH341A_v1_7_triggered()
{
    current_programmer = 1;
    ui->lStatus->setText("CH341A/B");
    SetItemStatus("comboBox_type", 2, false);
    SetItemStatus("comboBox_vcc", 3, false);
    applyPlatformRestrictions();
}

void MainWindow::on_actionCH347T_triggered()
{
    current_programmer = 2;
    ui->lStatus->setText("CH347T");
    SetItemStatus("comboBox_type", 2, true);
    SetItemStatus("comboBox_vcc", 3, true);
}

void MainWindow::on_actionCH347T_v1_1_triggered()
{
    current_programmer = 3;
    ui->lStatus->setText("CH347T");
    SetItemStatus("comboBox_type", 2, true);
    SetItemStatus("comboBox_vcc", 3, false);
}

void MainWindow::selectCH347F(unsigned char programmer)
{
    // CH347F (USB 1A86:55DE, SPI/I2C on interface 4). Same SPI/I2C protocol as CH347T.
    // The presets differ only in default SPI clocks (see getInterfaceSpeed()).
    current_programmer = programmer;
    ui->lStatus->setText("CH347F");
    SetItemStatus("comboBox_type", 2, true);
    SetItemStatus("comboBox_vcc", 3, false);
    defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
}

void MainWindow::on_actionCH347F_triggered() { selectCH347F(PROG_CH347F_STD); }
void MainWindow::on_actionCH347F_fast_triggered() { selectCH347F(PROG_CH347F_FAST); }
void MainWindow::on_actionCH347F_safe_triggered() { selectCH347F(PROG_CH347F_SAFE); }

void MainWindow::on_actionFT232H_v1_2_triggered()
{
    current_programmer = 4;
    ui->lStatus->setText("FT232H");
    SetItemStatus("comboBox_type", 2, false);
    SetItemStatus("comboBox_vcc", 3, false);
}

void MainWindow::closeEvent(QCloseEvent( *event))
{
    if (ui->actionStop->isEnabled())
    {
        // A window-close request must not leave a hidden erase/program loop running.
        // Stop at the next checked block boundary and keep the window for review.
        event->ignore();
        on_actionStop_triggered();
        return;
    }
    //Storing parameters in ini file
    if (lastDirectory.isEmpty()) lastDirectory = QDir::homePath();
    QString iniPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("config.ini");
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.beginGroup("Chip");
    settings.setValue("ChipDirectory", lastDirectory);
    settings.endGroup();
    settings.beginGroup("Device");
    settings.setValue("ProgrammerType", current_programmer);
    settings.setValue("SpiClockKHz", spiClockChoice);
    settings.endGroup();
    settings.beginGroup("FormPosition");
    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    hexEdit->setGeometry(0, 0, ui->frame->width(), ui->frame->height());
}

void MainWindow::SetItemStatus(QString comboboxName, int itemNumber, bool setDisable)
{
    QComboBox* comboBox = findChild<QComboBox*>(comboboxName);

    if (!comboBox) {
        qWarning() << "ComboBox" << comboboxName << "not found!";
        return;
    }

    if (itemNumber < 0 || itemNumber >= comboBox->count()) {
        qWarning() << "Item number" << itemNumber << "out of range";
        return;
    }

    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(comboBox->model());

    if (!model) {
        model = new QStandardItemModel(comboBox);
        for (int i = 0; i < comboBox->count(); ++i) {
            QStandardItem* item = new QStandardItem(comboBox->itemText(i));
            item->setData(comboBox->itemData(i), Qt::UserRole);
            model->appendRow(item);
        }
        comboBox->setModel(model);
    }

    QStandardItem* item = model->item(itemNumber);

    if (item) {
        if (setDisable) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setForeground(QBrush(Qt::gray));
        } else {
            item->setFlags(item->flags() | Qt::ItemIsEnabled);
            item->setForeground(QBrush(comboBox->palette().color(QPalette::Text)));
        }
    }
}

void MainWindow::on_actionShow_programmer_version_triggered()
{
    std::shared_ptr<uint8_t[]> buf(new uint8_t[0x12]);
    statusCH341 = ProgDeviceInit(current_programmer, currentChipType,
        currentChipType == 1 ? currentI2CBusSpeed : getInterfaceSpeed(current_programmer, currentChipType, currentDelay));
    if (statusCH341 == 0)
    {
        getDeviceDescriptor(buf.get(), current_programmer);
        ProgDeviceClose(current_programmer);
        QMessageBox::about(this, tr("Info:"), (tr("Device revision: ") + byteBCDPrint(buf[13], false) + "." + byteBCDPrint(buf[12], true)));
    }
    else showConnectionError();
}

void MainWindow::on_actionCheck_erase_triggered()
{
    lastOperationSucceeded = false;
    if (!validateNandOperation(false, false)) return;
    if (!ensureClockTuned()) return;
    QStringList skippedBadBlocks;   // NAND blocks skipped as bad (see below)
    //Verification of data erasure from the chip
    int res = 0;
    uint32_t step, numBlocks;

    if (currentChipType != 1)
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, defaultSpeed);
        qDebug() << "Interface speed" << defaultSpeed << " KHZ\n";
    }
    else
    {
        statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
        qDebug() << "Interface speed" << currentI2CBusSpeed << " KHZ\n";
    }

    //statusCH341 = ProgDeviceInit(current_programmer, currentChipType, currentI2CBusSpeed);
    if (statusCH341 == 0)
    {
       if (((currentNumBlocks > 0) && (currentBlockSize >0) && (currentChipType == 0)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 1)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 2)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 3)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 4)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 5)) ||
            ((currentNumBlocks > 0) && (currentPageSize >0) && (currentChipType == 6)))
           {
               ui->crcEdit->setText("");
               doNotDisturb();
               switch (currentChipType)
                             {
                             case 0:                 //SPI
                                step = currentBlockSize;
                                numBlocks = currentNumBlocks;
                             break;
                             case 1:                 //I2C
                                //step = currentPageSize;
                                step = 128;
                                numBlocks = currentChipSize / step;
                             break;
                             case 2:                 //MicroWire
                             case 3:                 //25xxx
                             case 4:                 //95xxx
                             case 5:                 //45xx
                                step = currentPageSize;
                                numBlocks = currentChipSize / step;
                             break;
                             case 6:             //NAND 35xx, GD5xx, W25xx
                                step = currentPageSize + (nandRaw ? currentECCsize : 0);
                                numBlocks = currentChipSize / currentPageSize;
                                if (!prepareNandOperation(tr("Erase check")))
                                {
                                    ProgDeviceClose(current_programmer);
                                    doNotDisturbCancel();
                                    clearCheckboxes();
                                    return;
                                }
                             break;
                             default:
                                //Unsupport
                                QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                                doNotDisturbCancel();
                                ProgDeviceClose(current_programmer);
                                ui->checkBox_1->setStyleSheet("");
                             return;
                             }
               ch341StatusFlashing();
               uint32_t addr = 0;
               uint32_t curBlock = 0;
               uint32_t j, k;
               //progerssbar settings
               int currentPercent = 0;
               int lastPercent = -1;
               ui->progressBar->setRange(0, 100);
               ui->progressBar->setValue(0);
               std::shared_ptr<uint8_t[]> buf(new uint8_t[step]);
               ui->checkBox_1->setStyleSheet("QCheckBox{font-weight:800;}");
               ui->statusMessage->setText(tr("Erase checking in ") + ui->comboBox_name->currentText());
               opSpeedStart();
               // Erase leaves blocks marked bad untouched (unless bad-block
               // skipping is off), so the blank check skips them as well.
               const uint32_t pagesPerBlock = (currentChipType == 6 && currentPageSize) ? currentBlockSize / currentPageSize : 0;
               const bool skipBadBlocks = currentChipType == 6 && pagesPerBlock && (nandSettings & 0xf0) == 0x00;
               for (k = 0; k < numBlocks; k++)
               {
                   if (skipBadBlocks && k % pagesPerBlock == 0)
                   {
                       const int bad = nand_checkBadBlock(k / pagesPerBlock, currentPageSize, pagesPerBlock, current_programmer);
                       if (bad < 0)
                       {
                           QMessageBox::warning(this, tr("Error"), tr("Bad-block check failed. Erase check stopped."));
                           ui->checkBox_1->setStyleSheet("");
                           ProgDeviceClose(current_programmer);
                           doNotDisturbCancel();
                           clearCheckboxes();
                           return;
                       }
                       if (bad == 1)
                       {
                           skippedBadBlocks << QString::number(k / pagesPerBlock);
                           addr += step * pagesPerBlock;
                           curBlock += pagesPerBlock;
                           k += pagesPerBlock - 1;
                           continue;
                       }
                   }
                   switch (currentChipType)
                      {
                      case 0:
                         //SPI
                         res = snor_read_param(buf.get(),curBlock * step, step, step, currentAddr4bit, current_programmer);
                      break;
                      case 1:
                         //I2C
                         if (current_programmer != 4) res = ch34xi2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm, current_programmer);
                         else res = ft232I2cBlockRead(buf.get(), curBlock * step, step, currentAlgorithm);
                         if (res==0) res = 1;
                      break;
                      case 2:
                         //MicroWire
                         if (current_programmer != 4) res = Read_EEPROM_3wire_param(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), static_cast<int>(currentChipSize), currentAlgorithm);
                         else res = ft232MWReadBlock(buf.get(), static_cast<int>(curBlock * step), static_cast<int>(step), currentAlgorithm);
                         if (res==0) res = 1;
                      break;
                      case 3:
                         //25xxx
                      case 4:
                         //95xxx
                         res = s95_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
                      break;
                      case 5:
                         res = at45_read_param(buf.get(), curBlock * step, step, step, currentAlgorithm, current_programmer);
                      break;
                      case 6:
                         //NAND
                         res = nand_page_read(buf.get(), step, k);
                         if (res==0) res = 1;
                      break;
                      default:
                         //Unsupport
                         QMessageBox::about(this, tr("Error"), tr("Unsupported chip type!"));
                         doNotDisturbCancel();
                         ProgDeviceClose(current_programmer);
                         ui->checkBox_1->setStyleSheet("");
                      return;
                      }
                    // if res=-1 - error, stop
                    if (statusCH341 != 0)
                    {
                       showConnectionError();
                       doNotDisturbCancel();
                       ProgDeviceClose(current_programmer);
                       return;
                    }
                    if (res <= 0)
                    {
                        if (currentChipType == 6)
                        {
                            ui->label_nandInfo->setText(tr("NAND operation failed at block/page %1 (error %2).").arg(curBlock).arg(res));
                        }
                        QMessageBox::about(this, tr("Error"), tr("Error reading block ") + QString::number(curBlock));
                        ProgDeviceClose(current_programmer);
                        ui->pushButton->setStyleSheet(grnKeyStyle);
                        doNotDisturbCancel();
                        clearCheckboxes();
                        return;
                    }
                    for (j = 0; j < step; j++)
                    {
                      if ((buf[addr + j - k * step]) != 0xff)
                          {
                            //error compare
                            QMessageBox::about(this, tr("Error"), tr("Error erasing chip!\nAddress:   ") + hexiAddr(addr + j) + tr("\nNeed: FF") + tr("    Chip: ") + bytePrint(buf[addr + j - k * step]));
                            ui->statusMessage->setText("");
                            ui->checkBox_1->setStyleSheet("");
                            ProgDeviceClose(current_programmer);
                            doNotDisturbCancel();
                            clearCheckboxes();
                            return;
                           }
                     }
                     addr = addr + step;
                     curBlock++;
                     qApp->processEvents();
                     if (isHalted)
                     {
                         isHalted = false;
                         ProgDeviceClose(current_programmer);
                         doNotDisturbCancel();
                         return;
                     }
                     opSpeedUpdate(curBlock, numBlocks, step);
                     currentPercent = static_cast<int>((curBlock * 100.0) / numBlocks);
                     if (currentPercent != lastPercent)
                     {
                         ui->progressBar->setValue(currentPercent);
                         lastPercent = currentPercent;
                     }
                 }
             }
             else
             {
                //Not correct Number fnd size of blocks
               // Nothing was read, so this must not end as a successful operation.
               if ((currentChipType == 0) || (currentChipType > 4)) QMessageBox::about(this, tr("Error"), tr("Before reading from chip please press 'Detect' button."));
               else QMessageBox::about(this, tr("Error"), tr("Please select the chip parameters - manufacture and chip name."));
               ProgDeviceClose(current_programmer);
               clearCheckboxes();
               return;
             }
             doNotDisturbCancel();
             opSpeedFinish();
             ui->progressBar->setValue(0);
             ui->checkBox_1->setStyleSheet("");
             ProgDeviceClose(current_programmer);
             lastOperationSucceeded = true;
             finishNandRead(tr("Erase check complete"));
             if (!skippedBadBlocks.isEmpty())
             {
                 const QString note = tr("Erase check passed. %1 bad block(s) were skipped, as Erase leaves them untouched: %2")
                                          .arg(skippedBadBlocks.size()).arg(skippedBadBlocks.join(", "));
                 ui->statusMessage->setText(note);
                 qInfo().noquote() << note;
             }

    }
      else
      {
          ch341StatusFlashing();
          showConnectionError();
      }
}

void MainWindow::on_actionFill_with_code_triggered()
{
    DialogFill* fillDialog = new DialogFill(this);
    fillDialog->show();
    connect(fillDialog, SIGNAL(sendAddr4(QString)), this, SLOT(receiveAddr4(QString)));
}

void MainWindow::receiveAddr4(QString addressData)
{
    QStringList resultOfForm;

    resultOfForm = addressData.split("-");
    uint32_t startAddr, endAddr, length, fillingCode, typeOfEnd;
    startAddr =   hexToInt(resultOfForm[0]);
    endAddr =     hexToInt(resultOfForm[1]);
    fillingCode = hexToInt(resultOfForm[2]);
    typeOfEnd =   hexToInt(resultOfForm[3]);
    if (typeOfEnd == 1) endAddr = endAddr + startAddr;
    length = endAddr - startAddr;

    if (endAddr < startAddr)
    {
       QMessageBox::about(this, tr("Error"), tr("The end address must be greater than the starting address."));
       return;
    }
    if (static_cast<int>(endAddr) >  hexEdit->data().length())
    {
       QMessageBox::about(this, tr("Error"), tr("The ending address must not exceed the buffer size."));
       return;
    }
    chipData = hexEdit->data();
    char *point = chipData.data();
    std::fill(point + startAddr, point + endAddr, fillingCode);
    hexEdit->setData(chipData);
    ui->statusMessage->setText("");
}

void MainWindow::on_comboBox_raw_currentIndexChanged(int index)
{
    const bool isNand = ui->comboBox_type->currentData().isValid() &&
                        ui->comboBox_type->currentData().toInt() == 6;
    if (isNand && uiReady && hexEdit && (!filled || hexEdit->isModified()) && nandRaw != (index != 0) &&
            QMessageBox::warning(this, tr("Change dump layout"),
              tr("Changing main/OOB layout clears the current buffer. Save it first if needed. Continue?"),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        const QSignalBlocker blocker(ui->comboBox_raw);
        ui->comboBox_raw->setCurrentIndex(nandRaw ? 1 : 0);
        return;
    }
    const bool layoutChanged = nandRaw != (index != 0);
    nandRaw = index != 0;
    // Suggest the usual ECC setting for the layout; it can still be changed.
    // Main + OOB is normally a raw dump (ECC Off), main only a corrected read.
    if (layoutChanged)
    {
        const int eccIndex = ui->comboBox_onDieEcc->findData(nandRaw ? 0 : 1);
        if (eccIndex >= 0) ui->comboBox_onDieEcc->setCurrentIndex(eccIndex);
    }
    if (isNand && uiReady) on_comboBox_size_currentIndexChanged(0);
    updateNandControlVisibility();
    updateNandSelectionInfo();
}

void MainWindow::handleScroll()
{
    if (currentChipType != 6 || !nandRaw || !currentPageSize || !currentECCsize) return;
    if (currentChipSize > 0x3ffffff)
    {
        long posStart = 0, posEnd = 0, i;
        posStart = hexEdit->_bPosFirst;
        posEnd = hexEdit->_bPosLast + currentECCsize + 16;
        hexEdit->clearUserAreas();
        hexEdit->setCursorPosition(posStart *2);
        if(posStart > 0x40)
        {
            for (i = posStart; i <= posEnd; i = i + 16)
            {
                if (i % (currentPageSize + currentECCsize) == 0) hexEdit->addUserArea(i - currentECCsize, i, QColor(0, 0, 0, 255), QColor(255, 0, 0, 60));
            }
        }
    }
}

quint64 MainWindow::nandImageSize() const
{
    if (!currentPageSize) return 0;
    return quint64(currentChipSize / currentPageSize) *
            (quint64(currentPageSize) + (nandRaw ? currentECCsize : 0));
}

bool MainWindow::validateNandOperation(bool writing, bool checkBuffer)
{
    if (currentChipType != 6) return true;
    // Capture what the user currently sees, including block-size edits.
    currentChipSize = ui->comboBox_size->currentData().toUInt();
    currentPageSize = ui->comboBox_page->currentData().toUInt();
    currentBlockSize = ui->comboBox_block->currentData().toUInt();
    currentECCsize = ui->comboBox_ECC->currentData().toUInt() * 64;
    currentNumBlocks = currentPageSize ? currentChipSize / currentPageSize : 0;
    defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
    if (!currentChipSize || !currentPageSize || !currentBlockSize ||
        currentBlockSize % currentPageSize ||
        currentChipSize % currentBlockSize ||
        (nandRaw && !currentECCsize) || nandImageSize() > quint64((std::numeric_limits<int>::max)()))
    {
        QMessageBox::warning(this, tr("Error"), tr("Select valid NAND size, page, block and OOB parameters."));
        return false;
    }
    if (writing && nandReadIncomplete)
    {
        QMessageBox::warning(this, tr("Incomplete NAND read"),
            tr("Writing an incomplete NAND read is blocked. Complete a fresh read or explicitly load a complete image first."));
        return false;
    }
    if (writing && nandRaw && ui->comboBox_onDieEcc->currentData().toBool())
    {
        QMessageBox::warning(this, tr("Unsupported write combination"),
            tr("Main + OOB writing requires on-die ECC Off, because ECC On may replace bytes in the supplied OOB. Read and Verify allow all four combinations."));
        return false;
    }
    if (checkBuffer && quint64(hexEdit->data().size()) != nandImageSize())
    {
        QMessageBox::warning(this, tr("Buffer size mismatch"),
            tr("The buffer must contain exactly %1 bytes for the selected main/OOB layout. Reload the correct image before Write or Verify.").arg(nandImageSize()));
        return false;
    }
    return true;
}

bool MainWindow::prepareNandOperation(const QString &operation)
{
    u8 id[3] = {0, 0, 0};
    u8 readback = 0;
    const bool eccEnabled = ui->comboBox_onDieEcc->currentData().toBool();
    int result = nand_read_devid(id, 3, current_programmer);
    if (result != 0 || id[0] == 0 || id[0] == 0xff)
    {
        ui->label_nandInfo->setText(tr("NAND identification failed. Operation stopped."));
        QMessageBox::warning(this, tr("Error"), ui->label_nandInfo->text());
        return false;
    }
    const int eccMode = nand_ecc_status_mode_for_id(id[0]);
    const bool isWinbond = eccMode == NAND_ECC_STATUS_WINBOND;
    nand_set_ecc_status_mode(eccMode);
    nand_reset_ecc_read_status();
    // Winbond W25N parts need BUF=1 for page/column addressed reads
    // (continuous-read variants power up with BUF=0). Other B0 bits are kept.
    result = nand_configure_ecc(current_programmer, eccEnabled, isWinbond, &readback);
    if (result != 0)
    {
        ui->label_nandInfo->setText(tr("Cannot set on-die ECC (error %1, B0=0x%2). Operation stopped.")
                                    .arg(result).arg(readback, 2, 16, QLatin1Char('0')));
        QMessageBox::warning(this, tr("Error"), ui->label_nandInfo->text());
        return false;
    }
    if (isWinbond && (readback & 0x40))
    {
        ui->label_nandInfo->setText(tr("Winbond OTP mode is enabled (B0=0x%1). Operation stopped.")
                                    .arg(readback, 2, 16, QLatin1Char('0')));
        QMessageBox::warning(this, tr("Error"), ui->label_nandInfo->text());
        return false;
    }
    nandOperationDescription = tr("%1, ECC %2 (ID %3 %4 %5, B0=0x%6)")
        .arg(nandRaw ? tr("Main + OOB") : tr("Main only"))
        .arg(eccEnabled ? tr("on") : tr("off"))
        .arg(id[0], 2, 16, QLatin1Char('0')).arg(id[1], 2, 16, QLatin1Char('0'))
        .arg(id[2], 2, 16, QLatin1Char('0')).arg(readback, 2, 16, QLatin1Char('0'));
    ui->label_nandInfo->setText(operation + ": " + nandOperationDescription);
    qInfo().noquote() << operation << ":" << nandOperationDescription;
    return true;
}

void MainWindow::finishNandRead(const QString &operation)
{
    if (currentChipType != 6) return;
    QString summary = operation + ": " + nandOperationDescription;
    const unsigned int corrected = nand_get_ecc_corrected_pages();
    const unsigned int uncorrectable = nand_get_ecc_uncorrectable_pages();
    if (corrected) summary += tr("; %1 page(s) corrected by ECC").arg(corrected);
    if (uncorrectable) summary += tr("; %1 page(s) uncorrectable").arg(uncorrectable);
    ui->label_nandInfo->setText(summary);
    qInfo().noquote() << summary;
    if (!uncorrectable || !pagesPerBlockForReport()) return;

    // List the affected blocks (pages are numbered from 0).
    std::vector<u32> pages(NAND_ECC_MAX_RECORDED_PAGES);
    const unsigned int listed = nand_get_ecc_uncorrectable_page_list(pages.data(), NAND_ECC_MAX_RECORDED_PAGES);
    const u32 ppb = pagesPerBlockForReport();
    QStringList blocks;
    u32 lastBlock = 0xffffffffu;
    for (unsigned int i = 0; i < listed; ++i)
    {
        const u32 block = pages[i] / ppb;
        if (block == lastBlock) continue;
        lastBlock = block;
        if (blocks.size() >= 40) { blocks << "..."; break; }
        blocks << QString::number(block);
    }
    const unsigned int totalPages = currentPageSize ? currentChipSize / currentPageSize : 0;
    QString text = tr("On-die ECC could not correct %1 of %2 pages. Their data was read as-is (uncorrected) and the operation continued.\n\nAffected blocks: %3")
            .arg(uncorrectable).arg(totalPages).arg(blocks.join(", "));
    if (totalPages && uncorrectable * 4 > totalPages)
        text += "\n\n" + tr("Most pages failed. This usually means the data was written by a host controller with its own ECC and on-die ECC disabled. Read this chip with on-die ECC Off.");
    else
        text += "\n\n" + tr("A few failing pages are usually bad blocks or worn pages. Compare with a second read, or read with ECC Off to get raw data.");
    QMessageBox::warning(this, tr("On-die ECC"), text);
}

bool MainWindow::confirmNandWriteMode()
{
    if (currentChipType != 6 || nandRaw || ui->comboBox_onDieEcc->currentData().toBool()) return true;
    return QMessageBox::question(this, tr("On-die ECC is Off"),
            tr("Writing main data with on-die ECC Off leaves the spare area without valid ECC bytes. "
               "A device that reads this chip with on-die ECC On will then see uncorrectable pages.\n\n"
               "Only continue if the target uses its own (host) ECC. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

uint32_t MainWindow::pagesPerBlockForReport() const
{
    return currentPageSize ? currentBlockSize / currentPageSize : 0;
}

void MainWindow::updateNandControlVisibility()
{
    const bool isNand = ui->comboBox_type->currentData().isValid() &&
                        ui->comboBox_type->currentData().toInt() == 6;
    const bool enabled = isNand && !ui->actionStop->isEnabled();
    const QList<QWidget *> controls = {ui->label_11, ui->comboBox_ECC,
        ui->label_12, ui->comboBox_raw, ui->label_onDieEcc,
        ui->comboBox_onDieEcc, ui->label_nandInfo};
    for (QWidget *control : controls)
    {
        control->setVisible(isNand);
        control->setEnabled(enabled);
    }
    const bool hadOobScrollConnection = bool(scroll_connect);
    if (scroll_connect)
    {
        disconnect(scroll_connect);
        scroll_connect = QMetaObject::Connection();
    }
    if (hexEdit)
    {
        if (isNand && nandRaw)
            scroll_connect = connect(hexEdit->verticalScrollBar(), &QScrollBar::valueChanged,
                                     this, &MainWindow::handleScroll);
        else if (hadOobScrollConnection) hexEdit->clearUserAreas();
    }
}

void MainWindow::updateNandSelectionInfo()
{
    if (!uiReady) return;
    if (currentChipType != 6) return;
    const bool enabled = ui->comboBox_onDieEcc->currentData().toBool();
    ui->label_nandInfo->setText(tr("%1, ECC %2, %3 bytes")
        .arg(nandRaw ? tr("Main + OOB") : tr("Main only"))
        .arg(enabled ? tr("on") : tr("off"))
        .arg(nandImageSize()));
    if (nandReadIncomplete) ui->label_nandInfo->setText(ui->label_nandInfo->text() + " " + tr("Current buffer is an incomplete NAND read."));
    if (nandRaw && enabled)
        ui->label_nandInfo->setText(ui->label_nandInfo->text() + " " + tr("(OOB read with ECC On is not raw; writing needs ECC Off.)"));
}

void MainWindow::on_comboBox_onDieEcc_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    // Selection only. No device I/O and no buffer mutation until an operation starts.
    updateNandSelectionInfo();
}

void MainWindow::applyPlatformRestrictions()
{
    ui->actionConnection_diagnostics->setVisible(false);
#ifdef _WIN32
    ui->actionConnection_diagnostics->setVisible(true);
    if (current_programmer == 4 || current_programmer > PROG_CH347F_SAFE) current_programmer = 2;
    ui->actionCH341A_B_v1_2->setEnabled(true);
    ui->actionCH341A_v1_7->setEnabled(true);
    ui->actionCH341A_B_v1_2->setText(tr("CH341A/B (SPI)"));
    ui->actionCH341A_v1_7->setText(tr("CH341A/B v1.7 board (SPI)"));
    ui->actionCH341A_B_v1_2->setToolTip(tr("CH341A/B share the SPI/parallel USB identity. This selection does not set the physical voltage."));
    ui->actionCH341A_v1_7->setToolTip(ui->actionCH341A_B_v1_2->toolTip());
    ui->actionCH341A_B_v1_2->setChecked(current_programmer == 0);
    ui->actionCH341A_v1_7->setChecked(current_programmer == 1);
    ui->actionFT232H_v1_2->setEnabled(false);
    ui->actionShow_programmer_version->setEnabled(false);
    ui->actionCH347T->setChecked(current_programmer == 2);
    ui->actionCH347T_v1_1->setChecked(current_programmer == 3);
    ui->actionCH347F->setChecked(current_programmer == PROG_CH347F_STD);
    ui->actionCH347F_fast->setChecked(current_programmer == PROG_CH347F_FAST);
    ui->actionCH347F_safe->setChecked(current_programmer == PROG_CH347F_SAFE);
    SetItemStatus("comboBox_type", 1, true); // I2C is not included in this Windows port.
    SetItemStatus("comboBox_type", 2, true); // MicroWire is not included.
    setWindowTitle(tr("IMSProg 1.9.4 - CH341A/B, CH347T/F SPI Programmer"));
#endif
}

int MainWindow::programmerFamily() const
{
    if (current_programmer == 2 || current_programmer == 3) return FamilyCH347T;
    if (PROG_IS_CH347F(current_programmer)) return FamilyCH347F;
    return FamilyCH341;
}

void MainWindow::setupProgrammerMenu()
{
    // Replace the per-board entries with three families and a submenu of
    // fixed SPI clocks. The legacy actions stay as hidden objects.
    QMenu *menu = ui->menuProgrammer_2;
    for (QAction *legacy : {ui->actionCH341A_B_v1_2, ui->actionCH341A_v1_7, ui->actionCH347T,
                            ui->actionCH347T_v1_1, ui->actionCH347F_fast, ui->actionCH347F,
                            ui->actionCH347F_safe, ui->actionFT232H_v1_2})
        menu->removeAction(legacy);
    QAction *firstRemaining = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    programmerChoiceGroup = new QActionGroup(this);
    programmerChoiceGroup->setExclusive(true);

    const auto addChoice = [this](QMenu *submenu, int family, uint16_t clock, const QString &text) {
        QAction *action = submenu->addAction(text);
        action->setCheckable(true);
        action->setData(QVariantList{family, clock});
        programmerChoiceGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, family, clock] { chooseProgrammer(family, clock); });
        return action;
    };

    menuCH341 = new QMenu(tr("CH341A/B"), menu);
    menuCH341->setObjectName("menuCH341");
    QAction *ch341 = addChoice(menuCH341, FamilyCH341, 0, tr("Standard (clock fixed by the CH341, about 1.5 MHz)"));
    ch341->setToolTip(tr("The CH341 SPI clock cannot be changed."));

    const struct { uint16_t khz; const char *text; } clocks[] = {
        {0,     QT_TR_NOOP("Automatic (per chip type)")},
        {kClockAutoTune, QT_TR_NOOP("Auto-tune (measure this chip)")},
        {60000, QT_TR_NOOP("60 MHz (SPI NOR is limited to 30 MHz)")},
        {30000, QT_TR_NOOP("30 MHz")},
        {15000, QT_TR_NOOP("15 MHz")},
        {7500,  QT_TR_NOOP("7.5 MHz")},
        {3750,  QT_TR_NOOP("3.75 MHz")},
        {1875,  QT_TR_NOOP("1.875 MHz")},
    };
    menuCH347T = new QMenu(tr("CH347T"), menu);
    menuCH347T->setObjectName("menuCH347T");
    menuCH347F = new QMenu(tr("CH347F"), menu);
    menuCH347F->setObjectName("menuCH347F");
    for (QMenu *submenu : {menuCH347T, menuCH347F})
    {
        const int family = submenu == menuCH347T ? FamilyCH347T : FamilyCH347F;
        for (const auto &clock : clocks)
        {
            addChoice(submenu, family, clock.khz, tr(clock.text));
            if (!clock.khz) submenu->addSeparator();
        }
    }
    for (QMenu *submenu : {menuCH341, menuCH347T, menuCH347F})
        menu->insertMenu(firstRemaining, submenu);

    if (WchDriver::supported())
    {
        actionDriver = new QAction(tr("Driver..."), this);
        actionDriver->setObjectName("actionDriver");
        actionDriver->setIcon(style()->standardIcon(QStyle::SP_VistaShield));
        menu->addAction(actionDriver);
        connect(actionDriver, &QAction::triggered, this, &MainWindow::showDriverDialog);

        driverStatusButton = new QToolButton(this);
        driverStatusButton->setObjectName("driverStatusButton");
        driverStatusButton->setAutoRaise(true);
        driverStatusButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        driverStatusButton->setToolTip(tr("Programmer driver. Click for details or to install."));
        connect(driverStatusButton, &QToolButton::clicked, this, &MainWindow::showDriverDialog);
        ui->statusBar->insertPermanentWidget(1, driverStatusButton, 0);
    }
}

void MainWindow::updateProgrammerMenu()
{
    const int family = programmerFamily();
    if (programmerChoiceGroup)
        for (QAction *action : programmerChoiceGroup->actions())
        {
            const QVariantList data = action->data().toList();
            action->setChecked(data.size() == 2 && data[0].toInt() == family &&
                               (family == FamilyCH341 || data[1].toUInt() == spiClockChoice));
        }
    QString name = family == FamilyCH341 ? QStringLiteral("CH341A/B")
                 : family == FamilyCH347T ? QStringLiteral("CH347T") : QStringLiteral("CH347F");
    if (family != FamilyCH341)
    {
        if (spiClockChoice == kClockAutoTune)
            name += " " + tr("auto-tune") + (tunedClock ? " " + formatClock(tunedClock) : QString());
        else
            name += " " + (spiClockChoice ? QString::number(spiClockChoice / 1000.0, 'g', 4) + " MHz" : tr("auto"));
    }
    ui->lStatus->setText(name);
}

void MainWindow::chooseProgrammer(int family, uint16_t clockKHz)
{
    if (ui->actionStop->isEnabled()) { updateProgrammerMenu(); return; }
    switch (family)
    {
    case FamilyCH347T: on_actionCH347T_triggered(); break;
    case FamilyCH347F: selectCH347F(PROG_CH347F_STD); break;
    default: on_actionCH341A_B_v1_2_triggered(); break;
    }
    spiClockChoice = family == FamilyCH341 ? 0 : clockKHz;
    tunedClock = 0;
    tunedKey.clear();
    applySpiClockChoice();
    defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
    updateProgrammerMenu();
}

void MainWindow::refreshDriverStatus()
{
    if (!driverStatusButton) return;
    const WchDriver::Status status = WchDriver::installedStatus();
    if (status.installed)
    {
        driverStatusButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        driverStatusButton->setText(tr("Driver %1").arg(status.version.isEmpty() ? tr("installed") : status.version));
    }
    else
    {
        driverStatusButton->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        driverStatusButton->setText(tr("Driver not installed"));
    }
}

void MainWindow::showDriverDialog()
{
    if (ui->actionStop->isEnabled()) return;
    const WchDriver::Status status = WchDriver::installedStatus();
    const QString bundled = WchDriver::bundledVersion();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Programmer driver"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    QVBoxLayout layout(&dialog);
    QHBoxLayout header;
    QLabel icon;
    icon.setPixmap(style()->standardIcon(status.installed ? QStyle::SP_MessageBoxInformation
                                                          : QStyle::SP_MessageBoxWarning).pixmap(32, 32));
    icon.setAlignment(Qt::AlignTop);
    QLabel headline(status.installed ? tr("The programmer driver is installed.")
                                     : tr("The programmer driver is not installed."));
    QFont bold = headline.font();
    bold.setBold(true);
    headline.setFont(bold);
    header.addWidget(&icon);
    header.addWidget(&headline, 1);
    layout.addLayout(&header);

    QFormLayout form;
    QLabel installedLabel(status.installed ? (status.version.isEmpty() ? tr("Yes") : status.version) : tr("No"));
    QLabel bundledLabel(bundled.isEmpty() ? tr("Not included") : bundled);
    QLabel devicesLabel(tr("CH341A/B, CH347T, CH347F (SPI/I2C interface)"));
    QLabel publisherLabel(tr("WCH (wch.cn), signed by Microsoft"));
    form.addRow(tr("Installed version:"), &installedLabel);
    form.addRow(tr("Included version:"), &bundledLabel);
    form.addRow(tr("Devices:"), &devicesLabel);
    form.addRow(tr("Publisher:"), &publisherLabel);
    layout.addLayout(&form);

    QLabel note(tr("Installing requires administrator rights; Windows will ask for permission. "
                   "The serial (COM) ports of the CH347 are not changed."));
    note.setWordWrap(true);
    layout.addWidget(&note);

    QDialogButtonBox buttons;
    QPushButton *install = buttons.addButton(status.installed ? tr("Reinstall driver") : tr("Install driver"),
                                             QDialogButtonBox::AcceptRole);
    install->setIcon(style()->standardIcon(QStyle::SP_VistaShield));
    install->setEnabled(!bundled.isEmpty());
    install->setDefault(!status.installed);
    buttons.addButton(QDialogButtonBox::Close);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(install, &QPushButton::clicked, &dialog, [&] {
        install->setEnabled(false);
        buttons.setEnabled(false);
        headline.setText(tr("Installing driver..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool timerWasActive = timer->isActive();
        timer->stop();
        QString detail;
        const WchDriver::Result result = WchDriver::install(&dialog, &detail);
        QApplication::restoreOverrideCursor();
        if (timerWasActive) timer->start();
        buttons.setEnabled(true);
        refreshDriverStatus();
        const WchDriver::Status now = WchDriver::installedStatus();
        installedLabel.setText(now.installed ? (now.version.isEmpty() ? tr("Yes") : now.version) : tr("No"));
        switch (result)
        {
        case WchDriver::Installed:
            icon.setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
            headline.setText(tr("Driver installed. Reconnect the programmer if it is plugged in."));
            install->setText(tr("Reinstall driver"));
            break;
        case WchDriver::InstalledRebootRequired:
            icon.setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
            headline.setText(tr("Driver installed. Restart Windows to finish."));
            break;
        case WchDriver::Cancelled:
            headline.setText(tr("Installation cancelled."));
            break;
        case WchDriver::Failed:
            icon.setPixmap(style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(32, 32));
            headline.setText(tr("Driver installation failed. %1").arg(detail));
            break;
        }
        install->setEnabled(true);
    });
    dialog.exec();
}

void MainWindow::showConnectionError()
{
    QString message = tr("Programmer ") + ui->lStatus->text() + tr(" is not connected!");
#ifdef _WIN32
    const QString reason = QString::fromUtf8(ch347_windows_last_error());
    if (!reason.isEmpty()) message += "\n\n" + reason;
    if (!WchDriver::installedStatus().installed && !WchDriver::bundledInfPath().isEmpty())
        message += "\n\n" + tr("The programmer driver is not installed. Install it from Programmer > Driver.");
    else
        message += "\n\n" + tr("Open Programmer > Programmer connection diagnostics to copy the selected programmer's driver/device report.");
#endif
    QMessageBox::warning(this, tr("Error"), message);
}

void MainWindow::on_actionConnection_diagnostics_triggered()
{
#ifdef _WIN32
    if (ui->actionStop->isEnabled()) return;
    const bool timerWasActive = timer->isActive();
    timer->stop();
    // Backend enumeration only: no SPI setup, NAND command or driver change.
    if (current_programmer >= 2) ch347_select_model(PROG_IS_CH347F(current_programmer) ? CH347_MODEL_F : CH347_MODEL_T);
    if (current_programmer < 2) ch341_windows_diagnose();
    else ch347_windows_diagnose();
    const QString report = QString::fromUtf8(ch347_windows_last_report());
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Programmer connection diagnostics"));
    dialog.resize(780, 520);
    QVBoxLayout layout(&dialog);
    QLabel description(tr("Driver/device enumeration only. No SPI commands or chip read/write/erase operations are sent."));
    description.setWordWrap(true);
    QPlainTextEdit details;
    details.setReadOnly(true);
    details.setPlainText(report);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    QPushButton *copy = buttons.addButton(tr("Copy diagnostics"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, [&report] { QApplication::clipboard()->setText(report); });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addWidget(&description);
    layout.addWidget(&details);
    layout.addWidget(&buttons);
    dialog.exec();
    if (timerWasActive) timer->start();
#endif
}

// ---------------------------------------------------------------------------
// Verified read
//
// 1. Read the whole chip twice at the selected clock and compare block by block.
//    Blocks that match in both reads are taken as they are: a transfer error
//    almost never repeats identically.
// 2. Blocks that differ are re-read twice at each lower clock (CH347 SPI: halve
//    down to 1.875 MHz; I2C: 400/100/20 kHz) until both reads agree.
// 3. Blocks that still differ at the lowest clock hold unstable bits (typically
//    weak NAND cells read with on-die ECC Off). They are read 7 more times and
//    every bit is set by majority vote; the report counts the unstable bits.
// Only read commands are sent, the same ones as the normal Read.
// ---------------------------------------------------------------------------

QString MainWindow::formatClock(uint16_t khz) const
{
    if (khz >= 1000) return QString::number(khz / 1000.0, 'g', 4) + " MHz";
    return QString::number(khz) + " kHz";
}

bool MainWindow::readGeometry(uint32_t &numBlocks, uint32_t &step, uint32_t &sectorsPerBlock) const
{
    sectorsPerBlock = 0;
    if (!currentNumBlocks) return false;
    switch (currentChipType)
    {
    case 0:                 //SPI
        if (!currentBlockSize) return false;
        step = currentBlockSize;
        numBlocks = currentNumBlocks;
        return true;
    case 1:                 //I2C
        if (!currentPageSize) return false;
        step = 128;
        numBlocks = currentChipSize / step;
        return numBlocks > 0;
    case 2: case 3: case 4: case 5:
        if (!currentPageSize) return false;
        step = currentPageSize;
        numBlocks = currentChipSize / step;
        return numBlocks > 0;
    case 6:                 //NAND
        if (!currentPageSize || !currentBlockSize) return false;
        numBlocks = currentChipSize / currentBlockSize;
        sectorsPerBlock = currentBlockSize / currentPageSize;
        step = nandRaw ? currentBlockSize + currentECCsize * sectorsPerBlock : currentBlockSize;
        return numBlocks > 0;
    default:
        return false;
    }
}

// Same calls as the Read loop. Returns > 0 on success, <= 0 on error.
int MainWindow::readChipUnit(uint8_t *buf, uint32_t k, uint32_t step, uint32_t sectorsPerBlock)
{
    int res = 0;
    switch (currentChipType)
    {
    case 0:
        res = snor_read_param(buf, k * step, step, step, currentAddr4bit, current_programmer);
        break;
    case 1:
        if (current_programmer != 4) res = ch34xi2cBlockRead(buf, k * step, step, currentAlgorithm, current_programmer);
        else res = ft232I2cBlockRead(buf, k * step, step, currentAlgorithm);
        if (res == 0) res = 1;
        break;
    case 2:
        if (current_programmer != 4) res = Read_EEPROM_3wire_param(buf, static_cast<int>(k * step), static_cast<int>(step), static_cast<int>(currentChipSize), currentAlgorithm);
        else res = ft232MWReadBlock(buf, static_cast<int>(k * step), static_cast<int>(step), currentAlgorithm);
        if (res == 0) res = 1;
        break;
    case 3:
    case 4:
        res = s95_read_param(buf, k * step, step, step, currentAlgorithm, current_programmer);
        break;
    case 5:
        res = at45_read_param(buf, k * step, step, step, currentAlgorithm, current_programmer);
        break;
    case 6:
        res = nand_block_read(buf, nandRaw ? currentPageSize + currentECCsize : currentPageSize, k, sectorsPerBlock, current_programmer);
        if (res == 0) res = 1;
        break;
    default:
        res = -1;
    }
    return res;
}

QVector<uint16_t> MainWindow::verifiedReadSpeeds(uint16_t start) const
{
    QVector<uint16_t> speeds{start};
    if (currentChipType == 1)
    {
        for (uint16_t khz : {400, 100, 20}) if (khz < start) speeds << khz;
        return speeds;
    }
    // MicroWire is bit-banged; CH341A/B and FT232H keep their fixed clock.
    if (currentChipType == 2 || programmerFamily() == FamilyCH341) return speeds;
    for (uint16_t khz : {30000, 15000, 7500, 3750, 1875}) if (khz < start) speeds << khz;
    return speeds;
}

bool MainWindow::openForVerifiedRead(uint16_t speed, const QString &operation)
{
    statusCH341 = ProgDeviceInit(current_programmer, currentChipType, speed);
    ch341StatusFlashing();
    if (statusCH341 != 0)
    {
        showConnectionError();
        return false;
    }
    qDebug() << "Verified read: interface speed" << speed << "kHz";
    if (currentChipType == 6 && !prepareNandOperation(operation.isEmpty() ? tr("Verified read") : operation))
    {
        ProgDeviceClose(current_programmer);
        return false;
    }
    return true;
}

void MainWindow::readAndVerify()
{
    if (ui->actionStop->isEnabled()) return;
    if (!validateNandOperation(false, false)) return;
    defaultSpeed = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
    if (!ensureClockTuned()) return;
    const uint16_t startSpeed = currentChipType == 1 ? currentI2CBusSpeed : defaultSpeed;
    uint32_t numBlocks = 0, step = 0, sectorsPerBlock = 0;
    if (!readGeometry(numBlocks, step, sectorsPerBlock))
    {
        if ((currentChipType == 0) || (currentChipType > 4))
            QMessageBox::about(this, tr("Error"), tr("Before reading from chip please press 'Detect' button."));
        else QMessageBox::about(this, tr("Error"), tr("Please select the chip parameters - manufacture and chip name"));
        return;
    }
    const quint64 total = quint64(numBlocks) * step;
    if (total > quint64((std::numeric_limits<int>::max)()))
    {
        QMessageBox::warning(this, tr("Error"), tr("The chip is too large for a verified read."));
        return;
    }
    const QVector<uint16_t> speeds = verifiedReadSpeeds(startSpeed);
    QByteArray first, second;
    try {
        first = QByteArray(int(total), char(0xff));
        second = QByteArray(int(total), char(0xff));
    } catch (const std::bad_alloc &) {
        QMessageBox::warning(this, tr("Error"), tr("Not enough memory for two copies of the chip."));
        return;
    }
    std::vector<uint8_t> x(step), y(step);

    newFileName = ui->comboBox_name->currentText();
    ui->crcEdit->setText("");
    doNotDisturb();
    if (!openForVerifiedRead(speeds[0]))
    {
        doNotDisturbCancel();
        clearCheckboxes();
        return;
    }
    if (numberOfReads > 0)
    {
        oldChipData = chipData;
        oldFileName = ui->comboBox_name->currentText();
    }
    if (currentChipType == 6)
    {
        chipData.resize(static_cast<int>(nandImageSize()));
        chipData.fill(char(0xff));
        nandReadIncomplete = true;
        filled = false;
        hexEdit->setData(chipData);
    }
    ui->pushButton->setStyleSheet(redKeyStyle);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    // Closes the programmer and restores the UI; used on every exit path.
    auto finish = [&]() {
        opSpeedFinish();
        ProgDeviceClose(current_programmer);
        ui->progressBar->setValue(0);
        ui->pushButton->setStyleSheet(grnKeyStyle);
        doNotDisturbCancel();
    };
    // Reads one block; 0 = ok, -1 = error (already reported), 1 = stopped by the user.
    auto readOne = [&](uint8_t *dst, uint32_t k) -> int {
        const int res = readChipUnit(dst, k, step, sectorsPerBlock);
        if (statusCH341 != 0) { showConnectionError(); return -1; }
        if (res <= 0)
        {
            if (currentChipType == 6)
                ui->label_nandInfo->setText(tr("Read failed at block %1 (error %2). Buffer is incomplete; unread bytes are FF.").arg(k).arg(res));
            QMessageBox::about(this, tr("Error"), tr("Error reading block ") + QString::number(k));
            return -1;
        }
        qApp->processEvents();
        if (isHalted) { isHalted = false; return 1; }
        return 0;
    };
    auto setProgress = [&](quint64 done, quint64 all) {
        ui->progressBar->setValue(all ? static_cast<int>(done * 100 / all) : 0);
    };
    auto readPass = [&](QByteArray &dst, const QString &label) -> int {
        ui->statusMessage->setText(label);
        opSpeedStart();
        for (uint32_t k = 0; k < numBlocks; k++)
        {
            const int r = readOne(reinterpret_cast<uint8_t *>(dst.data()) + quint64(k) * step, k);
            if (r) return r;
            if (&dst == &first && currentChipType != 6 && quint64(k) * step < 0x500)
                hexEdit->setData(dst);
            opSpeedUpdate(k + 1, numBlocks, step);
            setProgress(k + 1, numBlocks);
        }
        return 0;
    };

    QElapsedTimer elapsed;
    elapsed.start();
    const QString chip = ui->comboBox_name->currentText();
    if (readPass(first, tr("Verified read: pass 1 of 2 at %1 from %2").arg(formatClock(speeds[0]), chip)))
    { finish(); return; }
    // ECC statistics then describe one full pass (plus any re-reads).
    if (currentChipType == 6) nand_reset_ecc_read_status();
    if (readPass(second, tr("Verified read: pass 2 of 2 at %1 from %2").arg(formatClock(speeds[0]), chip)))
    { finish(); return; }

    QVector<uint32_t> mismatched;
    for (uint32_t k = 0; k < numBlocks; k++)
        if (memcmp(first.constData() + quint64(k) * step, second.constData() + quint64(k) * step, step))
            mismatched << k;
    second = QByteArray();  // the result is assembled in "first"
    const int initialMismatches = mismatched.size();

    int fixedBySlowerClock = 0;
    uint16_t slowestClockNeeded = 0;
    uint16_t currentClock = speeds[0];
    for (int si = 1; si < speeds.size() && !mismatched.isEmpty(); ++si)
    {
        ProgDeviceClose(current_programmer);
        if (!openForVerifiedRead(speeds[si])) { doNotDisturbCancel(); ui->pushButton->setStyleSheet(grnKeyStyle); ui->progressBar->setValue(0); return; }
        currentClock = speeds[si];
        ui->statusMessage->setText(tr("Verified read: re-reading %1 differing block(s) at %2")
                                   .arg(mismatched.size()).arg(formatClock(currentClock)));
        QVector<uint32_t> stillDifferent;
        for (int i = 0; i < mismatched.size(); ++i)
        {
            const uint32_t k = mismatched[i];
            int r = readOne(x.data(), k);
            if (!r) r = readOne(y.data(), k);
            if (r) { finish(); return; }
            if (!memcmp(x.data(), y.data(), step))
            {
                memcpy(first.data() + quint64(k) * step, x.data(), step);
                ++fixedBySlowerClock;
                slowestClockNeeded = currentClock;
            }
            else stillDifferent << k;
            setProgress(i + 1, mismatched.size());
        }
        mismatched = stillDifferent;
    }

    // Bitwise majority vote over fresh reads at the slowest clock.
    const int votes = 7;
    quint64 unstableBits = 0, nearlyTiedBits = 0;
    QStringList unstableBlocks;
    if (!mismatched.isEmpty())
    {
        ui->statusMessage->setText(tr("Verified read: %1 block(s) still differ at %2, reading each %3 times for a majority vote")
                                   .arg(mismatched.size()).arg(formatClock(currentClock)).arg(votes));
        std::vector<std::vector<uint8_t>> reads(votes, std::vector<uint8_t>(step));
        for (int i = 0; i < mismatched.size(); ++i)
        {
            const uint32_t k = mismatched[i];
            for (int v = 0; v < votes; ++v)
                if (readOne(reads[v].data(), k)) { finish(); return; }
            uint8_t *out = reinterpret_cast<uint8_t *>(first.data()) + quint64(k) * step;
            for (uint32_t j = 0; j < step; ++j)
            {
                uint8_t value = 0;
                for (int bit = 0; bit < 8; ++bit)
                {
                    int ones = 0;
                    for (int v = 0; v < votes; ++v) ones += (reads[v][j] >> bit) & 1;
                    if (ones * 2 > votes) value |= uint8_t(1u << bit);
                    if (ones != 0 && ones != votes) ++unstableBits;
                    if (ones == votes / 2 || ones == votes / 2 + 1) ++nearlyTiedBits;
                }
                out[j] = value;
            }
            if (unstableBlocks.size() < 40) unstableBlocks << QString::number(k);
            else if (unstableBlocks.size() == 40) unstableBlocks << "...";
            setProgress(i + 1, mismatched.size());
        }
    }

    chipData = first;
    first = QByteArray();
    hexEdit->setData(chipData);
    nandReadIncomplete = false;
    const QString crc = getCRC32(chipData);
    ui->crcEdit->setText(crc);
    newFileName = chip;
    finish();
    filled = 0;
    numberOfReads++;
    finishNandRead(tr("Verified read complete"));

    const QString unit = currentChipType == 6 ? tr("block(s)") : tr("segment(s)");
    const QString duration = formatDuration((elapsed.elapsed() + 500) / 1000);
    QString text;
    if (!initialMismatches)
    {
        text = tr("Both reads are identical at %1.").arg(formatClock(speeds[0]));
        ui->statusMessage->setText(tr("Verified read OK: 2 identical reads at %1, CRC32 %2, %3")
                                   .arg(formatClock(speeds[0]), crc, duration));
    }
    else
    {
        text = tr("The two reads at %1 differed in %2 of %3 %4.")
                   .arg(formatClock(speeds[0])).arg(initialMismatches).arg(numBlocks).arg(unit);
        if (fixedBySlowerClock)
            text += "\n\n" + tr("%1 of them read identically twice after lowering the clock (down to %2); those reads were used. Read this chip at %2 or lower next time.")
                                 .arg(fixedBySlowerClock).arg(formatClock(slowestClockNeeded));
        if (!mismatched.isEmpty())
        {
            text += "\n\n" + tr("%1 %2 still differed at %3. Each was read %4 more times and every bit was set by majority vote: %5 unstable bit(s), %6 of them nearly tied (3:4).")
                                 .arg(mismatched.size()).arg(unit).arg(formatClock(currentClock)).arg(votes)
                                 .arg(unstableBits).arg(nearlyTiedBits);
            text += "\n" + tr("Affected: %1").arg(unstableBlocks.join(", "));
            if (currentChipType == 6 && !ui->comboBox_onDieEcc->currentData().toBool())
                text += "\n\n" + tr("With on-die ECC Off this is usually weak cells in the chip, not the connection. The chip's ECC corrects such single-bit errors; read with ECC On to see the corrected data.");
            else
                text += "\n\n" + tr("Check the wiring, clip contact and power supply, then read again.");
        }
        ui->statusMessage->setText(tr("Verified read: %1 differing %2 resolved, CRC32 %3, %4")
                                   .arg(initialMismatches).arg(unit).arg(crc, duration));
    }
    text += "\n\nCRC32: " + crc;
    if (mismatched.isEmpty()) QMessageBox::information(this, tr("Verified read"), text);
    else QMessageBox::warning(this, tr("Verified read"), text);
}

// ---------------------------------------------------------------------------
// Clock auto-tune
//
// Finds the fastest CH347 clock at which this chip, with this wiring, reads
// back identically. Only read commands are sent:
//  - SPI NAND: one PAGE READ (13h) puts a page with varied data into the
//    chip's cache register; the cache (03h), the JEDEC ID and the A0/B0
//    feature registers are then read again and again. The cache does not
//    change, so a difference can only be a transfer error, never a weak cell.
//  - SPI NOR: one 4 KB area with varied data and the JEDEC ID are re-read.
// (A Winbond chip still in continuous-read mode is switched to buffer mode
// first, which every operation does anyway.)
// The reference is taken at 1.875 MHz. Clocks are tried from the fastest
// down; the first clock that passes a short and then a longer run is used
// without further margin (the verify step re-checks mismatches at lower
// clocks, see on_actionVerify_triggered()).
// ---------------------------------------------------------------------------

void MainWindow::applySpiClockChoice()
{
    if (spiClockChoice == kClockAutoTune) setSpiClockOverride(tunedClock);  // 0 = per-chip default until measured
    else setSpiClockOverride(spiClockChoice);
}

bool MainWindow::ensureClockTuned()
{
    if (spiClockChoice != kClockAutoTune || programmerFamily() == FamilyCH341) return true;
    if (currentChipType != 0 && currentChipType != 6) return true;
    const QString key = QString("%1/%2/%3/%4").arg(current_programmer).arg(currentChipType)
                            .arg(ui->comboBox_name->currentText()).arg(currentPageSize);
    if (!tunedClock || key != tunedKey)
    {
        const QString previous = ui->statusMessage->text();
        ui->statusMessage->setText(tr("Auto-tune: measuring the fastest reliable clock for this chip..."));
        ui->progressBar->setRange(0, 0);
        // The periodic connection probe reopens and closes the programmer.
        // Fired from inside the measurement it closed the connection under
        // test, so every clock "failed" and the result was always 1.875 MHz.
        const bool timerWasActive = timer->isActive();
        timer->stop();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        QString detail;
        const uint16_t clock = measureFastestClock(&detail);
        if (timerWasActive) timer->start();
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(0);
        ch341StatusFlashing();
        if (!clock)
        {
            ui->statusMessage->setText(previous);
            QMessageBox::warning(this, tr("Auto-tune"), tr("Clock measurement failed: %1").arg(detail));
            return false;
        }
        tunedClock = clock;
        tunedKey = key;
        applySpiClockChoice();
        updateProgrammerMenu();
        ui->statusMessage->setText(tr("Auto-tune: %1 (%2)").arg(formatClock(clock), detail));
        qInfo().noquote() << "Auto-tune:" << formatClock(clock) << detail;
    }
    const uint16_t automatic = getInterfaceSpeed(current_programmer, currentChipType, currentDelay);
    defaultSpeed = automatic < tunedClock ? automatic : tunedClock;
    return true;
}

uint16_t MainWindow::measureFastestClock(QString *detail)
{
    const bool nand = currentChipType == 6;
    const uint16_t slowest = 1875;
    const u8 prog = current_programmer;
    const uint32_t length = nand ? (currentPageSize ? currentPageSize + currentECCsize : 2112u) : 4096u;
    std::vector<uint8_t> reference(length), probe(length);
    u8 idRef[3] = {0, 0, 0}, regRef[2] = {0, 0};
    uint32_t norOffset = 0;

    auto open = [&](uint16_t khz) {
        ProgDeviceClose(prog);
        statusCH341 = ProgDeviceInit(prog, currentChipType, khz);
        return statusCH341 == 0;
    };
    auto readId = [&](u8 *id) {
        u8 raw[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
        const int r = nand ? nand_read_devid(raw, 3, prog) : snor_read_devid(raw, 3, prog);
        memcpy(id, raw, 3);
        return r == 0;
    };
    auto readData = [&](uint8_t *out) {
        if (nand) return nand_read_cache(out, length, prog) == 0;
        return snor_read_param(out, norOffset, length, length, currentAddr4bit, prog) > 0;
    };
    auto readRegs = [&](u8 *regs) {
        if (!nand) return true;
        return nand_get_feature(0xa0, &regs[0], prog) == 0 && nand_get_feature(0xb0, &regs[1], prog) == 0;
    };
    auto variety = [](const std::vector<uint8_t> &data) {
        bool seen[256] = {false};
        int distinct = 0;
        for (uint8_t b : data) if (!seen[b]) { seen[b] = true; ++distinct; }
        return distinct;
    };

    if (!open(slowest)) { *detail = tr("the programmer is not connected"); ProgDeviceClose(prog); return 0; }
    if (!readId(idRef) || idRef[0] == 0x00 || idRef[0] == 0xff)
    { *detail = tr("the chip does not answer"); ProgDeviceClose(prog); return 0; }

    // Winbond W25N in continuous-read mode (BUF=0, the power-up state of some
    // variants) streams on into the following pages, so repeated cache reads
    // would differ at any clock. Select buffer mode first, as every operation
    // does; the ECC bit is left as it is.
    if (nand && idRef[0] == 0xef)
    {
        u8 b0 = 0;
        if (nand_get_feature(0xb0, &b0, prog) != 0 ||
            (!(b0 & 0x08) && nand_configure_ecc(prog, (b0 & 0x10) != 0, 1, &b0) != 0))
        { *detail = tr("the chip does not answer"); ProgDeviceClose(prog); return 0; }
    }

    // Pick test data with many different byte values (an all-FF page would
    // hide most transfer errors). Search at 7.5 MHz, then re-read the pick at
    // the slowest clock as the reference.
    {
        open(7500);
        int bestVariety = -1;
        uint32_t bestUnit = 0;
        const uint32_t units = nand ? 128 : 64;   // 128 pages / 256 KB of NOR
        for (uint32_t unit = 0; unit < units && bestVariety < 160; ++unit)
        {
            if (nand) { if (nand_load_page_to_cache(unit, prog)) break; }
            else norOffset = unit * length;
            if (!readData(probe.data())) break;
            const int v = variety(probe);
            if (v > bestVariety) { bestVariety = v; bestUnit = unit; }
            if ((unit & 15) == 15) qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        open(slowest);
        if (nand) { if (nand_load_page_to_cache(bestUnit, prog)) { *detail = tr("page read failed"); ProgDeviceClose(prog); return 0; } }
        else norOffset = bestUnit * length;
    }
    if (!readData(reference.data()) || !readData(probe.data()) || probe != reference || !readRegs(regRef))
    {
        ProgDeviceClose(prog);
        *detail = tr("reads differ even at %1; check the wiring").arg(formatClock(slowest));
        return slowest;
    }

    // Returns the number of bytes compared, or -1 on the first difference.
    auto stress = [&](qint64 budgetMs) -> qint64 {
        QElapsedTimer t;
        t.start();
        qint64 bytes = 0;
        int round = 0;
        while (t.elapsed() < budgetMs)
        {
            if (!readData(probe.data()) || probe != reference) return -1;
            bytes += length;
            if ((round & 7) == 0)
            {
                u8 id[3], regs[2] = {regRef[0], regRef[1]};
                if (!readId(id) || memcmp(id, idRef, 3)) return -1;
                if (!readRegs(regs) || regs[0] != regRef[0] || regs[1] != regRef[1]) return -1;
            }
            if ((++round & 31) == 0) qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        return bytes;
    };

    QStringList failed;
    uint16_t chosen = slowest;
    qint64 testedBytes = 0;
    for (uint16_t khz : {60000, 30000, 15000, 7500, 3750})
    {
        if (!nand && khz > 30000) continue;   // SPI NOR uses plain READ (03h)
        if (!open(khz)) { *detail = tr("the programmer is not connected"); ProgDeviceClose(prog); return 0; }
        ui->statusMessage->setText(tr("Auto-tune: testing %1...").arg(formatClock(khz)));
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        qint64 bytes = stress(700);
        if (bytes >= 0)
        {
            const qint64 more = stress(2300);   // no margin is kept, so test the winner longer
            bytes = more >= 0 ? bytes + more : -1;
        }
        if (bytes >= 0) { chosen = khz; testedBytes = bytes; break; }
        failed << formatClock(khz);
    }
    ProgDeviceClose(prog);
    QString text = tr("%1 MB re-read without errors").arg(testedBytes / 1048576.0, 0, 'f', 1);
    if (!failed.isEmpty()) text += tr("; failed at %1").arg(failed.join(", "));
    *detail = text;
    return chosen;
}
