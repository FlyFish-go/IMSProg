/* SPDX-License-Identifier: BSD-1-Clause
 * CH347T/CH347F Windows transport: existing-driver libusb, then WCH vendor fallback.
 * ABI reference: WCHSoftGroup/ch347, OpenOCD_SourceCode_CH347/src/jtag/drivers/ch347.c.
 * No driver install, replacement, reset or kernel-driver detach is performed.
 */
#include "ch347_win32.h"
#include "ch347_usb_select.h"
#include "ch341_async.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stddef.h>
#include <stdarg.h>

/* WCH CH341PAR 2.6 / CH347DLL_EN.H v1.5 declares pack(1).
 * Keep this private so it cannot change the application's other structures. */
#pragma pack(push, 1)
typedef struct {
    UCHAR iIndex;
    UCHAR DevicePath[MAX_PATH];
    UCHAR UsbClass;
    UCHAR FuncType;
    CHAR DeviceID[64];
    UCHAR ChipMode;
    HANDLE DevHandle;
    USHORT BulkOutEndpMaxSize;
    USHORT BulkInEndpMaxSize;
    UCHAR UsbSpeedType;
    UCHAR CH347IfNum;
    UCHAR DataUpEndp;
    UCHAR DataDnEndp;
    CHAR ProductString[64];
    CHAR ManufacturerString[64];
    ULONG WriteTimeout;
    ULONG ReadTimeout;
    CHAR FuncDescStr[64];
    UCHAR FirewareVer;
} wch_device_info;
#pragma pack(pop)
_Static_assert(sizeof(ULONG) == 4, "WCH ULONG must be 32 bits");
_Static_assert(offsetof(wch_device_info, DevHandle) == 328, "WCH device-info handle offset mismatch");
_Static_assert(sizeof(wch_device_info) == (sizeof(HANDLE) == 8 ? 545 : 541),
               "WCH packed device-info size mismatch");

typedef HANDLE (WINAPI *wch_open_fn)(ULONG);
typedef BOOL (WINAPI *wch_close_fn)(ULONG);
typedef VOID (WINAPI *wch341_close_fn)(ULONG);
typedef BOOL (WINAPI *wch341_write_read_fn)(ULONG, ULONG, PVOID, ULONG, ULONG, PULONG, PVOID);
typedef BOOL (WINAPI *wch_io_fn)(ULONG, PVOID, PULONG);
typedef BOOL (WINAPI *wch_timeout_fn)(ULONG, ULONG, ULONG);
typedef BOOL (WINAPI *wch_info_fn)(ULONG, wch_device_info *);
typedef UCHAR (WINAPI *wch_chip_type_fn)(ULONG);

typedef int (LIBUSB_CALL *usb_init_fn)(libusb_context **);
typedef void (LIBUSB_CALL *usb_exit_fn)(libusb_context *);
typedef ssize_t (LIBUSB_CALL *usb_list_fn)(libusb_context *, libusb_device ***);
typedef void (LIBUSB_CALL *usb_free_list_fn)(libusb_device **, int);
typedef int (LIBUSB_CALL *usb_descriptor_fn)(libusb_device *, struct libusb_device_descriptor *);
typedef int (LIBUSB_CALL *usb_config_fn)(libusb_device *, struct libusb_config_descriptor **);
typedef void (LIBUSB_CALL *usb_free_config_fn)(struct libusb_config_descriptor *);
typedef int (LIBUSB_CALL *usb_open_fn)(libusb_device *, libusb_device_handle **);
typedef void (LIBUSB_CALL *usb_close_fn)(libusb_device_handle *);
typedef int (LIBUSB_CALL *usb_interface_fn)(libusb_device_handle *, int);
typedef int (LIBUSB_CALL *usb_alt_fn)(libusb_device_handle *, int, int);
typedef int (LIBUSB_CALL *usb_bulk_fn)(libusb_device_handle *, unsigned char, unsigned char *, int, int *, unsigned int);
typedef const char *(LIBUSB_CALL *usb_error_name_fn)(int);

struct windows_backend {
    int kind; /* 0: WCH vendor DLL; 1: libusb over the existing Windows driver. */
    int family; /* 0: CH347T/F (selected model); 1: CH341A/B parallel/SPI. Never changes after open. */
    unsigned char out_endpoint, in_endpoint;
    wch341_close_fn close341;
    wch_io_fn descriptor341;
    wch_io_fn config341;
    struct ch341_async_api async341;
    wch341_write_read_fn write_read341;
    HMODULE usbk_module;
    libusb_context *usb_context;
    libusb_device_handle *usb_handle;
    int usb_interface, usb_alternate, usb_claimed;
    usb_init_fn usb_init;
    usb_exit_fn usb_exit;
    usb_list_fn usb_list;
    usb_free_list_fn usb_free_list;
    usb_descriptor_fn usb_descriptor;
    usb_config_fn usb_config;
    usb_free_config_fn usb_free_config;
    usb_open_fn usb_open;
    usb_close_fn usb_close;
    usb_interface_fn usb_claim;
    usb_interface_fn usb_release;
    usb_alt_fn usb_set_alt;
    usb_bulk_fn usb_bulk;
    usb_error_name_fn usb_error_name;
    HMODULE module;
    ULONG index;
    int opened;
    wch_open_fn open;
    wch_close_fn close;
    wch_io_fn read;
    wch_io_fn write;
    wch_timeout_fn timeout;
    wch_info_fn info;
    wch_chip_type_fn chip_type;
};

