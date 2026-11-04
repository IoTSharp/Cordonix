#define WIN32_LEAN_AND_MEAN
#define CONST_VTABLE
#include "cdx/phone.h"
#include <roapi.h>
#include <winstring.h>
#include <windows.devices.bluetooth.h>
#include <windows.devices.enumeration.h>
#include <asyncinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothDeviceStatics bluetooth_factory;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothDevice bluetooth_device;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothDevice2 bluetooth_device2;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformation device_info;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformation2 device_info2;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformationPairing device_pairing;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformationPairing2 device_pairing2;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformationCustomPairing custom_pairing;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDevicePairingRequestedEventArgs pairing_args;
typedef __x_ABI_CWindows_CDevices_CEnumeration_CIDevicePairingResult pairing_result;
typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceInformationCustomPairing_Windows__CDevices__CEnumeration__CDevicePairingRequestedEventArgs pairing_handler;
typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceInformationCustomPairing_Windows__CDevices__CEnumeration__CDevicePairingRequestedEventArgsVtbl pairing_handler_vtable;

/* The C ABI headers declare these IIDs; values are from the same SDK interfaces. */
static const IID bluetooth_factory_id = {0x0991df51,0x57db,0x4725,{0xbb,0xd7,0x84,0xf6,0x43,0x27,0xec,0x2c}};
static const IID bluetooth_device2_id = {0x0133f954,0xb156,0x4dd0,{0xb1,0xf5,0xc1,0x1b,0xc3,0x1a,0x51,0x63}};
static const IID device_info2_id = {0xf156a638,0x7997,0x48d9,{0xa1,0x0c,0x26,0x9d,0x46,0x53,0x3f,0x48}};
static const IID device_pairing2_id = {0xf68612fd,0x0aee,0x4328,{0x85,0xcc,0x1c,0x74,0x2b,0xb1,0x79,0x0d}};
static const IID pairing_handler_id = {0xfa65231f,0x4178,0x5de1,{0xb2,0xcc,0x03,0xe2,0x2d,0x77,0x02,0xb4}};
static const IID async_info_id = {0x36,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID unknown_id = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID agile_id = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};

typedef struct pairing_confirmation {
    pairing_handler handler;
    LONG references, entered;
    HWND parent;
    HANDLE finished;
    wchar_t name[256];
    int accepted, response_sent;
    unsigned kind;
    HRESULT response;
    ULONGLONG confirmation_ms, response_ms;
} pairing_confirmation;

