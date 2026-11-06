/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DRIVERINSTALLER_H
#define DRIVERINSTALLER_H
#include <QString>

class QWidget;

// WCH CH341/CH347 parallel/SPI driver (ch341wdm.inf, service CH341_A64).
// The signed driver package is shipped in the "driver" folder beside the
// executable and installed with the Windows pnputil tool.
namespace WchDriver {

struct Status {
    bool installed = false;   // package present in the Windows driver store
    QString version;          // installed DriverVer version, e.g. 2.6.2025.4
};

bool supported();             // false on non-Windows builds
QString bundledInfPath();     // empty when the package is not bundled
QString bundledVersion();     // DriverVer of the bundled package
Status installedStatus();

enum Result { Installed, InstalledRebootRequired, Cancelled, Failed };
// Runs "pnputil /add-driver <inf> /install" elevated (UAC prompt) and waits.
Result install(QWidget *parent, QString *detail);

} // namespace WchDriver

#endif
