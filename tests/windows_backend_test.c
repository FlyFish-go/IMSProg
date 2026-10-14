/* Execute the production Windows backend on a host using fake DLL/USB APIs.
 * No Windows runtime, driver, USB device or NAND chip is contacted. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../IMSProg_programmer/windows/ch347_win32.c"

struct libusb_context { int unused; };
struct libusb_device { int index; };
struct libusb_device_handle { int index; };
struct fake_state {
    int missing_wch_app, missing_wch_system, missing_usbk, missing_libusb;
    int missing_ch341_app, missing_ch341_system;
    int wch341_present[16], wch341_opens, wch341_closes, wch341_loads, wch341_system_loads;
    int wch341_timeouts, wch341_reads, wch341_writes, wch341_exchanges;
    int wch341_actual, wch341_io_result, wch341_descriptor_length;
    uint16_t wch341_vid, wch341_pid;
    uint8_t wch341_configuration[80];
    ULONG wch341_configuration_length;
    ULONG wch341_step, wch341_times;
    int async_calls, async_result;
    const char *missing_export;
    int wch_present[32], wch_bad_info[32];
    int wch_wrong_mode, wch_wrong_chip_type, wch_ch347f, wch_timeout_result, wch_io_result, wch_actual;
    int wch_loads, wch_system_loads, wch_opens, wch_closes, wch_timeouts, wch_reads, wch_writes;
    int usb_loads, usbk_loads, frees;
    int load_path_count;
    WCHAR load_paths[8][MAX_PATH];
    int usb_count, usb_init_result, usb_list_result, usb_open_result, usb_claim_result, usb_alt_result;
    int usb_bulk_result, usb_actual;
    int usb_init_calls, usb_exit_calls, usb_open_calls, usb_close_calls, usb_claim_calls;
    int usb_release_calls, usb_alt_calls, usb_bulk_calls, usb_free_lists, usb_free_configs;
    int claimed_interface, alternate_interface, alternate_value, opened_device_index;
    DWORD last_error;
    struct libusb_device_descriptor descriptors[4];
    struct libusb_config_descriptor configs[4];
    struct libusb_interface interfaces[4][2];
    struct libusb_interface_descriptor alternatives[4][2];
    struct libusb_endpoint_descriptor endpoints[4][2][2];
} f;
static struct libusb_context context;
static struct libusb_device devices[4];
static struct libusb_device_handle handles[4];
static libusb_device *device_list[5];
static int checks;
#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

static void reset(void)
{
    memset(&f, 0, sizeof(f));
    f.wch_timeout_result = f.wch_io_result = f.wch341_io_result = 1;
    f.wch341_actual = -1; f.wch341_descriptor_length = 18;
    f.wch341_vid = 0x1a86; f.wch341_pid = 0x5512;
    const uint8_t configuration[] = {
        9,2,32,0,1,1,0,0x80,50, 9,4,0,0,2,0xff,0,0,0,
        7,5,0x02,2,32,0,0, 7,5,0x82,2,32,0,0
    };
    memcpy(f.wch341_configuration, configuration, sizeof(configuration));
    f.wch341_configuration_length = sizeof(configuration);
    f.wch_actual = f.usb_actual = -1;
    for (int i = 0; i < 4; ++i) {
        devices[i].index = handles[i].index = i;
        device_list[i] = &devices[i];
        f.descriptors[i].idVendor = CH347_SPI_VID;
        f.descriptors[i].idProduct = CH347_SPI_PID;
        f.descriptors[i].bcdDevice = 0x0544;
        f.configs[i].bNumInterfaces = 1;
        f.configs[i].interface = f.interfaces[i];
        for (int j = 0; j < 2; ++j) {
            f.interfaces[i][j].num_altsetting = 1;
            f.interfaces[i][j].altsetting = &f.alternatives[i][j];
            f.alternatives[i][j].bInterfaceNumber = (uint8_t)(7 + j);
            f.alternatives[i][j].bNumEndpoints = 2;
            f.alternatives[i][j].endpoint = f.endpoints[i][j];
            f.endpoints[i][j][0].bEndpointAddress = 0x06;
            f.endpoints[i][j][1].bEndpointAddress = 0x86;
            f.endpoints[i][j][0].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
            f.endpoints[i][j][1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
            f.endpoints[i][j][0].wMaxPacketSize = 512;
            f.endpoints[i][j][1].wMaxPacketSize = 512;
        }
    }
    device_list[4] = NULL;
}
DWORD GetModuleFileNameW(HMODULE module, WCHAR *out, DWORD capacity)
{
    const wchar_t path[] = L"C:\\test\\IMSProg.exe";
    (void)module;
    assert(capacity > sizeof(path) / sizeof(path[0]));
    wcscpy(out, path);
    return (DWORD)wcslen(path);
}
UINT GetSystemDirectoryW(WCHAR *out, UINT capacity)
{
    const wchar_t path[] = L"C:\\Windows\\System32";
    assert(capacity > sizeof(path) / sizeof(path[0]));
    wcscpy(out, path);
    return (UINT)wcslen(path);
}
DWORD GetLastError(void) { return f.last_error; }
void SetLastError(DWORD error) { f.last_error = error; }
HMODULE LoadLibraryExW(const WCHAR *path, HANDLE reserved, DWORD flags)
{
    int app_local = wcsncmp(path, L"C:\\test\\", 8) == 0;
    int system32 = wcsncmp(path, L"C:\\Windows\\System32\\", 20) == 0;
    assert(!reserved);
    assert(flags == (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    assert(app_local || system32);
    assert(f.load_path_count < (int)(sizeof(f.load_paths) / sizeof(f.load_paths[0])));
    wcscpy(f.load_paths[f.load_path_count++], path);
    f.last_error = 126;
    if (wcsstr(path, L"CH341DLLA64.DLL") || wcsstr(path, L"CH341DLL.DLL")) {
        if (system32) ++f.wch341_system_loads; else ++f.wch341_loads;
        return (system32 ? f.missing_ch341_system : f.missing_ch341_app) ? NULL : (HMODULE)(uintptr_t)4;
    }
    if (wcsstr(path, L"CH347DLLA64.DLL") || wcsstr(path, L"CH347DLL.DLL")) {
        if (system32) ++f.wch_system_loads; else ++f.wch_loads;
        return (system32 ? f.missing_wch_system : f.missing_wch_app) ? NULL : (HMODULE)(uintptr_t)1;
    }
    if (wcsstr(path, L"libusbK.dll")) { assert(app_local); ++f.usbk_loads; return f.missing_usbk ? NULL : (HMODULE)(uintptr_t)2; }
    if (wcsstr(path, L"libusb-1.0.dll")) { assert(app_local); ++f.usb_loads; return f.missing_libusb ? NULL : (HMODULE)(uintptr_t)3; }
    assert(!"unexpected DLL"); return NULL;
}
BOOL FreeLibrary(HMODULE module) { assert(module); ++f.frees; return TRUE; }
static HANDLE fake_wch_open(ULONG index)
{
    assert(index < 32);
    ++f.wch_opens;
    /* Null index zero is valid under the official DLL API. */
    return f.wch_present[index] ? (HANDLE)(uintptr_t)index : INVALID_HANDLE_VALUE;
}
static BOOL fake_wch_close(ULONG index) { assert(f.wch_present[index]); ++f.wch_closes; return TRUE; }
static BOOL fake_wch_info(ULONG index, wch_device_info *info)
{
    if (f.wch_bad_info[index]) return FALSE;
    memset(info, 0, sizeof(*info));
    strcpy(info->DeviceID, f.wch_ch347f ? "USB\\VID_1A86&PID_55DE&MI_04" : "USB\\VID_1A86&PID_55DB");
    info->FuncType = f.wch_ch347f ? 3 : 1;
    info->ChipMode = f.wch_wrong_mode ? 3 : (f.wch_ch347f ? 0 : 1);
    info->CH347IfNum = f.wch_ch347f ? CH347F_SPI_IF : 7;
    /* WCH reports endpoint numbers here; direction is supplied by Read/Write. */
    info->DataDnEndp = info->DataUpEndp = 0x06;
    return TRUE;
}
static UCHAR fake_wch_chip_type(ULONG index)
{ (void)index; return f.wch_wrong_chip_type ? 0 : (f.wch_ch347f ? 2 : 1); }
static BOOL fake_wch_timeout(ULONG index, ULONG write, ULONG read)
{ (void)index; assert(write > 0 && read > 0); ++f.wch_timeouts; return f.wch_timeout_result; }
static BOOL fake_wch_read(ULONG index, PVOID data, PULONG size)
{ (void)index; (void)data; ++f.wch_reads; if (f.wch_actual >= 0) *size = (ULONG)f.wch_actual; return f.wch_io_result; }
static BOOL fake_wch_write(ULONG index, PVOID data, PULONG size)
{ (void)index; (void)data; ++f.wch_writes; if (f.wch_actual >= 0) *size = (ULONG)f.wch_actual; return f.wch_io_result; }
static HANDLE fake_wch341_open(ULONG index)
{ assert(index < 16); ++f.wch341_opens; return f.wch341_present[index] ? (HANDLE)(uintptr_t)index : INVALID_HANDLE_VALUE; }
static VOID fake_wch341_close(ULONG index) { assert(index < 16 && f.wch341_present[index]); ++f.wch341_closes; }
static BOOL fake_wch341_descriptor(ULONG index, PVOID output, PULONG size)
{
    (void)index; uint8_t data[18] = {18,1};
    data[8] = f.wch341_vid; data[9] = f.wch341_vid >> 8;
    data[10] = f.wch341_pid; data[11] = f.wch341_pid >> 8;
    assert(*size >= sizeof(data)); memcpy(output, data, sizeof(data));
    *size = f.wch341_descriptor_length; return TRUE;
}
static BOOL fake_wch341_config(ULONG index, PVOID output, PULONG size)
{ (void)index; assert(*size >= f.wch341_configuration_length); memcpy(output, f.wch341_configuration, f.wch341_configuration_length); *size = f.wch341_configuration_length; return TRUE; }
static BOOL fake_wch341_timeout(ULONG index, ULONG write, ULONG read)
{ (void)index; assert(write && read); ++f.wch341_timeouts; return f.wch_timeout_result; }
static BOOL fake_wch341_read(ULONG index, PVOID data, PULONG size)
{ (void)index; (void)data; ++f.wch341_reads; if (f.wch341_actual >= 0) *size = f.wch341_actual; return f.wch341_io_result; }
static BOOL fake_wch341_write(ULONG index, PVOID data, PULONG size)
{ (void)index; (void)data; ++f.wch341_writes; if (f.wch341_actual >= 0) *size = f.wch341_actual; return f.wch341_io_result; }
static BOOL fake_wch341_exchange(ULONG index, ULONG length, PVOID tx, ULONG step, ULONG times, PULONG actual, PVOID rx)
{
    (void)index; assert(length && tx && rx); ++f.wch341_exchanges;
    f.wch341_step = step; f.wch341_times = times;
    *actual = f.wch341_actual < 0 ? step * times : (ULONG)f.wch341_actual;
    /* Real DLL may fill a rounded final packet: production must use its private
     * rounded reply buffer and only copy a fully successful exact reply. */
    memset(rx, 0x5a, step * times);
    return f.wch341_io_result;
}
static struct libusb_transfer *fake_usb_alloc_transfer(int count) { (void)count; return NULL; }
static void fake_usb_free_transfer(struct libusb_transfer *t) { (void)t; }
static int fake_usb_submit_transfer(struct libusb_transfer *t) { (void)t; return 0; }
static int fake_usb_cancel_transfer(struct libusb_transfer *t) { (void)t; return 0; }
static int fake_usb_events(libusb_context *c, struct timeval *v) { (void)c; (void)v; return 0; }
/* Helper internals are independently covered by ch341_async_test.c. Here check
 * the production backend routes only CH341 contexts to that helper. */