enum { FAMILY_CH347 = 0, FAMILY_CH341 = 1 };
/* CH347 variant chosen by the user. CH347T and CH347F are never fallback targets for each other. */
static int selected_ch347_model = CH347_MODEL_T;
void ch347_windows_select_model(int model)
{
    selected_ch347_model = ch347_model_valid(model) ? model : CH347_MODEL_T;
}
static const char *family_name(int family)
{
    return family == FAMILY_CH341 ? "CH341A/B" : ch347_model_name(selected_ch347_model);
}
static int family_matches(int family, uint16_t vid, uint16_t pid)
{
    return family == FAMILY_CH341 ? (pid == 0x5512 && (vid == 0x1a86 || vid == 0x4348))
                                  : (vid == CH347_SPI_VID && pid == ch347_model_pid(selected_ch347_model));
}
static const char *ch347_required_ids(void)
{
    return selected_ch347_model == CH347_MODEL_F ? "VID_1A86 PID_55DE (CH347F)" : "VID_1A86 PID_55DB (mode 1)";
}

/* No serial number, full device path or unrelated USB devices are recorded. */
static char last_error[2048];
static char last_report[16384];
static int usb_preflight_complete;

static void report_append(const char *format, ...)
{
    size_t used = strlen(last_report);
    va_list args;
    if (used >= sizeof(last_report) - 1) return;
    va_start(args, format);
    vsnprintf(last_report + used, sizeof(last_report) - used, format, args);
    va_end(args);
}

static void report_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    report_append("ERROR: %s\n", last_error);
}

const char *ch347_windows_last_error(void)
{
    return last_error[0] ? last_error : "No backend error recorded.";
}

const char *ch347_windows_last_report(void)
{
    return last_report[0] ? last_report : "No connection attempt recorded.";
}

void ch347_windows_set_error_stage(const char *stage, int error_code)
{
    char detail[sizeof(last_error)];
    snprintf(detail, sizeof(detail), "%s", last_error);
    report_error("%s failed (code %d).%s%.1500s", stage, error_code,
                 detail[0] ? " Previous backend detail: " : "", detail);
}

static HMODULE load_app_dll(const WCHAR *name, const char *display_name)
{
    WCHAR path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    WCHAR *slash;
    if (!n || n >= sizeof(path) / sizeof(path[0])) {
        report_error("DLL path: GetModuleFileNameW failed or truncated (Windows error %lu).", GetLastError());
        return NULL;
    }
    slash = wcsrchr(path, L'\\');
    if (!slash) { report_error("DLL path: executable directory not found."); return NULL; }
    slash[1] = 0;
    report_append("Loading app-local DLL: %s.\n", display_name);
    if (wcslen(path) + wcslen(name) >= sizeof(path) / sizeof(path[0])) {
        report_error("DLL path: executable path is too long.");
        return NULL;
    }
    wcscat(path, name);
    SetLastError(0);
    HMODULE module = LoadLibraryExW(path, NULL,
                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) report_error("DLL load failed (Windows error %lu). Check DLL presence, architecture and dependent system libraries.", GetLastError());
    else report_append("DLL load: success.\n");
    return module;
}

/* WCH's signed driver package installs its matching vendor DLL in the Windows
 * system directory. Prefer an explicitly bundled copy so portable deployments
 * remain deterministic, then try that protected install location by absolute
 * path. Never use the current directory, PATH or the unrestricted DLL search. */
