/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "driverinstaller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVersionNumber>
#include <QWidget>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

const char *const InfName = "ch341wdm.inf";

// Reads DriverVer ("DriverVer = 04/14/2025, 2.6.2025.04") from an INF that
// may be UTF-16LE (with BOM) or 8-bit.
QString infDriverVersion(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    const QByteArray raw = file.read(256 * 1024);
    QString text;
    if (raw.startsWith("\xff\xfe"))
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2), (raw.size() - 2) / 2);
    else
        text = QString::fromLocal8Bit(raw);
    const QStringList lines = text.split('\n');
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith("DriverVer", Qt::CaseInsensitive)) continue;
        const int comma = trimmed.indexOf(',');
        if (comma < 0) continue;
        // Normalise "2.6.2025.04" to "2.6.2025.4".
        return QVersionNumber::fromString(trimmed.mid(comma + 1).trimmed()).toString();
    }
    return QString();
}

#ifdef _WIN32
QString systemDirectory()
{
    wchar_t buffer[MAX_PATH];
    const UINT n = GetSystemDirectoryW(buffer, MAX_PATH);
    if (!n || n >= MAX_PATH) return QStringLiteral("C:/Windows/System32");
    return QDir::fromNativeSeparators(QString::fromWCharArray(buffer, static_cast<int>(n)));
}
#endif

} // namespace

namespace WchDriver {

bool supported()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

QString bundledInfPath()
{
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("driver/") + InfName);
    return QFileInfo::exists(path) ? path : QString();
}

QString bundledVersion()
{
    const QString inf = bundledInfPath();
    return inf.isEmpty() ? QString() : infDriverVersion(inf);
}

Status installedStatus()
{
    Status status;
#ifdef _WIN32
    // Driver store folders are readable without administrator rights.
    QDir store(systemDirectory() + QStringLiteral("/DriverStore/FileRepository"));
    const QStringList packages = store.entryList({QString(InfName) + QStringLiteral("_*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    QVersionNumber best;
    for (const QString &package : packages)
    {
        const QString version = infDriverVersion(store.filePath(package + "/" + InfName));
        const QVersionNumber number = QVersionNumber::fromString(version);
        status.installed = true;
        if (!number.isNull() && number > best) best = number;
    }
    if (!best.isNull()) status.version = best.toString();
#endif
    return status;
}

Result install(QWidget *parent, QString *detail)
{
#ifdef _WIN32
    const QString inf = bundledInfPath();
    if (inf.isEmpty())
    {
        if (detail) *detail = QCoreApplication::translate("WchDriver", "The driver folder is missing beside IMSProg.exe. Extract the complete package.");
        return Failed;
    }
    const std::wstring program = QDir::toNativeSeparators(systemDirectory() + QStringLiteral("/pnputil.exe")).toStdWString();
    const std::wstring arguments = (QStringLiteral("/add-driver \"") + QDir::toNativeSeparators(inf) +
                                    QStringLiteral("\" /install")).toStdWString();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.hwnd = parent ? reinterpret_cast<HWND>(parent->window()->winId()) : nullptr;
    info.lpVerb = L"runas";
    info.lpFile = program.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) return Cancelled;
        if (detail) *detail = QCoreApplication::translate("WchDriver", "Cannot start pnputil (Windows error %1).").arg(error);
        return Failed;
    }
    // Keep the window responsive while pnputil runs.
    while (WaitForSingleObject(info.hProcess, 50) == WAIT_TIMEOUT)
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    switch (exitCode)
    {
    case 0:     // installed and present devices updated
    case 259:   // ERROR_NO_MORE_ITEMS: package added, no device needed an update
        return Installed;
    case 3010:  // ERROR_SUCCESS_REBOOT_REQUIRED
        return InstalledRebootRequired;
    default:
        if (detail) *detail = QCoreApplication::translate("WchDriver", "pnputil exit code %1.").arg(exitCode);
        return Failed;
    }
#else
    Q_UNUSED(parent);
    if (detail) *detail = QStringLiteral("Not supported on this platform.");
    return Failed;
#endif
}

} // namespace WchDriver