int ch341_async_exchange(const struct ch341_async_api *api, libusb_context *c,
    libusb_device_handle *h, const uint8_t *tx, unsigned int tx_len,
    uint8_t *rx, unsigned int rx_len, unsigned int timeout)
{
    assert(api && api->alloc_transfer && api->free_transfer && api->submit_transfer && api->cancel_transfer && api->handle_events_timeout);
    assert(c == &context && h && tx && tx_len && rx && rx_len && timeout);
    assert(f.descriptors[h->index].idProduct == 0x5512);
    ++f.async_calls;
    if (!f.async_result) memset(rx, 0x3c, rx_len);
    return f.async_result;
}
static void make_ch341(int index, uint16_t vid)
{
    f.descriptors[index].idVendor = vid; f.descriptors[index].idProduct = 0x5512;
    f.endpoints[index][0][0].bEndpointAddress = 0x02;
    f.endpoints[index][0][1].bEndpointAddress = 0x82;
}
static int fake_usb_init(libusb_context **out) { ++f.usb_init_calls; *out = f.usb_init_result ? NULL : &context; return f.usb_init_result; }
static void fake_usb_exit(libusb_context *ctx) { assert(ctx == &context); ++f.usb_exit_calls; }
static ssize_t fake_usb_list(libusb_context *ctx, libusb_device ***out)
{ assert(ctx == &context); if (f.usb_list_result < 0) return f.usb_list_result; *out = device_list; return f.usb_count; }
static void fake_usb_free_list(libusb_device **list, int unref) { assert(list == device_list && unref == 1); ++f.usb_free_lists; }
static int fake_usb_descriptor(libusb_device *dev, struct libusb_device_descriptor *out) { *out = f.descriptors[dev->index]; return 0; }
static int fake_usb_config(libusb_device *dev, struct libusb_config_descriptor **out) { *out = &f.configs[dev->index]; return 0; }
static void fake_usb_free_config(struct libusb_config_descriptor *config) { assert(config); ++f.usb_free_configs; }
static int fake_usb_open(libusb_device *dev, libusb_device_handle **out)
{ ++f.usb_open_calls; f.opened_device_index = dev->index; *out = f.usb_open_result ? NULL : &handles[dev->index]; return f.usb_open_result; }
static void fake_usb_close(libusb_device_handle *h) { assert(h); ++f.usb_close_calls; }
static int fake_usb_claim(libusb_device_handle *h, int iface)
{ assert(h); ++f.usb_claim_calls; f.claimed_interface = iface; return f.usb_claim_result; }
static int fake_usb_release(libusb_device_handle *h, int iface)
{ assert(h && iface == f.claimed_interface); ++f.usb_release_calls; return 0; }
static int fake_usb_alt(libusb_device_handle *h, int iface, int alternate)
{ assert(h); ++f.usb_alt_calls; f.alternate_interface = iface; f.alternate_value = alternate; return f.usb_alt_result; }
static int fake_usb_bulk(libusb_device_handle *h, unsigned char ep, unsigned char *data, int length, int *out, unsigned int timeout)
{ assert(h && data && (ep == 0x06 || ep == 0x86 || ep == 0x02 || ep == 0x82) && timeout); ++f.usb_bulk_calls; *out = f.usb_actual < 0 ? length : f.usb_actual; return f.usb_bulk_result; }
static const char *fake_usb_error_name(int value) { (void)value; return "FAKE_USB_RESULT"; }
#define EXPORT(symbol, fn) if (strcmp(name, symbol) == 0) { \
    __typeof__(&fn) typed = &fn; FARPROC ptr; \
    _Static_assert(sizeof(ptr) == sizeof(typed), "fake function pointer ABI"); \
    memcpy(&ptr, &typed, sizeof(ptr)); return ptr; }