static HMODULE load_wch_dll(const WCHAR *name, const char *display_name)
{
    WCHAR path[32768];
    const size_t capacity = sizeof(path) / sizeof(path[0]);
    DWORD n, app_error, system_error;
    WCHAR *slash;
    HMODULE module;

    n = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (!n || n >= capacity) {
        report_error("WCH DLL path: GetModuleFileNameW failed or truncated (Windows error %lu).", GetLastError());
        return NULL;
    }
    slash = wcsrchr(path, L'\\');
    if (!slash) {
        report_error("WCH DLL path: executable directory not found.");
        return NULL;
    }
    slash[1] = 0;
    if (wcslen(path) + wcslen(name) >= capacity) {
        report_error("WCH DLL path: executable path is too long.");
        return NULL;
    }
    wcscat(path, name);
    report_append("Loading app-local WCH DLL: %s.\n", display_name);
    SetLastError(0);
    module = LoadLibraryExW(path, NULL,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module) {
        report_append("App-local WCH DLL load: success.\n");
        return module;
    }
    app_error = GetLastError();
    report_append("App-local WCH DLL load failed (Windows error %lu); trying the installed System32 copy.\n",
                  app_error);

    SetLastError(0);
    n = GetSystemDirectoryW(path, (UINT)capacity);
    if (!n || n >= capacity) {
        system_error = GetLastError();
        report_error("WCH DLL load failed: app-local Windows error %lu; GetSystemDirectoryW failed or truncated (Windows error %lu).",
                     app_error, system_error);
        return NULL;
    }
    if (path[n - 1] != L'\\') {
        if ((size_t)n + 1 >= capacity) {
            report_error("WCH DLL load failed: app-local Windows error %lu; System32 path is too long.", app_error);
            return NULL;
        }
        path[n++] = L'\\';
        path[n] = 0;
    }
    if ((size_t)n + wcslen(name) >= capacity) {
        report_error("WCH DLL load failed: app-local Windows error %lu; System32 DLL path is too long.", app_error);
        return NULL;
    }
    wcscat(path, name);
    report_append("Loading installed WCH DLL from the absolute System32 path: %s.\n", display_name);
    SetLastError(0);
    module = LoadLibraryExW(path, NULL,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        system_error = GetLastError();
        report_error("WCH DLL load failed from app-local (Windows error %lu) and System32 (Windows error %lu). Check the official WCH driver/DLL installation and architecture.",
                     app_error, system_error);
        return NULL;
    }
    report_append("System32 WCH DLL load: success.\n");
    return module;
}

static void begin_connection_report(int diagnostic_only, int family)
{
    char previous[sizeof(last_error)];
    snprintf(previous, sizeof(previous), "%s", last_error);
    last_error[0] = last_report[0] = '\0';
    usb_preflight_complete = 0;
    report_append("%s connection diagnostics v4\n", family_name(family));
    if (diagnostic_only) {
        report_append("Read-only enumeration: no SPI command, flash read/write/erase or driver change.\n");
        if (previous[0]) report_append("Previous failure: %s\n", previous);
    }
    report_append("Selected family: %s. Family-specific matching only; another family is never a fallback target.\n", family_name(family));
    report_append("Required IDs: %s. Existing-driver libusb first; WCH vendor fallback only before SPI I/O.\n",
                  family == FAMILY_CH341 ? "VID_1A86 or VID_4348, PID_5512 (parallel/SPI mode)" : ch347_required_ids());
}

static int windows_open_impl(struct ch347_priv *p, int diagnostic_only)
{
    struct windows_backend *b;
    int found = 0, opened_count = 0, rejected_count = 0, info_errors = 0;
    int failure = -ENODEV;
    const int model = selected_ch347_model;
    char pid_text[16];
    if (!p) { report_error("Open: invalid device context."); return -EINVAL; }
    snprintf(pid_text, sizeof(pid_text), "PID_%04X", ch347_model_pid(model));
    report_append("Trying official WCH vendor backend (class 0, %s chip type %d, function %d, ChipMode %d%s).\n",
                  ch347_model_name(model), ch347_model_wch_chip_type(model),
                  ch347_model_wch_func_type(model), ch347_model_wch_chip_mode(model),
                  model == CH347_MODEL_F ? ", interface 4" : "");
    b = (struct windows_backend *)calloc(1, sizeof(*b));
    if (!b) { report_error("Open: out of memory."); return -ENOMEM; }
    b->family = FAMILY_CH347;
    b->out_endpoint = 0x06; b->in_endpoint = 0x86;
    p->windows_backend = b;
#ifdef _WIN64
    b->module = load_wch_dll(L"CH347DLLA64.DLL", "CH347DLLA64.DLL");
#else
    b->module = load_wch_dll(L"CH347DLL.DLL", "CH347DLL.DLL");
#endif
    if (!b->module) goto fail;
#define LOAD_MEMBER(member, type, name) do { \
    FARPROC address = GetProcAddress(b->module, name); \
    _Static_assert(sizeof(type) == sizeof(address), "Windows function pointer ABI mismatch"); \
    memcpy(&b->member, &address, sizeof(address)); \
    if (!b->member) { report_error("DLL export missing: %s (Windows error %lu).", name, GetLastError()); goto fail; } \
} while (0)
    LOAD_MEMBER(open, wch_open_fn, "CH347OpenDevice");
    LOAD_MEMBER(close, wch_close_fn, "CH347CloseDevice");
    LOAD_MEMBER(read, wch_io_fn, "CH347ReadData");
    LOAD_MEMBER(write, wch_io_fn, "CH347WriteData");
    LOAD_MEMBER(timeout, wch_timeout_fn, "CH347SetTimeout");
    LOAD_MEMBER(info, wch_info_fn, "CH347GetDeviceInfor");
    LOAD_MEMBER(chip_type, wch_chip_type_fn, "CH347GetChipType");
