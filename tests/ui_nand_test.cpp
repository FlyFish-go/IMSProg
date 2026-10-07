// Hardware-free integration tests for the real Qt MainWindow and production slots.
// The executable must be linked with the --wrap flags in ui_run_tests.sh.
#include <QtWidgets>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <vector>
#define private public
#include "mainwindow.h"
#undef private

namespace {
struct Transfer { unsigned int pageSize, block, pages; unsigned char first; };
struct Fake {
    int initResult = -1, idResult = 0, configResult = 0, writeResult = 0;
    u16 lastSpeed = 0;
    int expectedProgrammer = -1;
    int readFailBlock = -1, readError = -1, badCheckResult = 0, eraseFailBlock = -1;
    unsigned char readbackExtra = 0;
    int opens = 0, closes = 0, configureCalls = 0, ecc = -1, bufferMode = -1;
    int eccMode = -1;
    unsigned int eccStatus = 0;
    unsigned char id[3] = {0xc8, 0xd1, 0x00};
    bool erasedReads = false;
    std::set<unsigned int> badBlocks;
    std::vector<Transfer> reads, writes, norReads;
    std::vector<unsigned int> erases;
    std::function<void()> afterRead, afterErase;
} fake;
int assertions = 0;
void check(bool success, const char *description) {
    ++assertions;
    if (!success) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
void checkProgrammer(u8 programmer) {
    if (fake.expectedProgrammer >= 0)
        check(programmer == fake.expectedProgrammer, "all device/NAND calls use the selected programmer family/index");
}
QComboBox *combo(MainWindow &w, const char *name) {
    auto *c = w.findChild<QComboBox *>(name);
    check(c != nullptr, name);
    return c;
}
void checkNandControls(MainWindow &w, bool shown, bool enabled) {
    const char *controls[] = {"label_11", "comboBox_ECC", "label_12", "comboBox_raw",
                              "label_onDieEcc", "comboBox_onDieEcc", "label_nandInfo"};
    for (const char *name : controls) {
        auto *control = w.findChild<QWidget *>(name);
        check(control != nullptr, "NAND-only control exists");
        check(control->isHidden() == !shown, "NAND-only control visibility follows selected protocol data");
        check(control->isEnabled() == enabled, "NAND-only control enabled state follows protocol and operation lock");
    }
}
QCheckBox *box(MainWindow &w, const char *name) {
    auto *c = w.findChild<QCheckBox *>(name);
    check(c != nullptr, name);
    return c;
}
void choose(MainWindow &w, const char *name, unsigned int value) {
    auto *c = combo(w, name);
    int index = c->findData(value);
    if (index < 0) { c->addItem(QString("Test %1").arg(value), value); index = c->count() - 1; }
    c->setCurrentIndex(index);
}
void reset(MainWindow &w, bool raw = false, bool ecc = false, int blocks = 2) {
    w.timer->stop();
    w.isHalted = false;
    w.nandReadIncomplete = false;
    w.filled = true;
    w.clearCheckboxes();
    choose(w, "comboBox_type", 6);
    combo(w, "comboBox_name")->setCurrentIndex(0);
    choose(w, "comboBox_page", 2048);
    choose(w, "comboBox_block", 131072);
    choose(w, "comboBox_ECC", 1);
    choose(w, "comboBox_size", static_cast<unsigned int>(131072 * blocks));
    w.filled = true;
    choose(w, "comboBox_raw", raw ? 1 : 0);
    choose(w, "comboBox_onDieEcc", ecc ? 1 : 0);
    w.nandSettings = 0;
    w.chipData = QByteArray(static_cast<int>(w.nandImageSize()), '\xff');
    w.hexEdit->setData(w.chipData);
    w.filled = true;
    fake = Fake();
    fake.initResult = 0;
    w.timer->stop();
}
void setImage(MainWindow &w) {
    int step = static_cast<int>(w.nandImageSize() / (w.currentChipSize / w.currentBlockSize));
    w.chipData.resize(static_cast<int>(w.nandImageSize()));
    for (int offset = 0; offset < w.chipData.size(); offset += step)
        std::memset(w.chipData.data() + offset, 0x11 * (offset / step + 1), step);
    w.hexEdit->setData(w.chipData);
}
}

extern "C" {
int __wrap_ProgDeviceInit(u8 programmer, u8 chip, u16 speed) {
    checkProgrammer(programmer);
    ++fake.opens; fake.lastSpeed = speed;
    if ((programmer == 2 || programmer == 3) && chip != 1)
        check(speed >= 468 && speed <= 60000, "CH347 SPI init/probe uses a representable clock, not I2C speed/index");
    return fake.initResult;
}
int __wrap_ProgDeviceClose(u8 programmer) { checkProgrammer(programmer); ++fake.closes; return 0; }
int __wrap_getDeviceDescriptor(u8 *, u8) { return -1; }
int __wrap_snor_read_param(unsigned char *out, unsigned long from, unsigned long length,
                          unsigned int sectorSize, unsigned int, u8 programmer) {
    checkProgrammer(programmer);
    fake.norReads.push_back({sectorSize, static_cast<unsigned int>(from / sectorSize), 1, 0});
    std::memset(out, 0x4c + from / sectorSize, length);
    if (fake.afterRead) { auto callback = fake.afterRead; fake.afterRead = {}; callback(); }
    return static_cast<int>(length);
}
int __wrap_snor_read_devid(u8 *out, int count, u8 programmer) {
    checkProgrammer(programmer);
    // NOR-style probe of a NAND chip returns a leading dummy byte; the real
    // Detect slot switches protocol, then performs its NAND JEDEC retry.
    for (int i = 0; i < count; ++i) out[i] = i > 0 && i <= 3 ? fake.id[i - 1] : 0xff;
    return fake.idResult;
}
int __wrap_nand_read_devid(u8 *out, int count, u8 programmer) {
    checkProgrammer(programmer);
    for (int i = 0; i < count; ++i) out[i] = i < 3 ? fake.id[i] : 0xff;
    return fake.idResult;
}
int __wrap_nand_configure_ecc(u8 programmer, int enabled, int buffered, u8 *readback) {
    checkProgrammer(programmer);
    ++fake.configureCalls; fake.ecc = enabled; fake.bufferMode = buffered;
    if (readback) *readback = static_cast<u8>(0x80 | fake.readbackExtra | (enabled ? 0x10 : 0) | (buffered ? 0x08 : 0));
    return fake.configResult;
}
void __wrap_nand_set_ecc_status_mode(int mode) { fake.eccMode = mode; }
void __wrap_nand_reset_ecc_read_status() { fake.eccStatus = 0; }
unsigned int __wrap_nand_get_ecc_read_status() { return fake.eccStatus; }
int __wrap_nand_unprotect(u8 programmer) { checkProgrammer(programmer); return 0; }
int __wrap_nand_checkBadBlock(u32 block, u32, u32, u8 programmer) {
    checkProgrammer(programmer);
    if (fake.badCheckResult < 0) return fake.badCheckResult;
    return fake.badBlocks.count(block) ? 1 : 0;
}
int __wrap_nand_block_read(unsigned char *out, unsigned int pageSize, u32 block, unsigned int pages, u8 programmer) {
    checkProgrammer(programmer);
    fake.reads.push_back({pageSize, block, pages, 0});
    if (static_cast<int>(block) == fake.readFailBlock) return fake.readError;
    std::memset(out, fake.erasedReads ? 0xff : 0x11 * (block + 1), pageSize * pages);
    if (fake.afterRead) { auto callback = fake.afterRead; fake.afterRead = {}; callback(); }
    return 0;
}
int __wrap_nand_page_read(unsigned char *out, unsigned int pageSize, u32 page) {
    fake.reads.push_back({pageSize, page, 1, 0});
    std::memset(out, fake.erasedReads ? 0xff : 0x11, pageSize);
    return 0;
}
int __wrap_nand_block_write(unsigned char *data, unsigned int pageSize, u32 block, unsigned int pages, u8 programmer) {
    checkProgrammer(programmer);
    fake.writes.push_back({pageSize, block, pages, data[0]});
    return fake.writeResult;
}
int __wrap_nand_block_erase(unsigned int block, unsigned int, u8 programmer) {
    checkProgrammer(programmer);
    fake.erases.push_back(block);
    if (fake.afterErase) { auto callback = fake.afterErase; fake.afterErase = {}; callback(); }
    return static_cast<int>(block) == fake.eraseFailBlock ? -1 : 0;
}
// Any reintroduced implicit ECC toggle is a test failure.
void __wrap_nand_ECCEnable(u8) { check(false, "unexpected implicit ECC enable"); }
void __wrap_nand_ECCDisable(u8) { check(false, "unexpected implicit ECC disable"); }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("IMSProgUiTests");
    QCoreApplication::setApplicationName("IMSProgUiTests");
    QStandardPaths::setTestModeEnabled(true);
    check(argc >= 2, "database argument supplied");
    app.setProperty("app/userChipDatabaseFile", QString::fromLocal8Bit(argv[1]));
    app.setProperty("app/systemChipDatabaseFile", QString::fromLocal8Bit(argv[1]));
    // Dismiss real production modal dialogs deterministically, including failure paths.
    // No platform file, security or OS dialog is used by these test cases.
    int answer = QMessageBox::Yes;
    QStringList dialogs;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *msg = qobject_cast<QMessageBox *>(widget)) {
                if (!msg->isVisible()) continue;
                dialogs << msg->text();
                if (msg->standardButtons() & QMessageBox::Yes)
                    msg->button(static_cast<QMessageBox::StandardButton>(answer))->click();
                else if (msg->button(QMessageBox::Ok)) msg->button(QMessageBox::Ok)->click();
                else msg->accept();
            }
    });
    dismiss.start(1);
    // Persisted CH347 selection exercises the startup probe, not just later actions.
    QDir settingsDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    check(settingsDir.mkpath("."), "test settings directory created");
    QSettings settings(settingsDir.filePath("config.ini"), QSettings::IniFormat);
    settings.setValue("Device/ProgrammerType", 2);
    settings.sync();
    MainWindow w;
    w.timer->stop();
    check(w.hexEdit != nullptr && w.uiReady, "constructor completes with initialized editor and selectors");
    check(combo(w, "comboBox_raw")->count() == 2, "exactly two image layouts");
    check(combo(w, "comboBox_onDieEcc")->count() == 2, "exactly two independent ECC choices");

    check(w.current_programmer == 2 && combo(w, "comboBox_type")->currentData().toInt() == 0,
          "restoring programmer does not select NAND protocol");
    checkNandControls(w, false, false);

    // Windows-only diagnostics remain hidden on native builds and cannot run
    // concurrently with a NAND operation through a menu action.
    auto *diagnostics = w.findChild<QAction *>("actionConnection_diagnostics");
    check(diagnostics != nullptr, "connection diagnostics action exists");
    check(!diagnostics->isVisible(), "Windows connection diagnostics hidden on native builds");
    int opensBeforeDiagnostics = fake.opens;
    w.on_actionConnection_diagnostics_triggered();
    check(fake.opens == opensBeforeDiagnostics && fake.reads.empty() && fake.writes.empty() && fake.erases.empty(),
          "native diagnostics slot does not initialize programmer or issue chip I/O");
    w.doNotDisturb();
    check(!diagnostics->isEnabled(), "connection diagnostics disabled during operations");
    w.doNotDisturbCancel();
    check(!diagnostics->isVisible(), "native diagnostics remains hidden after operation cleanup");
    w.timer->stop();

    // Real QAction selection preserves NAND controls and routes every production
    // slot through the requested programmer, including after operation cleanup.
    const char *programmerActions[] = {"actionCH341A_B_v1_2", "actionCH341A_v1_7",
                                     "actionCH347T", "actionCH347T_v1_1"};
    for (int programmer = 0; programmer < 4; ++programmer) {
        reset(w, true, false);
        setImage(w);
        const QByteArray beforeSwitch = w.hexEdit->data();
        auto *action = w.findChild<QAction *>(programmerActions[programmer]);
        check(action && action->isEnabled(), "requested CH341/CH347 action is selectable");
        action->trigger();
        check(w.current_programmer == programmer && action->isChecked(), "QAction selects the exact requested programmer index");
        check(w.nandRaw && !combo(w, "comboBox_onDieEcc")->currentData().toBool() && w.hexEdit->data() == beforeSwitch,
              "programmer switch preserves independent layout/ECC and image bytes");
        w.doNotDisturb();
        for (const char *name : programmerActions)
            check(!w.findChild<QAction *>(name)->isEnabled(), "programmer selection locked while an operation is active");
        w.doNotDisturbCancel();
        check(w.current_programmer == programmer && action->isChecked(), "cleanup preserves CH341/CH347 programmer selection");
        fake.expectedProgrammer = programmer;
        w.on_pushButton_clicked();
        check(fake.reads.size() == 2 && fake.ecc == 0 && fake.reads[0].pageSize == 2112,
              "selected programmer reads raw Main+OOB with ECC Off");
        w.on_actionVerify_triggered();
        check(w.lastOperationSucceeded, "selected programmer verifies without switching family");
        w.on_actionWrite_triggered();
        check(w.lastOperationSucceeded && fake.writes.size() == 2, "selected programmer writes without switching family");
        check(w.current_programmer == programmer && action->isChecked(), "completed read/verify/write retains programmer selection");
        w.timer->stop();
    }
    fake.expectedProgrammer = -1;
    w.findChild<QAction *>("actionCH347T")->trigger();

    // NAND controls are protocol-dependent, not filename/programmer-dependent.
    for (unsigned int protocol : {0u, 1u, 2u, 3u, 4u, 5u, 77u}) {
        reset(w, true, false);
        checkNandControls(w, true, true);
        w.hexEdit->addUserArea(1, 2, Qt::red, QBrush(Qt::blue));
        choose(w, "comboBox_type", protocol);
        checkNandControls(w, false, false);
        check(w.hexEdit->_colorManager->_userAreas.isEmpty(), "leaving NAND removes stale OOB highlights");
        check(w.nandRaw && combo(w, "comboBox_raw")->currentData().toInt() == 1 &&
              !combo(w, "comboBox_onDieEcc")->currentData().toBool(),
              "leaving NAND preserves dormant raw-layout and ECC-Off choices");
        const QByteArray nonNandBuffer(71, char(0x6d));
        w.chipData = nonNandBuffer; w.hexEdit->setData(nonNandBuffer);
        w.hexEdit->addUserArea(20, 30, Qt::green, QBrush(Qt::blue));
        w.fileName = "W25N01GW_NAND_Main_OOB.bin";
        choose(w, "comboBox_raw", 0); choose(w, "comboBox_raw", 1);
        choose(w, "comboBox_ECC", 2);
        choose(w, "comboBox_onDieEcc", 1); choose(w, "comboBox_onDieEcc", 0);
        check(w.hexEdit->data() == nonNandBuffer && w.chipData == nonNandBuffer,
              "hidden NAND callbacks cannot resize or modify non-NAND buffers");
        w.currentChipSize = 134217728; w.currentPageSize = 2048; w.currentECCsize = 64;
        w.hexEdit->_bPosFirst = 2048; w.hexEdit->_bPosLast = 4096;
        w.handleScroll();
        check(w.hexEdit->_colorManager->_userAreas.size() == 1 &&
              w.hexEdit->_colorManager->_userAreas[0].posStart() == 20 &&
              w.hexEdit->_colorManager->_userAreas[0].posEnd() == 30,
              "non-NAND callbacks preserve unrelated highlights without adding OOB colors");
        for (const char *name : programmerActions) {
            w.findChild<QAction *>(name)->trigger();
            checkNandControls(w, false, false);
        }
        w.doNotDisturb(); checkNandControls(w, false, false);
        w.doNotDisturbCancel(); checkNandControls(w, false, false);
        check(w.hexEdit->_colorManager->_userAreas.size() == 1,
              "non-NAND operation cleanup preserves unrelated editor highlights");
        choose(w, "comboBox_type", 6);
        checkNandControls(w, true, true);
        check(w.nandRaw && combo(w, "comboBox_raw")->currentData().toInt() == 1 &&
              !combo(w, "comboBox_onDieEcc")->currentData().toBool(),
              "returning to NAND restores dormant layout and ECC choices");
        w.doNotDisturb(); checkNandControls(w, true, false);
        w.doNotDisturbCancel(); checkNandControls(w, true, true);
        w.timer->stop();
    }
    reset(w, true, true);
    combo(w, "comboBox_type")->setCurrentIndex(-1);
    checkNandControls(w, false, false);
    w.doNotDisturbCancel(); checkNandControls(w, false, false);
    choose(w, "comboBox_type", 6);
    checkNandControls(w, true, true);
    check(combo(w, "comboBox_onDieEcc")->currentData().toBool(), "ECC-On dormant choice also survives unknown protocol");
    // A NAND protocol stored at another row must still activate these controls.
    auto *protocols = combo(w, "comboBox_type");
    protocols->addItem("NAND protocol data at another index", 6);
    protocols->setCurrentIndex(protocols->count() - 1);
    checkNandControls(w, true, true);
    protocols->removeItem(protocols->count() - 1);
    w.findChild<QAction *>("actionCH347T")->trigger();
    w.timer->stop();

    // A real production NOR read ignores dormant NAND layout/ECC choices.
    reset(w, true, true);
    choose(w, "comboBox_type", 0);
    choose(w, "comboBox_page", 256);
    choose(w, "comboBox_block", 65536);
    choose(w, "comboBox_size", 131072);
    choose(w, "comboBox_ECC", 2);
    checkNandControls(w, false, false);
    fake.afterRead = [&] {
        checkNandControls(w, false, false);
        check(w.findChild<QAction *>("actionStop")->isEnabled(), "NOR read exercises the actual operation lock");
    };
    w.on_pushButton_clicked();
    check(fake.norReads.size() == 2 && fake.reads.empty() && fake.configureCalls == 0,
          "NOR read performs only NOR transfers without NAND ECC setup");
    check(w.hexEdit->data().size() == 131072 && w.hexEdit->data()[0] == char(0x4c) &&
          w.hexEdit->data().back() == char(0x4d), "NOR buffer uses exact main-only size despite dormant Main+OOB");
    check(w.nandRaw && combo(w, "comboBox_onDieEcc")->currentData().toBool(),
          "NOR read preserves dormant raw-layout and ECC-On choices");
    checkNandControls(w, false, false);
    choose(w, "comboBox_type", 6);
    checkNandControls(w, true, true);
    fake.opens = 0; fake.reads.clear();
    w.on_pushButton_clicked();
    check(fake.opens == 0 && fake.reads.empty(), "NAND controls may show without a chip, but invalid geometry cannot operate");
    w.timer->stop();

    // Rejecting a layout change must preserve layout, ECC and buffer exactly.
    reset(w, false, true);
    setImage(w);
    w.filled = false;
    const QByteArray unchanged = w.hexEdit->data();
    answer = QMessageBox::No;
    choose(w, "comboBox_raw", 1);
    answer = QMessageBox::Yes;
    check(!w.nandRaw && combo(w, "comboBox_raw")->currentData().toInt() == 0,
          "cancelled layout change restores selector and layout state");
    check(w.hexEdit->data() == unchanged && combo(w, "comboBox_onDieEcc")->currentData().toBool(),
          "cancelled layout change preserves image and independent ECC state");
    check(fake.opens == 0, "layout selection never opens programmer");

    reset(w);
    w.slotTimerAlarm();
    check(w.statusCH341 == 0 && fake.lastSpeed >= 468, "periodic CH347 probe uses valid SPI clock");
    choose(w, "comboBox_type", 0);
    checkNandControls(w, false, false);
    fake.id[0] = 0xef; fake.id[1] = 0xba; fake.id[2] = 0x21;
    w.on_pushButton_2_clicked();
    check(w.statusCH341 == 0 && combo(w, "comboBox_name")->currentText().startsWith("W25N01GW"),
          "Detect reaches JEDEC and selects W25N01GW with checked SPI initialization");
    check(w.currentPageSize == 2048 && w.currentECCsize == 64, "Detect selects W25N01GW geometry");
    checkNandControls(w, true, true);
    w.timer->stop();
    reset(w);
    fake.idResult = -7;
    w.on_pushButton_2_clicked();
    check(fake.closes == 1 && fake.reads.empty(), "Detect transport error closes programmer and stops");
    w.timer->stop();
    reset(w, true, false);
    const QByteArray disconnectedBuffer = w.hexEdit->data();
    fake.initResult = -1;
    w.on_pushButton_clicked();
    check(fake.reads.empty() && fake.configureCalls == 0 && w.hexEdit->data() == disconnectedBuffer,
          "connection failure never reads/configures or replaces existing buffer");

    // All four Read/Verify combinations use the selected stride and ECC setting.
    for (bool raw : {false, true}) for (bool ecc : {false, true}) {
        reset(w, raw, ecc);
        const auto expectedSize = quint64(262144 + (raw ? 8192 : 0));
        check(w.nandImageSize() == expectedSize, "NAND size counts OOB independently of ECC");
        QByteArray before = w.hexEdit->data();
        const int opens = fake.opens;
        choose(w, "comboBox_onDieEcc", ecc ? 0 : 1);
        choose(w, "comboBox_onDieEcc", ecc ? 1 : 0);
        check(w.hexEdit->data() == before && fake.opens == opens && fake.configureCalls == 0,
              "ECC selector alone neither mutates buffer nor opens device");
        w.chipData = QByteArray(7, 'x'); // Regression: Read must resize a previously short file.
        w.hexEdit->setData(w.chipData);
        w.on_pushButton_clicked();
        check(fake.reads.size() == 2, "Read reads each selected physical block");
        check(fake.ecc == ecc, "Read uses requested ECC state");
        check(fake.reads[0].pageSize == (raw ? 2112u : 2048u), "Read stride follows layout only");
        check(quint64(w.hexEdit->data().size()) == expectedSize, "Read replaces short buffer with exact image size");
        check(static_cast<unsigned char>(w.hexEdit->data()[0]) == 0x11 &&
              static_cast<unsigned char>(w.hexEdit->data().back()) == 0x22, "Read stores both physical block payloads");
        fake.reads.clear();
        w.on_actionVerify_triggered();
        check(w.lastOperationSucceeded && fake.reads.size() == 2, "Verify succeeds in all four combinations");
        check(fake.ecc == ecc, "Verify uses requested ECC state");
        check(fake.reads[0].pageSize == (raw ? 2112u : 2048u), "Verify stride follows layout only");
    }

    // Blank-check uses selected OOB stride and ECC for all four combinations.
    for (bool raw : {false, true}) for (bool ecc : {false, true}) {
        reset(w, raw, ecc);
        fake.erasedReads = true;
        w.on_actionCheck_erase_triggered();
        check(w.lastOperationSucceeded && fake.reads.size() == 128, "blank check visits each page");
        check(fake.ecc == ecc && fake.reads[0].pageSize == (raw ? 2112u : 2048u),
              "blank check follows both independent choices");
    }

    reset(w, true, true);
    w.on_actionWrite_triggered();
    check(fake.opens == 0 && fake.writes.empty(), "Main+OOB ECC-on write blocked before opening hardware");
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    w.on_pushButton_3_clicked();
    check(fake.opens == 0 && fake.erases.empty(), "invalid batch write blocked before any erase");

    reset(w, false, false);
    w.hexEdit->setData(QByteArray(16, '\xff'));
    w.on_actionWrite_triggered();
    w.on_actionVerify_triggered();
    check(fake.opens == 0, "short write/verify buffer blocked before opening device");
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    w.on_pushButton_3_clicked();
    check(fake.erases.empty(), "short-buffer batch blocked before erase");

    reset(w);
    choose(w, "comboBox_block", 0);
    w.on_pushButton_clicked();
    w.on_actionErase_triggered();
    check(fake.opens == 0, "invalid block geometry blocked before device init");

    reset(w);
    choose(w, "comboBox_block", 65536); // 32 pages/block; production erase is 64-page only.
    w.on_actionErase_triggered();
    check(fake.opens == 0 && fake.erases.empty(), "unsupported pages-per-block geometry cannot erase wrong rows");

    // Setup readback failure cannot fall through to data I/O or destructive I/O.
    reset(w);
    setImage(w);
    const auto imageBeforeFailedSetup = w.hexEdit->data();
    fake.configResult = -1;
    w.on_pushButton_clicked();
    check(fake.reads.empty(), "ECC configuration failure stops Read");
    check(w.chipData == imageBeforeFailedSetup && w.hexEdit->data() == imageBeforeFailedSetup,
          "failed Read setup preserves the old buffer without clearing it");
    reset(w);
    fake.configResult = -1;
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    w.on_pushButton_3_clicked();
    check(fake.erases.empty() && fake.writes.empty(), "configuration failure stops whole destructive batch");

    reset(w);
    std::memcpy(fake.id, "\xef\xba\x21", 3);
    w.on_pushButton_clicked();
    check(fake.configureCalls == 0 && fake.reads.empty(), "W25N01GW wrong geometry fails before configuration");
    reset(w);
    std::memcpy(fake.id, "\xef\xba\x21", 3);
    w.currentChipSize = 134217728;
    check(w.prepareNandOperation("Test"), "W25N01GW exact geometry accepted");
    check(fake.bufferMode == 1 && fake.ecc == 0, "W25N01GW raw main-only explicitly requires BUF=1");

    reset(w);
    choose(w, "comboBox_size", 134217728);
    std::memcpy(fake.id, "\xef\xba\x21", 3);
    fake.readbackExtra = 0x40;
    w.on_pushButton_clicked();
    check(fake.configureCalls == 1 && fake.reads.empty(), "W25N01GW OTP mode rejects main-array read");
    w.nandReadIncomplete = false; // Isolate the independent OTP guard for Write.
    w.on_actionWrite_triggered();
    w.on_actionErase_triggered();
    check(fake.writes.empty() && fake.erases.empty(), "W25N01GW OTP mode blocks both write and erase");

    reset(w, true, true);
    fake.readFailBlock = 0;
    fake.readError = -1002;
    dialogs.clear();
    w.on_pushButton_clicked();
    check(w.nandReadIncomplete && fake.reads.size() == 1,
          "uncorrectable ECC read stops and marks image incomplete");
    check(!dialogs.join(" ").contains("The operation was successful"), "failed read never reports success");
    reset(w, true, true);
    fake.readFailBlock = 0;
    fake.readError = -1002;
    dialogs.clear();
    w.on_actionVerify_triggered();
    check(!w.lastOperationSucceeded && fake.reads.size() == 1, "uncorrectable ECC Verify stops at first page block");
    check(!dialogs.join(" ").contains("The operation was successful"), "failed Verify never reports success");

    for (bool ecc : {false, true}) {
        reset(w, false, ecc);
        setImage(w);
        w.on_actionWrite_triggered();
        check(w.lastOperationSucceeded && fake.writes.size() == 2 && fake.ecc == ecc,
              "main-only writing independently accepts ECC Off and On");
    }

    // Raw bad-block skipping retains physical image positions and propagates errors.
    reset(w, true, false, 3);
    setImage(w);
    w.nandSettings = 1;
    fake.badBlocks.insert(1);
    w.on_actionWrite_triggered();
    check(w.lastOperationSucceeded && fake.writes.size() == 2, "raw write skips bad physical block");
    check(fake.writes[0].block == 0 && fake.writes[0].first == 0x11 &&
          fake.writes[1].block == 2 && fake.writes[1].first == 0x33, "raw image offsets do not compact after bad block");
    reset(w, false, false, 3);
    setImage(w);
    w.nandSettings = 1;
    fake.badBlocks.insert(1);
    w.on_actionWrite_triggered();
    check(fake.writes.size() == 2 && fake.writes[1].block == 2 && fake.writes[1].first == 0x22,
          "main-only skip retains the documented logical compaction policy");
    check(!w.lastOperationSucceeded, "main-only image bytes left unwritten cannot report successful completion");
    reset(w, true, false);
    fake.writeResult = -1;
    w.on_actionWrite_triggered();
    check(!w.lastOperationSucceeded && fake.writes.size() == 1, "raw programming error stops at failed block");
    reset(w, true, false);
    w.nandSettings = 1;
    fake.badCheckResult = -1;
    w.on_actionWrite_triggered();
    check(!w.lastOperationSucceeded && fake.writes.empty(), "failed bad-block check never programs");

    // User cancellation or an erase error must never proceed to Write.
    reset(w);
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    answer = QMessageBox::No;
    w.on_pushButton_3_clicked();
    answer = QMessageBox::Yes;
    check(fake.erases.empty() && fake.writes.empty(), "declined erase confirmation stops batch");
    reset(w);
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    fake.eraseFailBlock = 0;
    w.on_pushButton_3_clicked();
    check(fake.erases.size() == 1 && fake.writes.empty(), "erase transport error stops batch");
    reset(w);
    box(w, "checkBox")->setChecked(true);
    box(w, "checkBox_2")->setChecked(true);
    fake.afterErase = [&] { QTimer::singleShot(0, [&] { w.on_actionStop_triggered(); }); };
    w.on_pushButton_3_clicked();
    check(fake.erases.size() == 1 && fake.writes.empty(), "Stop during erase prevents batch write");
    check(combo(w, "comboBox_raw")->isEnabled() && combo(w, "comboBox_onDieEcc")->isEnabled(),
          "both selectors restored after cancellation");

    // A new unselected destructive stage cannot be injected during processEvents.
    reset(w);
    box(w, "checkBox")->setChecked(true);
    fake.afterErase = [&] { box(w, "checkBox_2")->setChecked(true); };
    w.on_pushButton_3_clicked();
    check(fake.writes.empty(), "batch snapshots selected stages before erase");

    reset(w, true, false);
    fake.readFailBlock = 1;
    w.on_pushButton_clicked();
    check(fake.reads.size() == 2, "read error stops without overrunning selected blocks");
    check(combo(w, "comboBox_raw")->isEnabled() && combo(w, "comboBox_onDieEcc")->isEnabled(),
          "both selectors restored after read error");

    check(w.nandReadIncomplete, "failed read marks buffer incomplete");
    check(w.hexEdit->data().back() == char(0xff), "unread tail is FF rather than stale bytes");
    fake.writes.clear();
    fake.opens = 0;
    w.on_actionWrite_triggered();
    check(fake.opens == 0 && fake.writes.empty(), "incomplete read buffer cannot be programmed");

    // Closing during an operation requests Stop instead of hiding an active loop.
    reset(w, true, false);
    fake.afterRead = [&] { QTimer::singleShot(0, [&] {
        QCloseEvent event;
        w.closeEvent(&event);
        check(!event.isAccepted(), "Close during I/O keeps window open while requesting Stop");
    }); };
    w.on_pushButton_clicked();
    check(fake.reads.size() == 1 && w.nandReadIncomplete,
          "Close stops after current block and marks the read incomplete");

    // Opening an undersized NAND image must not retain bytes from an older dump.
    reset(w, true, false);
    setImage(w);
    QTemporaryFile shortImage;
    check(shortImage.open(), "temporary image opens");
    check(shortImage.write(QByteArray(16, char(0x5a))) == 16, "temporary image written");
    shortImage.flush();
    w.cmdStarted = true;
    w.lastDirectory = shortImage.fileName();
    w.on_actionOpen_triggered();
    check(w.hexEdit->data() == QByteArray(16, char(0x5a)), "NAND Open preserves actual file size without stale tail");
    w.on_actionWrite_triggered();
    check(fake.opens == 0 && fake.writes.empty(), "short NAND file cannot pass exact-size write guard");
    reset(w, true, false);
    choose(w, "comboBox_page", 0);
    w.cmdStarted = true;
    w.lastDirectory = shortImage.fileName();
    w.on_actionOpen_triggered();
    check(fake.opens == 0, "NAND Open with unselected page size neither divides by zero nor opens device");

    reset(w);
    choose(w, "comboBox_type", 0);
    check(combo(w, "comboBox_raw")->isHidden() && combo(w, "comboBox_onDieEcc")->isHidden(),
          "both NAND selectors hide for non-NAND chips");
    choose(w, "comboBox_type", 6);
    check(!combo(w, "comboBox_raw")->isHidden() && !combo(w, "comboBox_onDieEcc")->isHidden(),
          "both NAND selectors return when NAND is selected");

    if (argc > 2) {
        reset(w, true, false);
        auto *manufacturers = combo(w, "comboBox_man");
        int manufacturer = manufacturers->findText("WINBOND", Qt::MatchFixedString);
        if (manufacturer >= 0) manufacturers->setCurrentIndex(manufacturer);
        auto *names = combo(w, "comboBox_name");
        int chip = names->findText("W25N01GW", Qt::MatchStartsWith);
        if (chip >= 0) names->setCurrentIndex(chip);
        choose(w, "comboBox_raw", 1);
        choose(w, "comboBox_onDieEcc", 0);
        w.on_actionCH347T_triggered();
        fake.initResult = -1;
        w.statusCH341 = -1;
        w.ch341StatusFlashing();
        w.findChild<QProgressBar *>("progressBar")->setValue(0);
        w.findChild<QLineEdit *>("crcEdit")->clear();
        w.resize(1100, 760);
        w.show();
        app.processEvents();
        check(w.grab().save(QString::fromLocal8Bit(argv[2])), "actual Qt UI screenshot saved");
    }
    w.timer->stop();
    std::printf("PASS: %d Qt MainWindow integration assertions (fake NAND transport; no hardware I/O)\n", assertions);
    return 0;
}