FARPROC GetProcAddress(HMODULE module, const char *name)
{
    if (f.missing_export && strcmp(name, f.missing_export) == 0) return NULL;
    if ((uintptr_t)module == 1) {
        EXPORT("CH347OpenDevice", fake_wch_open)
        EXPORT("CH347CloseDevice", fake_wch_close)
        EXPORT("CH347GetDeviceInfor", fake_wch_info)
        EXPORT("CH347GetChipType", fake_wch_chip_type)
        EXPORT("CH347SetTimeout", fake_wch_timeout)
        EXPORT("CH347ReadData", fake_wch_read)
        EXPORT("CH347WriteData", fake_wch_write)
    } else if ((uintptr_t)module == 4) {
        EXPORT("CH341OpenDevice", fake_wch341_open)
        EXPORT("CH341CloseDevice", fake_wch341_close)
        EXPORT("CH341GetDeviceDescr", fake_wch341_descriptor)
        EXPORT("CH341GetConfigDescr", fake_wch341_config)
        EXPORT("CH341SetTimeout", fake_wch341_timeout)
        EXPORT("CH341ReadData", fake_wch341_read)
        EXPORT("CH341WriteData", fake_wch341_write)
        EXPORT("CH341WriteRead", fake_wch341_exchange)
    } else if ((uintptr_t)module == 3) {
        EXPORT("libusb_alloc_transfer", fake_usb_alloc_transfer)
        EXPORT("libusb_free_transfer", fake_usb_free_transfer)
        EXPORT("libusb_submit_transfer", fake_usb_submit_transfer)
        EXPORT("libusb_cancel_transfer", fake_usb_cancel_transfer)
        EXPORT("libusb_handle_events_timeout", fake_usb_events)
        EXPORT("libusb_init", fake_usb_init)
        EXPORT("libusb_exit", fake_usb_exit)
        EXPORT("libusb_get_device_list", fake_usb_list)
        EXPORT("libusb_free_device_list", fake_usb_free_list)
        EXPORT("libusb_get_device_descriptor", fake_usb_descriptor)
        EXPORT("libusb_get_active_config_descriptor", fake_usb_config)
        EXPORT("libusb_free_config_descriptor", fake_usb_free_config)
        EXPORT("libusb_open", fake_usb_open)
        EXPORT("libusb_close", fake_usb_close)
        EXPORT("libusb_claim_interface", fake_usb_claim)
        EXPORT("libusb_release_interface", fake_usb_release)
        EXPORT("libusb_set_interface_alt_setting", fake_usb_alt)
        EXPORT("libusb_bulk_transfer", fake_usb_bulk)
        EXPORT("libusb_error_name", fake_usb_error_name)
    }
    assert(!"unexpected DLL export"); return NULL;
}
#undef EXPORT