#undef LOAD_MEMBER
    report_append("DLL required exports: success. Device-info packed size: %u.\n", (unsigned)sizeof(wch_device_info));
    for (ULONG i = 0; i < 32; ++i) {
        SetLastError(0);
        HANDLE h = b->open(i);
        DWORD open_error = GetLastError();
        wch_device_info info;
        UCHAR chip_type;
        /* WCH documents a device-index result: index 0 is valid. */
        if (h == INVALID_HANDLE_VALUE) {
            report_append("Index %lu: OpenDevice failed, Windows error %lu.\n", i, open_error);
            continue;
        }
        ++opened_count;
        memset(&info, 0, sizeof(info));
        SetLastError(0);
        if (!b->info(i, &info)) {
            ++info_errors;
            report_append("Index %lu: OpenDevice succeeded; GetDeviceInfor failed, Windows error %lu.\n", i, GetLastError());
            b->close(i);
            continue;
        }
        info.DeviceID[sizeof(info.DeviceID) - 1] = '\0';
        for (size_t j = 0; j < sizeof(info.DeviceID); ++j)
            if (info.DeviceID[j] >= 'a' && info.DeviceID[j] <= 'z') info.DeviceID[j] -= 'a' - 'A';
        chip_type = b->chip_type(i);
        report_append("Index %lu: ID=%s; UsbClass=%u ChipType=%u FuncType=%u ChipMode=%u Interface=%u; WCH endpoint numbers up=0x%02X down=0x%02X; firmware=0x%02X.\n",
                      i, info.DeviceID, info.UsbClass, chip_type, info.FuncType, info.ChipMode,
                      info.CH347IfNum, info.DataUpEndp, info.DataDnEndp, info.FirewareVer);
        if (info.UsbClass != 0 || chip_type != ch347_model_wch_chip_type(model) ||
            info.FuncType != ch347_model_wch_func_type(model) ||
            info.ChipMode != ch347_model_wch_chip_mode(model) ||
            (model == CH347_MODEL_F && info.CH347IfNum != CH347F_SPI_IF) ||
            !strstr(info.DeviceID, "VID_1A86") || !strstr(info.DeviceID, pid_text)) {
            ++rejected_count;
            report_append("Index %lu: excluded by this port's chip/interface/mode/driver/function/VID-PID filter.\n", i);
            b->close(i);
            continue;
        }
        if (found) {
            b->close(i);
            report_error("Enumeration: multiple matching %s interfaces; connect only the intended programmer.", ch347_model_name(model));
            failure = -EEXIST;
            goto fail;
        }
        b->index = i;
        b->opened = 1;
        found = 1;
        report_append("Index %lu: accepted by filter.\n", i);
    }
    if (!found) {
        report_error("Enumeration: no matching %s interface. Opened=%d, GetDeviceInfor failures=%d, excluded=%d. This does not by itself prove a wrong driver; inspect the report and Device Manager.",
                     model == CH347_MODEL_F ? "CH347F" : "CH347T mode 1", opened_count, info_errors, rejected_count);
        goto fail;
    }
    if (!diagnostic_only) {
        SetLastError(0);
        if (!b->timeout(b->index, 3000, 3000)) {
            report_error("SetTimeout at index %lu failed (Windows error %lu).", b->index, GetLastError());
            goto fail;
        }
        report_append("SetTimeout: success. SPI initialization is the next stage.\n");
    } else report_append("Read-only enumeration succeeded; SPI packet/configuration behavior was not tested.\n");
    report_append("Selected backend: WCH vendor DLL.\n");
    last_error[0] = '\0';
    return 0;
fail:
    ch347_windows_close(p);
    return failure;
}

static int validate_ch341_config(const uint8_t *data, ULONG size, int *iface_out)
{
    unsigned pos = 0, total, matches = 0;
    int iface = -1, alternate = -1, in = 0, out = 0;
    if (size < 9 || data[0] < 9 || data[1] != 2) return -EINVAL;
    total = data[2] | ((unsigned)data[3] << 8);
    if (total > size || total < 9) return -EINVAL;
    while (pos < total) {
        unsigned length = data[pos];
        if (length < 2 || length > total - pos) return -EINVAL;
        if (data[pos + 1] == 4) {
            if (in == 1 && out == 1) {
                if (alternate != 0) return -ENOTSUP;
                *iface_out = iface; ++matches;
            }
            if (length < 9) return -EINVAL;
            iface = data[pos + 2]; alternate = data[pos + 3]; in = out = 0;
        } else if (data[pos + 1] == 5) {
            if (length < 7 || iface < 0) return -EINVAL;
            if ((data[pos + 3] & 3) == 2) {
                if (data[pos + 2] == 0x82) ++in;
                if (data[pos + 2] == 0x02) ++out;
                if (in > 1 || out > 1) return -EEXIST;
            }
        }
        pos += length;
    }
    if (in == 1 && out == 1) {
        if (alternate != 0) return -ENOTSUP;
        *iface_out = iface; ++matches;
    }
    return matches == 1 ? 0 : (matches > 1 ? -EEXIST : -ENODEV);
}