static ULONG STDMETHODCALLTYPE confirmation_addref(pairing_handler *self) {
    return (ULONG)InterlockedIncrement(&((pairing_confirmation *)self)->references);
}
static ULONG STDMETHODCALLTYPE confirmation_release(pairing_handler *self) {
    pairing_confirmation *confirmation = (pairing_confirmation *)self;
    ULONG refs = (ULONG)InterlockedDecrement(&confirmation->references);
    if (!refs) { CloseHandle(confirmation->finished); free(confirmation); }
    return refs;
}
static HRESULT STDMETHODCALLTYPE confirmation_query(pairing_handler *self, REFIID id, void **object) {
    if (!object) return E_POINTER;
    *object = NULL;
    if (memcmp(id, &unknown_id, sizeof(*id)) && memcmp(id, &agile_id, sizeof(*id)) &&
        memcmp(id, &pairing_handler_id, sizeof(*id))) return E_NOINTERFACE;
    *object = self; (void)confirmation_addref(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE confirmation_invoke(pairing_handler *self, custom_pairing *sender, pairing_args *args) {
    pairing_confirmation *confirmation = (pairing_confirmation *)self;
    enum __x_ABI_CWindows_CDevices_CEnumeration_CDevicePairingKinds kind = DevicePairingKinds_None;
    __x_ABI_CWindows_CFoundation_CIDeferral *deferral = NULL;
    HSTRING pin = NULL;
    wchar_t message[1024];
    HRESULT result;
    ULONGLONG started = GetTickCount64(), response_started = 0;
    (void)sender;
    InterlockedExchange(&confirmation->entered, 1);
    result = args->lpVtbl->GetDeferral(args, &deferral);
    if (SUCCEEDED(result)) result = args->lpVtbl->get_PairingKind(args, &kind);
    if (SUCCEEDED(result)) result = args->lpVtbl->get_Pin(args, &pin);
    confirmation->kind = (unsigned)kind;
    if (SUCCEEDED(result) && (kind == DevicePairingKinds_ConfirmPinMatch || kind == DevicePairingKinds_DisplayPin)) {
        const wchar_t *code = WindowsGetStringRawBuffer(pin, NULL);
        (void)swprintf_s(message, _countof(message),
            kind == DevicePairingKinds_ConfirmPinMatch ?
                L"手机：%s\n\n配对码：%s\n\n请核对手机显示的配对码。两端相同才选择“是”，并在手机上确认。" :
                L"手机：%s\n\n请在手机上输入配对码：%s\n\n输入完成后选择“是”。",
            confirmation->name, code);
        confirmation->accepted = MessageBoxW(confirmation->parent, message, L"Cordonix · 蓝牙配对确认",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
        confirmation->confirmation_ms = GetTickCount64() - started;
        if (confirmation->accepted) {
            response_started = GetTickCount64();
            confirmation->response_sent = 1;
            result = args->lpVtbl->Accept(args);
        }
    } else if (SUCCEEDED(result)) result = E_NOTIMPL;
    confirmation->response = result;
    if (deferral) {
        HRESULT completed = deferral->lpVtbl->Complete(deferral);
        if (SUCCEEDED(confirmation->response)) confirmation->response = completed;
        deferral->lpVtbl->Release(deferral);
    }
    if (response_started) confirmation->response_ms = GetTickCount64() - response_started;
    WindowsDeleteString(pin);
    SetEvent(confirmation->finished);
    return S_OK;
}
static const pairing_handler_vtable confirmation_vtable = {
    confirmation_query, confirmation_addref, confirmation_release, confirmation_invoke
};

static HRESULT await_operation(IUnknown *operation, ULONGLONG deadline) {
    IAsyncInfo *info = NULL;
    AsyncStatus status = Started;
    unsigned steps;
    HRESULT result = operation->lpVtbl->QueryInterface(operation, &async_info_id, (void **)&info);
    if (FAILED(result)) return result;
    for (steps = 0; steps < 1201 && GetTickCount64() < deadline; ++steps) {
        result = info->lpVtbl->get_Status(info, &status);
        if (FAILED(result) || status != Started) break;
        Sleep(50);
    }
    if (SUCCEEDED(result)) {
        if (status == Started) { (void)info->lpVtbl->Cancel(info); result = HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
        else if (status == Canceled) result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        else if (status == Error) {
            HRESULT failure = E_FAIL;
            result = info->lpVtbl->get_ErrorCode(info, &failure);
            if (SUCCEEDED(result)) result = failure;
        }
    }
    info->lpVtbl->Release(info); return result;
}

DWORD cdx_phone_authenticate(const cdx_phone_request *request, const wchar_t *name,
                             int *submitted, cdx_json_output *diagnostics) {
    bluetooth_factory *factory = NULL;
    bluetooth_device *device = NULL;
    bluetooth_device2 *device2 = NULL;
    device_info *info = NULL;
    device_info2 *info2 = NULL;
    device_pairing *pairing = NULL;
    device_pairing2 *pairing2 = NULL;
    custom_pairing *custom = NULL;
    pairing_result *paired = NULL;
    __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CBluetoothDevice *lookup = NULL;
    __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDevicePairingResult *operation = NULL;
    enum __x_ABI_CWindows_CDevices_CEnumeration_CDevicePairingResultStatus status = DevicePairingResultStatus_Failed;
    enum __x_ABI_CWindows_CDevices_CEnumeration_CDevicePairingProtectionLevel protection = DevicePairingProtectionLevel_Default;
    pairing_confirmation *confirmation = NULL;
    EventRegistrationToken token = {0};
    HSTRING class_name = NULL;
    ULONGLONG deadline = GetTickCount64() + request->timeout_ms;
    int registered = 0;
    const char *stage = "initialize";
    DWORD error = ERROR_SUCCESS;
    HRESULT result;
    *submitted = 0;
    result = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(result)) return (DWORD)result;
    confirmation = calloc(1, sizeof(*confirmation));
    if (!confirmation) { result = E_OUTOFMEMORY; goto done; }
    confirmation->handler.lpVtbl = &confirmation_vtable; confirmation->references = 1;
    confirmation->parent = (HWND)request->parent_window;
    wcsncpy_s(confirmation->name, _countof(confirmation->name), name, _TRUNCATE);
    confirmation->finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!confirmation->finished) { result = HRESULT_FROM_WIN32(GetLastError()); goto done; }
    stage = "device_lookup";
    result = WindowsCreateString(L"Windows.Devices.Bluetooth.BluetoothDevice", 41, &class_name);
    if (SUCCEEDED(result)) result = RoGetActivationFactory(class_name, &bluetooth_factory_id, (void **)&factory);
    if (SUCCEEDED(result)) result = factory->lpVtbl->FromBluetoothAddressAsync(factory, request->address, &lookup);
    if (SUCCEEDED(result)) result = await_operation((IUnknown *)lookup, deadline);
    if (SUCCEEDED(result)) result = lookup->lpVtbl->GetResults(lookup, &device);
    if (SUCCEEDED(result) && !device) result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (FAILED(result)) goto done;
    stage = "pairing_interface";
    result = device->lpVtbl->QueryInterface(device, &bluetooth_device2_id, (void **)&device2);
    if (SUCCEEDED(result)) result = device2->lpVtbl->get_DeviceInformation(device2, &info);
    if (SUCCEEDED(result)) result = info->lpVtbl->QueryInterface(info, &device_info2_id, (void **)&info2);
    if (SUCCEEDED(result)) result = info2->lpVtbl->get_Pairing(info2, &pairing);
    if (SUCCEEDED(result)) result = pairing->lpVtbl->QueryInterface(pairing, &device_pairing2_id, (void **)&pairing2);
    if (SUCCEEDED(result)) result = pairing2->lpVtbl->get_Custom(pairing2, &custom);
    if (SUCCEEDED(result)) result = custom->lpVtbl->add_PairingRequested(custom, &confirmation->handler, &token);
    if (FAILED(result)) goto done;
    registered = 1; stage = "pairing";
    result = custom->lpVtbl->PairAsync(custom,
        DevicePairingKinds_ConfirmPinMatch | DevicePairingKinds_DisplayPin, &operation);
    if (FAILED(result)) goto done;
    *submitted = 1;
    result = await_operation((IUnknown *)operation, deadline);
    if (SUCCEEDED(result)) result = operation->lpVtbl->GetResults(operation, &paired);
    if (SUCCEEDED(result)) result = paired->lpVtbl->get_Status(paired, &status);
    if (SUCCEEDED(result)) result = paired->lpVtbl->get_ProtectionLevelUsed(paired, &protection);
done:
    if (registered) (void)custom->lpVtbl->remove_PairingRequested(custom, token);
    if (confirmation && InterlockedCompareExchange(&confirmation->entered, 0, 0)) {
        ULONGLONG now = GetTickCount64();
        if (WaitForSingleObject(confirmation->finished, now < deadline ? (DWORD)(deadline - now) : 0) != WAIT_OBJECT_0)
            ExitProcess(ERROR_TIMEOUT);
    }
    if (FAILED(result)) error = (DWORD)result;
    else if (status != DevicePairingResultStatus_Paired && status != DevicePairingResultStatus_AlreadyPaired)
        error = status == DevicePairingResultStatus_PairingCanceled || status == DevicePairingResultStatus_RejectedByHandler ?
            ERROR_CANCELLED : ERROR_NOT_AUTHENTICATED;
    cdx_json_raw(diagnostics, ",\"pairing_backend\":\"windows_device_pairing\",\"pairing_stage\":");
    cdx_json_quote(diagnostics, stage, strlen(stage));
    cdx_json_raw(diagnostics, ",\"pairing_result\":"); cdx_json_uint(diagnostics, (unsigned)status);
    cdx_json_raw(diagnostics, ",\"protection_level\":"); cdx_json_uint(diagnostics, (unsigned)protection);
    cdx_json_raw(diagnostics, ",\"authentication\":{\"callback_received\":");
    cdx_json_raw(diagnostics, confirmation && confirmation->entered ? "true" : "false");
    cdx_json_raw(diagnostics, ",\"method\":"); cdx_json_uint(diagnostics, confirmation ? confirmation->kind : 0);
    cdx_json_raw(diagnostics, ",\"user_confirmed\":"); cdx_json_raw(diagnostics, confirmation && confirmation->accepted ? "true" : "false");
    cdx_json_raw(diagnostics, ",\"response_sent\":"); cdx_json_raw(diagnostics, confirmation && confirmation->response_sent ? "true" : "false");
    cdx_json_raw(diagnostics, ",\"response_error\":"); cdx_json_uint(diagnostics, confirmation ? (DWORD)confirmation->response : 0);
    cdx_json_raw(diagnostics, ",\"confirmation_ms\":"); cdx_json_uint(diagnostics, confirmation ? confirmation->confirmation_ms : 0);
    cdx_json_raw(diagnostics, ",\"response_ms\":"); cdx_json_uint(diagnostics, confirmation ? confirmation->response_ms : 0);
    cdx_json_raw(diagnostics, "}");
    if (operation) operation->lpVtbl->Release(operation);
    if (paired) paired->lpVtbl->Release(paired);
    if (custom) custom->lpVtbl->Release(custom);
    if (pairing2) pairing2->lpVtbl->Release(pairing2);
    if (pairing) pairing->lpVtbl->Release(pairing);
    if (info2) info2->lpVtbl->Release(info2);
    if (info) info->lpVtbl->Release(info);
    if (device2) device2->lpVtbl->Release(device2);
    if (device) device->lpVtbl->Release(device);
    if (lookup) lookup->lpVtbl->Release(lookup);
    if (factory) factory->lpVtbl->Release(factory);
    if (confirmation) (void)confirmation_release(&confirmation->handler);
    WindowsDeleteString(class_name);
    RoUninitialize();
    return error;
}