int main(void)
{
    struct ch347_priv p = {0};
    unsigned char packet[8] = {0};
    int transferred;

#ifdef _WIN64
    const wchar_t *expected_ch347_dll = L"CH347DLLA64.DLL";
    const wchar_t *expected_ch341_dll = L"CH341DLLA64.DLL";
#else
    const wchar_t *expected_ch347_dll = L"CH347DLL.DLL";
    const wchar_t *expected_ch341_dll = L"CH341DLL.DLL";
#endif

    /* Vendor DLLs use only explicit app-local then System32 paths. libusb
     * helpers remain app-local and never inherit the WCH fallback. */
    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS;
    f.missing_wch_app = 1; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.wch_loads == 1 && f.wch_system_loads == 1);
    CHECK(f.load_path_count == 4);
    CHECK(wcscmp(f.load_paths[0], L"C:\\test\\libusbK.dll") == 0);
    CHECK(wcscmp(f.load_paths[1], L"C:\\test\\libusb-1.0.dll") == 0);
    CHECK(wcsncmp(f.load_paths[2], L"C:\\test\\", 8) == 0 && wcsstr(f.load_paths[2], expected_ch347_dll));
    CHECK(wcsncmp(f.load_paths[3], L"C:\\Windows\\System32\\", 20) == 0 && wcsstr(f.load_paths[3], expected_ch347_dll));
    CHECK(strstr(ch347_windows_last_report(), "System32 WCH DLL load: success") != NULL);
    ch347_windows_close(&p);

    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS;
    f.missing_wch_app = f.missing_wch_system = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.wch_opens == 0);
    CHECK(strstr(ch347_windows_last_report(), "app-local (Windows error 126) and System32 (Windows error 126)") != NULL);

    reset(); f.usb_count = 1; make_ch341(0, 0x1a86); f.usb_open_result = LIBUSB_ERROR_ACCESS;
    f.missing_ch341_app = 1; f.wch341_present[0] = 1;
    CHECK(ch341_windows_open(&p) == 0);
    CHECK(f.wch341_loads == 1 && f.wch341_system_loads == 1);
    CHECK(wcsncmp(f.load_paths[3], L"C:\\Windows\\System32\\", 20) == 0 && wcsstr(f.load_paths[3], expected_ch341_dll));
    ch347_windows_close(&p);

    /* WCH is selected only after a successful single-device preflight. */
    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.wch_timeouts == 1 && f.usb_loads == 1 && f.usb_init_calls == 1);
    CHECK(ch347_windows_transfer(&p, 0x06, packet, sizeof(packet), &transferred, 1000) == 0);
    CHECK(transferred == sizeof(packet) && f.wch_writes == 1 && f.usb_bulk_calls == 0);
    f.wch_actual = 3;
    CHECK(ch347_windows_transfer(&p, 0x06, packet, sizeof(packet), &transferred, 1000) != 0);
    CHECK(f.wch_writes == 2 && f.usb_loads == 1); /* Never retry/switch after a command. */
    ch347_windows_close(&p); CHECK(!p.windows_backend && f.wch_closes == 1);

    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch_present[31] = 1;
    CHECK(ch347_windows_open(&p) == 0 && f.wch_opens == 32);
    ch347_windows_close(&p);
    CHECK(f.wch_closes == 1);

    /* One device visible through both APIs is one physical candidate, not two. */
    reset(); f.usb_count = 1; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.wch_opens == 0 && f.wch_loads == 0 && f.usb_open_calls == 1 && f.usb_claim_calls == 1);
    CHECK(f.claimed_interface == 7 && f.usb_alt_calls == 0 && f.usb_bulk_calls == 0);
    CHECK(strstr(ch347_windows_last_report(), "interface=7 alternate=0") != NULL);
    CHECK(ch347_windows_transfer(&p, 0x06, packet, sizeof(packet), &transferred, 1000) == 0);
    CHECK(transferred == sizeof(packet) && f.usb_bulk_calls == 1 && f.wch_writes == 0);
    f.usb_bulk_result = LIBUSB_ERROR_IO;
    CHECK(ch347_windows_transfer(&p, 0x06, packet, sizeof(packet), &transferred, 1000) != 0);
    CHECK(f.usb_bulk_calls == 2 && f.wch_opens == 0 && f.wch_writes == 0);
    ch347_windows_close(&p);
    CHECK(f.usb_release_calls == 1 && f.usb_close_calls == 1 && f.usb_exit_calls == 1 && !p.windows_backend);

    reset(); f.usb_count = 1; f.alternatives[0][0].bAlternateSetting = 3;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.usb_claim_calls == 1 && f.usb_alt_calls == 1 && f.alternate_interface == 7 && f.alternate_value == 3);
    ch347_windows_close(&p);

    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.usb_open_calls == 1 && f.usb_claim_calls == 0 && f.wch_opens > 0 && f.wch_timeouts == 1);
    CHECK(strstr(ch347_windows_last_report(), "libusb_open failed") != NULL);
    ch347_windows_close(&p);

    reset(); f.usb_count = 1; f.usb_claim_result = LIBUSB_ERROR_BUSY; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(f.usb_claim_calls == 1 && f.usb_close_calls == 1 && f.wch_timeouts == 1);
    ch347_windows_close(&p);

    reset(); f.wch_present[0] = f.wch_present[1] = 1;
    CHECK(ch347_windows_open(&p) == -EEXIST);
    CHECK(f.wch_closes == 2 && f.usb_bulk_calls == 0 && !p.windows_backend);
    reset(); f.usb_count = 2;
    CHECK(ch347_windows_open(&p) == -EEXIST);
    CHECK(f.usb_claim_calls == 0 && f.usb_bulk_calls == 0 && f.wch_opens == 0 && !p.windows_backend);
    /* A first WCH-bound device may refuse libusb_open. Count ALL devices first. */
    reset(); f.usb_count = 2; f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) == -EEXIST);
    CHECK(f.usb_open_calls == 0 && f.wch_opens == 0 && f.usb_claim_calls == 0 && !p.windows_backend);
    reset(); f.usb_count = 1; f.configs[0].bNumInterfaces = 2;
    CHECK(ch347_windows_open(&p) == -EEXIST);
    CHECK(f.usb_open_calls == 0 && f.usb_claim_calls == 0 && f.wch_opens == 0 && !p.windows_backend);

    reset(); f.usb_count = 1; f.usb_claim_result = LIBUSB_ERROR_BUSY;
    CHECK(ch347_windows_open(&p) != 0);
    CHECK(f.usb_claim_calls == 1 && f.usb_release_calls == 0 && f.usb_bulk_calls == 0 && f.usb_close_calls == 1);
    reset(); f.usb_count = 1; f.alternatives[0][0].bAlternateSetting = 1; f.usb_alt_result = LIBUSB_ERROR_IO;
    CHECK(ch347_windows_open(&p) != 0);
    CHECK(f.usb_claim_calls == 1 && f.usb_release_calls == 1 && f.usb_bulk_calls == 0 && f.usb_close_calls == 1);
    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_claim_calls == 0 && !p.windows_backend);
    CHECK(strstr(ch347_windows_last_report(), "left unchanged") != NULL);
    CHECK(strstr(ch347_windows_last_error(), "libusb:") != NULL &&
          strstr(ch347_windows_last_error(), "WCH:") != NULL &&
          strstr(ch347_windows_last_error(), "libusb_open failed") != NULL &&
          strstr(ch347_windows_last_error(), "no matching CH347T") != NULL);

    /* CH347F (1A86:55DE): selected explicitly; never matched by the CH347T selection and vice versa. */
    ch347_windows_select_model(CH347_MODEL_F);
    reset(); f.usb_count = 1; f.descriptors[0].idProduct = CH347F_SPI_PID;
    f.alternatives[0][0].bInterfaceNumber = CH347F_SPI_IF;
    CHECK(ch347_windows_open(&p) == 0 && f.usb_claim_calls == 1 && f.claimed_interface == CH347F_SPI_IF);
    CHECK(strstr(ch347_windows_last_report(), "CH347F") != NULL && strstr(ch347_windows_last_report(), "PID_55DE") != NULL);
    ch347_windows_close(&p);
    reset(); f.usb_count = 1; /* a CH347T is attached while CH347F is selected */
    CHECK(ch347_windows_open(&p) != 0 && f.usb_open_calls == 0 && f.usb_claim_calls == 0 && !p.windows_backend);
    reset(); f.wch_present[0] = 1; f.wch_ch347f = 1;
    CHECK(ch347_windows_open(&p) == 0 && f.wch_timeouts == 1);
    CHECK(strstr(ch347_windows_last_report(), "ChipType=2 FuncType=3 ChipMode=0 Interface=4") != NULL);
    ch347_windows_close(&p);
    reset(); f.wch_present[0] = 1; /* WCH reports CH347T mode 1 */
    CHECK(ch347_windows_open(&p) != 0 && f.wch_timeouts == 0 &&
          strstr(ch347_windows_last_error(), "no matching CH347F") != NULL);
    reset(); f.wch_present[0] = f.wch_present[1] = 1; f.wch_ch347f = 1;
    CHECK(ch347_windows_open(&p) == -EEXIST && !p.windows_backend);
    reset(); f.wch_present[0] = 1; f.wch_ch347f = 1; f.wch_wrong_chip_type = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.wch_timeouts == 0);
    reset(); f.usb_count = 1; f.descriptors[0].idProduct = CH347F_SPI_PID;
    CHECK(ch347_windows_diagnose() == 0 && f.usb_claim_calls == 0 && f.usb_bulk_calls == 0);
    ch347_windows_select_model(CH347_MODEL_T);
    reset(); f.usb_count = 1; f.descriptors[0].idProduct = CH347F_SPI_PID;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_open_calls == 0 && !p.windows_backend);
    reset(); f.wch_present[0] = 1; f.wch_ch347f = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.wch_timeouts == 0);

    reset(); f.usb_count = 1; f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch_present[0] = 1;
    CHECK(ch347_windows_diagnose() == 0);
    CHECK(f.wch_timeouts == 0 && f.wch_reads == 0 && f.wch_writes == 0 && f.wch_closes == 1);
    reset(); f.usb_count = 1; f.alternatives[0][0].bAlternateSetting = 3;
    CHECK(ch347_windows_diagnose() == 0);
    CHECK(f.usb_open_calls == 1 && f.usb_close_calls == 1 && f.usb_claim_calls == 0 && f.usb_alt_calls == 0 && f.usb_bulk_calls == 0);
    CHECK(strstr(ch347_windows_last_report(), "no interface claim") != NULL);
    reset(); f.usb_count = 2;
    CHECK(ch347_windows_diagnose() == -EEXIST);
    CHECK(f.usb_claim_calls == 0 && f.usb_bulk_calls == 0 && f.wch_opens == 0);

    reset(); f.usb_count = 2; f.descriptors[0].idProduct = 0x55dd;
    CHECK(ch347_windows_open(&p) == 0 && f.usb_open_calls == 1);
    ch347_windows_close(&p);
    reset(); f.usb_count = 1; f.descriptors[0].idVendor = 0xffff;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_open_calls == 0);
    reset(); f.usb_count = 1; f.endpoints[0][0][1].bEndpointAddress = 0x82;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_open_calls == 0);

    reset(); f.usb_count = 1;
    CHECK(ch347_windows_open(&p) == 0);
    CHECK(ch347_windows_transfer(&p, 0x82, packet, 8, &transferred, 1000) != 0 && f.usb_bulk_calls == 0);
    CHECK(ch347_windows_transfer(&p, 0x06, packet, 8, NULL, 1000) != 0 && f.usb_bulk_calls == 0);
    CHECK(ch347_windows_transfer(&p, 0x06, NULL, 8, &transferred, 1000) != 0 && f.usb_bulk_calls == 0);
    CHECK(ch347_windows_transfer(&p, 0x06, packet, 0, &transferred, 1000) == 0 && transferred == 0 && f.usb_bulk_calls == 0);
    const int invalid_lengths[] = {0, 3, 9};
    for (unsigned i = 0; i < sizeof(invalid_lengths)/sizeof(invalid_lengths[0]); ++i) {
        f.usb_actual = invalid_lengths[i];
        int before = f.usb_bulk_calls;
        CHECK(ch347_windows_transfer(&p, 0x06, packet, 8, &transferred, 1000) != 0);
        CHECK(f.usb_bulk_calls == before + 1 && f.wch_writes == 0 && f.wch_loads == 0);
    }
    f.usb_actual = 3;
    CHECK(ch347_windows_transfer(&p, 0x86, packet, 8, &transferred, 1000) == 0 && transferred == 3);
    ch347_windows_close(&p);
    ch347_windows_close(&p); /* Idempotent cleanup. */

    /* Without a complete cross-driver preflight, even an available WCH device
     * must not be opened. Diagnostics obeys the same fail-closed boundary. */
    reset(); f.missing_libusb = 1; f.usb_count = 1; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.wch_opens == 0 && f.wch_loads == 0 && !p.windows_backend);
    CHECK(strstr(ch347_windows_last_error(), "fallback suppressed") != NULL);
    CHECK(ch347_windows_diagnose() != 0 && f.wch_opens == 0 && f.usb_bulk_calls == 0);
    reset(); f.missing_usbk = 1; f.usb_count = 1; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_init_calls == 0 && f.wch_opens == 0 && !p.windows_backend);
    reset(); f.missing_export = "libusb_claim_interface"; f.usb_count = 1; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_init_calls == 0 && f.wch_opens == 0 && !p.windows_backend);
    reset(); f.usb_init_result = LIBUSB_ERROR_OTHER; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_exit_calls == 0 && f.wch_opens == 0 && !p.windows_backend);
    reset(); f.usb_list_result = LIBUSB_ERROR_IO; f.wch_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.usb_exit_calls == 1 && f.wch_opens == 0 && !p.windows_backend);

    /* Explicit family selection with both programmer families attached. */
    reset(); f.usb_count = 2; make_ch341(1, 0x1a86);
    CHECK(ch341_windows_open(&p) == 0 && f.opened_device_index == 1);
    CHECK(f.wch_loads == 0 && f.wch341_loads == 0);
    unsigned char reply[10]; memset(reply, 0xa5, sizeof(reply));
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), NULL, 0) == 0 && f.usb_bulk_calls == 1);
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), reply + 1, 8) == 0 && f.async_calls == 1);
    CHECK(reply[0] == 0xa5 && reply[9] == 0xa5 && reply[1] == 0x3c);
    f.async_result = LIBUSB_ERROR_IO;
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), reply + 1, 8) != 0 && f.async_calls == 2);
    CHECK(f.wch_loads == 0 && f.wch341_loads == 0 && f.opened_device_index == 1);
    CHECK(ch347_windows_transfer(&p, 0x06, packet, sizeof(packet), &transferred, 1000) != 0);
    ch347_windows_close(&p);
    reset(); f.usb_count = 2; make_ch341(1, 0x1a86);
    CHECK(ch347_windows_open(&p) == 0 && f.opened_device_index == 0);
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), NULL, 0) != 0 && f.usb_bulk_calls == 0);
    ch347_windows_close(&p);
    reset(); f.usb_count = 3; make_ch341(0, 0x1a86); make_ch341(1, 0x4348);
    CHECK(ch341_windows_open(&p) == -EEXIST && f.usb_open_calls == 0 && f.wch_loads == 0 && f.wch341_loads == 0);
    CHECK(ch347_windows_open(&p) == 0 && f.opened_device_index == 2);
    ch347_windows_close(&p);
    reset(); f.usb_count = 2; f.descriptors[0].idProduct = 0x7523; make_ch341(1, 0x4348);
    CHECK(ch341_windows_open(&p) == 0 && f.opened_device_index == 1);
    ch347_windows_close(&p);
    reset(); f.usb_count = 1; f.wch_present[0] = 1;
    CHECK(ch341_windows_open(&p) != 0 && f.wch_loads == 0 && f.usb_open_calls == 0);
    reset(); f.usb_count = 1; make_ch341(0, 0x1a86); f.wch341_present[0] = 1;
    CHECK(ch347_windows_open(&p) != 0 && f.wch341_loads == 0 && f.usb_open_calls == 0);

    /* Every CH341 async capability must be available before GPIO/SPI setup.
     * A missing export may choose the vendor backend only at open time. */
    const char *async_exports[] = {
        "libusb_alloc_transfer", "libusb_free_transfer", "libusb_submit_transfer",
        "libusb_cancel_transfer", "libusb_handle_events_timeout"
    };
    for (unsigned int i = 0; i < sizeof(async_exports) / sizeof(async_exports[0]); ++i) {
        reset(); f.usb_count = 1; make_ch341(0, 0x1a86);
        f.missing_export = async_exports[i];
        CHECK(ch341_windows_open(&p) != 0 && !p.windows_backend);
        CHECK(f.usb_claim_calls == 0 && f.usb_bulk_calls == 0 && f.async_calls == 0);
        CHECK(f.usb_close_calls == 1 && f.usb_exit_calls == 1);
        CHECK(strstr(ch347_windows_last_report(), async_exports[i]) != NULL);
        reset(); f.usb_count = 1; make_ch341(0, 0x1a86);
        f.missing_export = async_exports[i]; f.wch341_present[0] = 1;
        CHECK(ch341_windows_open(&p) == 0 && p.windows_backend);
        CHECK(f.usb_claim_calls == 0 && f.usb_bulk_calls == 0 && f.async_calls == 0);
        CHECK(f.wch341_timeouts == 1 && f.wch341_writes == 0 && f.wch341_exchanges == 0);
        ch347_windows_close(&p);
        CHECK(f.usb_close_calls == 1 && f.usb_exit_calls == 1 && f.wch341_closes == 1);
        /* Diagnostics promises metadata-only inspection, not SPI capability. */
        reset(); f.usb_count = 1; make_ch341(0, 0x1a86);
        f.missing_export = async_exports[i];
        CHECK(ch341_windows_diagnose() == 0);
        CHECK(f.usb_claim_calls == 0 && f.usb_bulk_calls == 0 && f.async_calls == 0 && f.wch341_loads == 0);
    }

    /* CH341 vendor selection, descriptor rejection, and exact rounded replies. */
    reset(); f.usb_count = 1; make_ch341(0, 0x1a86); f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch341_present[15] = 1;
    CHECK(ch341_windows_open(&p) == 0 && f.wch341_opens == 16 && f.wch341_timeouts == 1 && f.wch_loads == 0);
    memset(reply, 0xa5, sizeof(reply)); f.wch341_actual = 8;
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), reply + 1, 8) == 0);
    CHECK(f.wch341_step == 31 && f.wch341_times == 1 && f.wch341_exchanges == 1 && reply[1] == 0x5a && reply[0] == 0xa5 && reply[9] == 0xa5);
    uint8_t large_reply[4098]; memset(large_reply, 0xa5, sizeof(large_reply));
    f.wch341_actual = 4096;
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), large_reply + 1, 4096) == 0);
    CHECK(f.wch341_times == 133 && large_reply[0] == 0xa5 && large_reply[4097] == 0xa5 && large_reply[4096] == 0x5a);
    f.wch341_actual = 7; memset(reply, 0xa5, sizeof(reply));
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), reply + 1, 8) != 0 && reply[1] == 0xa5);
    CHECK(f.wch341_exchanges == 3 && f.wch341_writes == 0 && f.wch_writes == 0 && f.wch_loads == 0);
    f.wch341_io_result = 0;
    CHECK(ch341_windows_exchange(&p, packet, sizeof(packet), reply + 1, 8) != 0 && f.wch341_exchanges == 4);
    ch347_windows_close(&p); CHECK(f.wch341_closes == 1);
    reset(); f.wch341_present[0] = 1; f.wch341_pid = 0x55db; f.wch_present[0] = 1;
    CHECK(ch341_windows_open(&p) != 0 && f.wch341_closes == 1 && f.wch_loads == 0);
    reset(); f.wch341_present[0] = 1; f.wch341_descriptor_length = 17;
    CHECK(ch341_windows_open(&p) != 0 && f.wch341_timeouts == 0 && f.wch341_closes == 1);
    reset(); f.wch341_present[0] = 1; f.wch341_configuration[12] = 1;
    CHECK(ch341_windows_open(&p) != 0 && f.wch341_timeouts == 0 && f.wch341_closes == 1);
    reset(); f.wch341_present[0] = 1; f.wch341_configuration[27] = 0x86;
    CHECK(ch341_windows_open(&p) != 0 && f.wch341_timeouts == 0);
    reset(); f.wch341_present[0] = f.wch341_present[1] = 1;
    CHECK(ch341_windows_open(&p) == -EEXIST && f.wch341_closes == 2 && f.wch341_timeouts == 0);
    reset(); f.wch341_present[0] = 1;
    memcpy(f.wch341_configuration + 32, f.wch341_configuration + 9, 23);
    f.wch341_configuration[2] = 55; f.wch341_configuration[4] = 2; f.wch341_configuration[34] = 1; f.wch341_configuration_length = 55;
    CHECK(ch341_windows_open(&p) == -EEXIST && f.wch341_timeouts == 0);

    reset(); f.usb_count = 2; make_ch341(1, 0x1a86);
    CHECK(ch341_windows_diagnose() == 0 && f.opened_device_index == 1);
    CHECK(f.usb_claim_calls == 0 && f.usb_alt_calls == 0 && f.usb_bulk_calls == 0 && f.async_calls == 0 && f.wch_loads == 0);
    reset(); f.usb_count = 1; make_ch341(0, 0x1a86); f.usb_open_result = LIBUSB_ERROR_ACCESS; f.wch341_present[0] = 1;
    CHECK(ch341_windows_diagnose() == 0);
    CHECK(f.wch341_timeouts == 0 && f.wch341_writes == 0 && f.wch341_reads == 0 && f.wch341_exchanges == 0 && f.wch_loads == 0);
    reset(); f.missing_libusb = 1; f.wch341_present[0] = 1;
    CHECK(ch341_windows_open(&p) != 0 && f.wch341_opens == 0 && f.wch_opens == 0);
    CHECK(ch341_windows_open(NULL) == -EINVAL);

    reset();
    CHECK(ch347_windows_open(NULL) == -EINVAL);
    CHECK(f.wch_loads == 0 && f.usb_loads == 0);
    puts("Windows backend integration: explicit CH341/CH347 family selection, app-local/System32-exact WCH loading with app-local-only libusb, SDK-backed CH347F metadata, descriptor-selected interface/alternate, ambiguity refusal, cleanup, diagnostic no-I/O and no transfer retries/backend switching passed.");
    printf("PASS: %d backend assertions against production C (fake Windows/DLL/USB APIs; no hardware I/O)\n", checks);
    return 0;
}