static int windows341_open_impl(struct ch347_priv *p, int diagnostic_only)
{
    struct windows_backend *b = calloc(1, sizeof(*b));
    int found = 0, failure = -ENODEV;
    if (!b) { report_error("CH341 vendor open: out of memory."); return -ENOMEM; }
    b->family = FAMILY_CH341; b->out_endpoint = 0x02; b->in_endpoint = 0x82;
    p->windows_backend = b;
    report_append("Trying CH341 vendor DLL; VID/PID and bulk configuration are verified before any SPI command.\n");
#ifdef _WIN64
    b->module = load_wch_dll(L"CH341DLLA64.DLL", "CH341DLLA64.DLL");
#else
    b->module = load_wch_dll(L"CH341DLL.DLL", "CH341DLL.DLL");
#endif
    if (!b->module) goto fail;
#define LOAD_341(member, type, name) do { \
    FARPROC address = GetProcAddress(b->module, name); \
    _Static_assert(sizeof(type) == sizeof(address), "Windows function pointer ABI mismatch"); \
    memcpy(&b->member, &address, sizeof(address)); \
    if (!b->member) { report_error("CH341 DLL export missing: %s.", name); goto fail; } \
} while (0)
    LOAD_341(open, wch_open_fn, "CH341OpenDevice");
    LOAD_341(close341, wch341_close_fn, "CH341CloseDevice");
    LOAD_341(read, wch_io_fn, "CH341ReadData");
    LOAD_341(write, wch_io_fn, "CH341WriteData");
    LOAD_341(timeout, wch_timeout_fn, "CH341SetTimeout");
    LOAD_341(descriptor341, wch_io_fn, "CH341GetDeviceDescr");
    LOAD_341(config341, wch_io_fn, "CH341GetConfigDescr");
    LOAD_341(write_read341, wch341_write_read_fn, "CH341WriteRead");
#undef LOAD_341
    for (ULONG i = 0; i < 16; ++i) {
        uint8_t descriptor[18] = {0}, configuration[4096] = {0};
        ULONG length = sizeof(descriptor), config_length = sizeof(configuration);
        int iface = -1;
        SetLastError(0);
        HANDLE h = b->open(i);
        if (h == INVALID_HANDLE_VALUE) {
            report_append("CH341 index %lu: OpenDevice failed, Windows error %lu.\n", i, GetLastError());
            continue;
        }
        if (!b->descriptor341(i, descriptor, &length) || length != 18 ||
            descriptor[0] != 18 || descriptor[1] != 1) {
            report_append("CH341 index %lu: device descriptor unavailable/invalid; rejected.\n", i);
            b->close341(i); continue;
        }
        uint16_t vid = descriptor[8] | ((uint16_t)descriptor[9] << 8);
        uint16_t pid = descriptor[10] | ((uint16_t)descriptor[11] << 8);
        report_append("CH341 index %lu: VID=%04X PID=%04X.\n", i, vid, pid);
        if (!family_matches(FAMILY_CH341, vid, pid)) {
            report_append("Rejected: belongs to a different device family/mode.\n");
            b->close341(i); continue;
        }
        int config_result = -EIO;
        if (b->config341(i, configuration, &config_length) && config_length <= sizeof(configuration))
            config_result = validate_ch341_config(configuration, config_length, &iface);
        if (config_result) {
            b->close341(i);
            report_error("CH341 vendor configuration lacks a unique supported bulk02/82 interface (code %d).", config_result);
            failure = config_result; goto fail;
        }
        if (found) {
            b->close341(i);
            report_error("Multiple CH341A/B family devices found; refusing to choose a target.");
            failure = -EEXIST; goto fail;
        }
        b->index = i; b->opened = 1; found = 1;
        report_append("Accepted CH341A/B index %lu, SPI bulk interface %d. A/B silicon cannot be distinguished by these family USB IDs.\n", i, iface);
    }
    if (!found) { report_error("No matching CH341A/B parallel/SPI device opened through the official vendor driver."); goto fail; }
    if (!diagnostic_only && !b->timeout(b->index, 3000, 3000)) {
        report_error("CH341 SetTimeout failed (Windows error %lu).", GetLastError()); goto fail;
    }
    report_append("Selected backend: CH341 vendor DLL. %s\n", diagnostic_only ? "Metadata-only diagnostics; no GPIO/SPI setup." : "SPI setup is the next stage.");
    last_error[0] = '\0';
    return 0;
fail:
    ch347_windows_close(p);
    return failure;
}

static int libusb_open_impl(struct ch347_priv *p, int diagnostic_only, int family)
{
    struct windows_backend *b = (struct windows_backend *)calloc(1, sizeof(*b));
    libusb_device **devices = NULL;
    ssize_t count;
    int result = -ENODEV, candidates = 0;
    if (!b) { report_error("libusb: out of memory."); return -ENOMEM; }
    b->kind = 1;
    b->family = family;
    b->out_endpoint = family == FAMILY_CH341 ? 0x02 : 0x06;
    b->in_endpoint = family == FAMILY_CH341 ? 0x82 : 0x86;
    b->usb_interface = -1;
    p->windows_backend = b;
    report_append("Trying userspace libusb backend with the currently installed driver. No driver is installed, replaced or detached.\n");
    /* Preload the app-local helper securely. This does not load/install a kernel driver. */
    b->usbk_module = load_app_dll(L"libusbK.dll", "libusbK.dll (userspace helper)");
    if (!b->usbk_module) goto fail;
    b->module = load_app_dll(L"libusb-1.0.dll", "libusb-1.0.dll");
    if (!b->module) goto fail;
#define LOAD_USB(member, type, name) do { \
    FARPROC address = GetProcAddress(b->module, name); \
    _Static_assert(sizeof(type) == sizeof(address), "Windows function pointer ABI mismatch"); \
    memcpy(&b->member, &address, sizeof(address)); \
    if (!b->member) { report_error("libusb export missing: %s.", name); goto fail; } \
} while (0)
    LOAD_USB(usb_init, usb_init_fn, "libusb_init");
    LOAD_USB(usb_exit, usb_exit_fn, "libusb_exit");
    LOAD_USB(usb_list, usb_list_fn, "libusb_get_device_list");
    LOAD_USB(usb_free_list, usb_free_list_fn, "libusb_free_device_list");
    LOAD_USB(usb_descriptor, usb_descriptor_fn, "libusb_get_device_descriptor");
    LOAD_USB(usb_config, usb_config_fn, "libusb_get_active_config_descriptor");
    LOAD_USB(usb_free_config, usb_free_config_fn, "libusb_free_config_descriptor");
    LOAD_USB(usb_open, usb_open_fn, "libusb_open");
    LOAD_USB(usb_close, usb_close_fn, "libusb_close");
    LOAD_USB(usb_claim, usb_interface_fn, "libusb_claim_interface");
    LOAD_USB(usb_release, usb_interface_fn, "libusb_release_interface");
    LOAD_USB(usb_set_alt, usb_alt_fn, "libusb_set_interface_alt_setting");
    LOAD_USB(usb_bulk, usb_bulk_fn, "libusb_bulk_transfer");
    LOAD_USB(usb_error_name, usb_error_name_fn, "libusb_error_name");
#undef LOAD_USB
    result = b->usb_init(&b->usb_context);
    if (result) { report_error("libusb_init failed: %s (%d).", b->usb_error_name(result), result); goto fail; }
    count = b->usb_list(b->usb_context, &devices);
    if (count < 0) { result = (int)count; report_error("libusb enumeration failed: %s (%d).", b->usb_error_name(result), result); goto fail; }
    /* Count every matching physical candidate before opening any of them.
     * An inaccessible first device must not hide a second device behind a
     * different driver and trigger an unsafe vendor-backend fallback. */
    int matching_count = 0;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor descriptor;
        if (b->usb_descriptor(devices[i], &descriptor) == 0 &&
            family_matches(family, descriptor.idVendor, descriptor.idProduct))
            ++matching_count;
    }
    if (matching_count > 1) {
        report_error("libusb pre-open enumeration found %d %s devices. Refusing ambiguous cross-driver selection.", matching_count, family_name(family));
        result = -EEXIST;
        goto fail;
    }
    usb_preflight_complete = 1;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor descriptor;
        struct libusb_config_descriptor *config = NULL;
        int iface = -1, alternate = -1;
        int ret = b->usb_descriptor(devices[i], &descriptor);
        if (ret || !family_matches(family, descriptor.idVendor, descriptor.idProduct)) continue;
        ++candidates;
        report_append("libusb %s candidate %d: VID=%04X PID=%04X device-revision=%04X.\n",
                      family_name(family), candidates, descriptor.idVendor, descriptor.idProduct, descriptor.bcdDevice);
        if (candidates > 1) {
            report_error("libusb: multiple %s devices present; refusing to choose a target.", family_name(family));
            result = -EEXIST; goto fail;
        }
        ret = b->usb_config(devices[i], &config);
        if (ret) { report_error("libusb active configuration failed: %s (%d).", b->usb_error_name(ret), ret); result = ret; goto fail; }
        ret = ch34x_usb_select_bulk_interface(config, b->out_endpoint, b->in_endpoint, &iface, &alternate);
        b->usb_free_config(config);
        if (ret) {
            report_error("libusb descriptor selection: %s bulk endpoint pair 0x%02X/0x%02X. No hard-coded interface number was used.", ret == -2 ? "ambiguous/malformed" : "no matching", b->out_endpoint, b->in_endpoint);
            result = ret == -2 ? -EEXIST : -ENODEV; goto fail;
        }
        b->usb_interface = iface;
        b->usb_alternate = alternate;
        report_append("libusb selected interface=%d alternate=%d from actual bulk endpoint descriptors.\n", iface, alternate);
        ret = b->usb_open(devices[i], &b->usb_handle);
        if (ret) { report_error("libusb_open failed: %s (%d). Existing driver was left unchanged.", b->usb_error_name(ret), ret); result = ret; goto fail; }
    }
    if (devices) { b->usb_free_list(devices, 1); devices = NULL; }
    if (!candidates || !b->usb_handle) { report_error("libusb: no accessible %s matching the selected family IDs found. Inspect Device Manager and close other programmer applications.", family_name(family)); result = -ENODEV; goto fail; }
    if (!diagnostic_only) {
        /* Resolve all CH341 transfer capabilities before interface claim or
         * GPIO/SPI setup. An incomplete DLL may fall back only at open time;
         * never cache a partially populated async table during an exchange. */
#define LOAD_ASYNC(member, name) do { \
    FARPROC address = GetProcAddress(b->module, name); \
    _Static_assert(sizeof(b->async341.member) == sizeof(address), "Windows function pointer ABI mismatch"); \
    memcpy(&b->async341.member, &address, sizeof(address)); \
    if (!b->async341.member) { report_error("CH341 async libusb export missing: %s.", name); result = -ENOSYS; goto fail; } \
} while (0)
        if (family == FAMILY_CH341) {
            LOAD_ASYNC(alloc_transfer, "libusb_alloc_transfer");
            LOAD_ASYNC(free_transfer, "libusb_free_transfer");
            LOAD_ASYNC(submit_transfer, "libusb_submit_transfer");
            LOAD_ASYNC(cancel_transfer, "libusb_cancel_transfer");
            LOAD_ASYNC(handle_events_timeout, "libusb_handle_events_timeout");
        }
#undef LOAD_ASYNC
        result = b->usb_claim(b->usb_handle, b->usb_interface);
        if (result) { report_error("libusb_claim_interface(%d) failed: %s (%d). No driver was detached or replaced.", b->usb_interface, b->usb_error_name(result), result); goto fail; }
        b->usb_claimed = 1;
        if (b->usb_alternate != 0) {
            result = b->usb_set_alt(b->usb_handle, b->usb_interface, b->usb_alternate);
            if (result) { report_error("libusb alternate setting failed: %s (%d).", b->usb_error_name(result), result); goto fail; }
        }
        report_append("Selected backend: libusb; interface claimed. SPI initialization follows; no later backend switching is permitted.\n");
    } else report_append("Selected backend: libusb. Read-only open succeeded; no interface claim, alternate-setting change or SPI transfer was performed.\n");
    b->opened = 1;
    last_error[0] = '\0';
    return 0;
fail:
    if (devices && b->usb_free_list) b->usb_free_list(devices, 1);
    ch347_windows_close(p);
    return result ? result : -ENODEV;
}

static int open_compatible_backend(struct ch347_priv *p, int diagnostic_only, int family)
{
    char libusb_failure[900];
    int result = libusb_open_impl(p, diagnostic_only, family);
    if (result == 0 || result == -EEXIST) return result;
    if (!usb_preflight_complete) {
        ch347_windows_set_error_stage("Cross-driver uniqueness preflight (vendor fallback suppressed)", result);
        return result;
    }
    snprintf(libusb_failure, sizeof(libusb_failure), "%.899s", last_error);
    result = family == FAMILY_CH341 ? windows341_open_impl(p, diagnostic_only) : windows_open_impl(p, diagnostic_only);
    if (result) {
        char vendor_failure[900];
        snprintf(vendor_failure, sizeof(vendor_failure), "%.899s", last_error);
        report_error("No usable backend. libusb: %s WCH: %s", libusb_failure, vendor_failure);
    }
    return result;
}

int ch347_windows_open(struct ch347_priv *p)
{
    begin_connection_report(0, FAMILY_CH347);
    if (!p) { report_error("Open: invalid device context."); return -EINVAL; }
    return open_compatible_backend(p, 0, FAMILY_CH347);
}

int ch347_windows_diagnose(void)
{
    struct ch347_priv probe = {0};
    begin_connection_report(1, FAMILY_CH347);
    int result = open_compatible_backend(&probe, 1, FAMILY_CH347);
    ch347_windows_close(&probe);
    return result;
}

int ch341_windows_open(struct ch347_priv *p)
{
    begin_connection_report(0, FAMILY_CH341);
    if (!p) { report_error("CH341 open: invalid device context."); return -EINVAL; }
    return open_compatible_backend(p, 0, FAMILY_CH341);
}

int ch341_windows_diagnose(void)
{
    struct ch347_priv probe = {0};
    begin_connection_report(1, FAMILY_CH341);
    int result = open_compatible_backend(&probe, 1, FAMILY_CH341);
    ch347_windows_close(&probe);
    return result;
}

void ch347_windows_close(struct ch347_priv *p)
{
    struct windows_backend *b = p ? (struct windows_backend *)p->windows_backend : NULL;
    if (!b) return;
    if (b->kind == 1) {
        if (b->usb_claimed && b->usb_release) b->usb_release(b->usb_handle, b->usb_interface);
        if (b->usb_handle && b->usb_close) b->usb_close(b->usb_handle);
        if (b->usb_context && b->usb_exit) b->usb_exit(b->usb_context);
    } else if (b->opened) {
        if (b->family == FAMILY_CH341 && b->close341) b->close341(b->index);
        else if (b->close) b->close(b->index);
    }
    if (b->module) FreeLibrary(b->module);
    if (b->usbk_module) FreeLibrary(b->usbk_module);
    free(b);
    p->windows_backend = NULL;
}

int ch347_windows_transfer(struct ch347_priv *p, unsigned char endpoint,
                           unsigned char *data, int length, int *transferred,
                           unsigned int timeout)
{
    struct windows_backend *b = p ? (struct windows_backend *)p->windows_backend : NULL;
    ULONG actual;
    BOOL ok;
    if (transferred) *transferred = 0;
    if (!b || !b->opened) { report_error("USB transfer: no open WCH interface."); return -ENODEV; }
    if (!transferred || length < 0 || (length && !data)) { report_error("USB transfer: invalid buffer/length."); return -EINVAL; }
    if (endpoint != b->in_endpoint && endpoint != b->out_endpoint) { report_error("USB transfer: unsupported endpoint 0x%02X.", endpoint); return -ENOTSUP; }
    if (!length) return 0;
    if (b->kind == 1) {
        int actual_usb = 0;
        int ret = b->usb_bulk(b->usb_handle, endpoint, data, length, &actual_usb, timeout);
        if (ret || actual_usb <= 0 || actual_usb > length ||
            (endpoint == b->out_endpoint && actual_usb != length)) {
            report_error("libusb bulk endpoint=0x%02X requested=%d actual=%d: %s (%d). No retry or backend switch was attempted.",
                         endpoint, length, actual_usb, b->usb_error_name(ret), ret);
            return ret ? ret : -EIO;
        }
        *transferred = actual_usb;
        return 0;
    }
    SetLastError(0);
    if (!b->timeout(b->index, timeout, timeout)) {
        report_error("Transfer SetTimeout index=%lu failed (Windows error %lu).", b->index, GetLastError());
        return -EIO;
    }
    actual = (ULONG)length;
    SetLastError(0);
    ok = endpoint == b->in_endpoint ? b->read(b->index, data, &actual)
                               : b->write(b->index, data, &actual);
    if (!ok || !actual || actual > (ULONG)length) {
        report_error("%s index=%lu endpoint=0x%02X requested=%d actual=%lu result=%d Windows error=%lu.",
                     endpoint == b->in_endpoint ? "ReadData" : "WriteData", b->index,
                     endpoint, length, actual, (int)ok, GetLastError());
        return -EIO;
    }
    *transferred = (int)actual;
    if (endpoint == b->out_endpoint && actual != (ULONG)length) {
        report_error("WriteData short transfer: requested=%d actual=%lu. Command was not retried.", length, actual);
        return -EIO;
    }
    return 0;
}

int ch341_windows_exchange(struct ch347_priv *p, const uint8_t *tx, unsigned int tx_len,
                           uint8_t *rx, unsigned int rx_len)
{
    struct windows_backend *b = p ? p->windows_backend : NULL;
    if (!b || !b->opened || b->family != FAMILY_CH341) {
        report_error("CH341 exchange rejected: no opened CH341A/B family context.");
        return -ENODEV;
    }
    if (tx_len > 4096 || rx_len > 4096 || (tx_len && !tx) || (rx_len && !rx)) {
        report_error("CH341 exchange: invalid buffer/length; maximum batch is 4096 bytes.");
        return -EINVAL;
    }
    if (!tx_len && !rx_len) return 0;
    if (!rx_len) {
        int written = 0;
        int ret = ch347_windows_transfer(p, 0x02, (uint8_t *)tx, (int)tx_len, &written, 3000);
        return ret ? ret : (written == (int)tx_len ? 0 : -EIO);
    }
    if (b->kind == 1) {
        int ret = ch341_async_exchange(&b->async341, b->usb_context, b->usb_handle, tx, tx_len, rx, rx_len, 3000);
        if (ret) report_error("CH341 async SPI exchange failed: %s (%d). No backend switch or command retry was attempted.", b->usb_error_name(ret), ret);
        return ret;
    }
    /* CH341WriteRead queues packet reads in the driver. Its final read may be
     * shorter than 31 bytes; receive into a rounded buffer and demand exact
     * total completion before copying to the caller. No CS-changing API is used. */
    uint8_t reply[4096 + 30];
    ULONG actual = 0;
    ULONG reads = (rx_len + 30U) / 31U;
    SetLastError(0);
    BOOL ok = b->write_read341(b->index, tx_len, (PVOID)tx, 31, reads, &actual, reply);
    if (!ok || actual != rx_len) {
        report_error("CH341WriteRead failed/short: requested=%u actual=%lu result=%d Windows error=%lu. No retry.", rx_len, actual, (int)ok, GetLastError());
        return -EIO;
    }
    memcpy(rx, reply, rx_len);
    return 0;
}
