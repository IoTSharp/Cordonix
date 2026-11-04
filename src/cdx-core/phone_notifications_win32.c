#define WIN32_LEAN_AND_MEAN
#define CONST_VTABLE
#include "cdx/phone.h"
#include <roapi.h>
#include <winstring.h>
#include <windows.devices.bluetooth.h>
#include <windows.devices.enumeration.h>
#include <windows.devices.bluetooth.genericattributeprofile.h>
#include <windows.storage.streams.h>
#include <asyncinfo.h>
#include <robuffer.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "cfgmgr32.lib")

typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothLEDeviceStatics ble_factory;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothLEDevice ble_device;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothLEDevice2 ble_device2;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothLEDevice3 ble_device3;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattDeviceService gatt_service;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattDeviceService3 gatt_service3;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattSession gatt_session;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattSessionStatics gatt_session_factory;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattCharacteristic gatt_characteristic;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattDeviceServicesResult service_result;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattValueChangedEventArgs value_args;
typedef __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CBluetoothLEDevice ble_operation;
typedef __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattDeviceServicesResult service_operation;
typedef __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattSession gatt_session_operation;
typedef __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattCommunicationStatus communication_operation;
typedef __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattCharacteristicsResult characteristic_operation;
typedef __FIVectorView_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattDeviceService service_vector;
typedef __FIVectorView_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattCharacteristic characteristic_vector;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CIGattCharacteristicsResult characteristic_result;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothLEDevice4 ble_device4;
typedef __x_ABI_CWindows_CDevices_CBluetooth_CIBluetoothDeviceId bluetooth_device_id;
typedef __FITypedEventHandler_2_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattCharacteristic_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattValueChangedEventArgs value_handler;
typedef __FITypedEventHandler_2_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattCharacteristic_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattValueChangedEventArgsVtbl value_handler_vtable;
typedef __x_ABI_CWindows_CStorage_CStreams_CIBuffer buffer;

static const IID ble_factory_id = {0xc8cf1a19,0xf0b6,0x4bf0,{0x86,0x89,0x41,0x30,0x3d,0xe2,0xd9,0xf4}};
static const IID ble_device3_id = {0xaee9e493,0x44ac,0x40dc,{0xaf,0x33,0xb2,0xc1,0x3c,0x01,0xca,0x46}};
static const IID ble_device4_id = {0x2b605031,0x2248,0x4b2f,{0xac,0xf0,0x7c,0xee,0x36,0xfc,0x58,0x70}};
static const IID gatt_session_statics_id = {0x2e65b95c,0x539f,0x4db7,{0x82,0xa8,0x73,0xbd,0xbb,0xf7,0x3e,0xbf}};
static const IID buffer_factory_id = {0x71af914d,0xc10f,0x484b,{0xbc,0x50,0x14,0xbc,0x62,0x3b,0x3a,0x27}};
static const IID buffer_bytes_id = {0x905a0fef,0xbc53,0x11df,{0x8c,0x49,0x00,0x1e,0x4f,0xc6,0x86,0xda}};
static const IID async_info_id = {0x36,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID unknown_id = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID agile_id = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static const IID value_handler_id = {0xc1f420f6,0x6292,0x5760,{0xa2,0xc9,0x9d,0xdf,0x98,0x68,0x3c,0xfc}};
static const GUID ancs_service = {0x7905f431,0xb5ce,0x4e99,{0xa4,0x0f,0x4b,0x1e,0x12,0x2d,0x00,0xd0}};
static const GUID ancs_source = {0x9fbf120d,0x6301,0x42d9,{0x8c,0x58,0x25,0xe6,0x99,0xa2,0x1d,0xbd}};
static const GUID ancs_control = {0x69d1d8f3,0x45e1,0x49a8,{0x98,0x21,0x9b,0xbd,0xfd,0xaa,0xd9,0xd9}};
static const GUID ancs_data = {0x22eac6e9,0x24d6,0x4bb5,{0xbe,0x44,0xb3,0x6a,0xce,0x7c,0x7b,0xfb}};

typedef struct buffer_bytes_vtable {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *Buffer)(void *, BYTE **);
} buffer_bytes_vtable;
typedef struct buffer_bytes_object { const buffer_bytes_vtable *lpVtbl; } buffer_bytes_object;

typedef struct notification_handler {
    value_handler handler;
    LONG references;
    HANDLE event;
    SRWLOCK lock;
    BYTE bytes[4096];
    UINT32 size;
    int overflow;
    int drop_oldest;
    UINT32 dropped_bytes;
} notification_handler;

static ULONG STDMETHODCALLTYPE handler_addref(value_handler *self) {
    return (ULONG)InterlockedIncrement(&((notification_handler *)self)->references);
}

static ULONG STDMETHODCALLTYPE handler_release(value_handler *self) {
    notification_handler *handler = (notification_handler *)self;
    ULONG references = (ULONG)InterlockedDecrement(&handler->references);
    if (!references) { CloseHandle(handler->event); free(handler); }
    return references;
}

static HRESULT STDMETHODCALLTYPE handler_query(value_handler *self, REFIID id, void **object) {
    if (!object) return E_POINTER;
    *object = NULL;
    if (memcmp(id, &unknown_id, sizeof(*id)) && memcmp(id, &agile_id, sizeof(*id)) &&
        memcmp(id, &value_handler_id, sizeof(*id))) return E_NOINTERFACE;
    *object = self;
    (void)handler_addref(self);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE handler_invoke(value_handler *self, gatt_characteristic *sender, value_args *args) {
    notification_handler *handler = (notification_handler *)self;
    buffer *value = NULL;
    buffer_bytes_object *bytes = NULL;
    BYTE *data = NULL;
    UINT32 length = 0;
    (void)sender;
    if (FAILED(args->lpVtbl->get_CharacteristicValue(args, &value)) || !value) return S_OK;
    if (SUCCEEDED(value->lpVtbl->get_Length(value, &length)) &&
        SUCCEEDED(value->lpVtbl->QueryInterface(value, &buffer_bytes_id, (void **)&bytes)) &&
        SUCCEEDED(bytes->lpVtbl->Buffer(bytes, &data)) && data) {
        AcquireSRWLockExclusive(&handler->lock);
        if (length > sizeof(handler->bytes) - handler->size) {
            UINT32 discard;
            if (!handler->drop_oldest) handler->overflow = 1;
            else {
                if (length >= sizeof(handler->bytes)) {
                    discard = handler->size + length - (UINT32)sizeof(handler->bytes);
                    data += length - sizeof(handler->bytes); length = (UINT32)sizeof(handler->bytes);
                    handler->size = 0;
                } else {
                    discard = length - ((UINT32)sizeof(handler->bytes) - handler->size);
                    memmove(handler->bytes, handler->bytes + discard, handler->size - discard);
                    handler->size -= discard;
                }
                handler->dropped_bytes += discard;
                memcpy(handler->bytes + handler->size, data, length); handler->size += length;
            }
        } else { memcpy(handler->bytes + handler->size, data, length); handler->size += length; }
        SetEvent(handler->event);
        ReleaseSRWLockExclusive(&handler->lock);
    }
    if (bytes) bytes->lpVtbl->Release(bytes);
    value->lpVtbl->Release(value);
    return S_OK;
}

static const value_handler_vtable handler_vtable = {handler_query, handler_addref, handler_release, handler_invoke};

static int cancelled(cdx_cancel_handle cancel) {
    return cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
}

static HRESULT resolve_ble_address(const cdx_phone_request *request, cdx_cancel_handle cancel,
                                   ULONGLONG deadline, UINT64 *address) {
    static const DEVPROPKEY container_key = {{0x8c7ed206,0x3f8a,0x4827,{0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c}},2};
    wchar_t *ids = calloc(32768, sizeof(wchar_t));
    wchar_t target[64];
    GUID container = {0};
    int found = 0;
    unsigned pass;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (!ids) return E_OUTOFMEMORY;
    (void)swprintf_s(target, _countof(target), L"BTHENUM\\DEV_%012llX\\", request->address);
    /* Windows groups the classic and private LE identities in one device container. */
    for (pass = 0; pass < 2; ++pass) {
        CONFIGRET cr = CM_Get_Device_ID_ListW(pass ? L"BTHLE" : L"BTHENUM", ids, 32768,
            CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT);
        wchar_t *id;
        unsigned entries = 0;
        if (cr != CR_SUCCESS) { hr = HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_GEN_FAILURE)); break; }
        for (id = ids; *id && entries++ < 256; id += wcslen(id) + 1) {
            DEVINST node;
            GUID value;
            DEVPROPTYPE type = 0;
            ULONG size = sizeof(value);
            if (cancelled(cancel)) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
            if (GetTickCount64() >= deadline) { hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT); goto done; }
            if (!pass && _wcsnicmp(id, target, wcslen(target))) continue;
            if (pass && (_wcsnicmp(id, L"BTHLE\\DEV_", 10) || wcslen(id) < 23 || id[22] != L'\\')) continue;
            if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
                CM_Get_DevNode_PropertyW(node, &container_key, &type, (BYTE *)&value, &size, 0) != CR_SUCCESS ||
                type != DEVPROP_TYPE_GUID || size != sizeof(value)) continue;
            if (!pass) { container = value; found = 1; break; }
            if (!memcmp(&container, &value, sizeof(value))) {
                wchar_t hex[13], *end;
                UINT64 parsed;
                memcpy(hex, id + 10, 12 * sizeof(wchar_t)); hex[12] = 0;
                parsed = _wcstoui64(hex, &end, 16);
                if (end != hex + 12) { hr = E_INVALIDARG; goto done; }
                *address = parsed; hr = S_OK; goto done;
            }
        }
        if (!found) break;
    }
done:
    free(ids);
    return hr;
}

static HRESULT await_operation(IUnknown *operation, ULONGLONG deadline, cdx_cancel_handle cancel) {
    IAsyncInfo *info = NULL;
    AsyncStatus status = Started;
    HRESULT result = operation->lpVtbl->QueryInterface(operation, &async_info_id, (void **)&info);
    unsigned steps;
    if (FAILED(result)) return result;
    for (steps = 0; steps < 2401 && GetTickCount64() < deadline && !cancelled(cancel); ++steps) {
        result = info->lpVtbl->get_Status(info, &status);
        if (FAILED(result) || status != Started) break;
        if (cancel) (void)WaitForSingleObject(cancel, 25); else Sleep(25);
    }
    if (SUCCEEDED(result)) {
        if (cancelled(cancel)) { (void)info->lpVtbl->Cancel(info); result = HRESULT_FROM_WIN32(ERROR_CANCELLED); }
        else if (status == Started) { (void)info->lpVtbl->Cancel(info); result = HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
        else if (status == Canceled) result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        else if (status == Error) {
            HRESULT failure = E_FAIL;
            result = info->lpVtbl->get_ErrorCode(info, &failure);
            if (SUCCEEDED(result)) result = failure;
        }
    }
    info->lpVtbl->Release(info);
    return result;
}

static HRESULT create_buffer(const BYTE *data, UINT32 size, buffer **result) {
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory *factory = NULL;
    HSTRING class_name = NULL;
    buffer_bytes_object *bytes = NULL;
    BYTE *target = NULL;
    HRESULT hr;
    *result = NULL;
    hr = WindowsCreateString(L"Windows.Storage.Streams.Buffer", 30, &class_name);
    _Analysis_assume_(FAILED(hr) || class_name != NULL);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(class_name, &buffer_factory_id, (void **)&factory);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->Create(factory, size, result);
    if (SUCCEEDED(hr)) hr = (*result)->lpVtbl->put_Length(*result, size);
    if (SUCCEEDED(hr)) hr = (*result)->lpVtbl->QueryInterface(*result, &buffer_bytes_id, (void **)&bytes);
    if (SUCCEEDED(hr)) hr = bytes->lpVtbl->Buffer(bytes, &target);
    if (SUCCEEDED(hr) && size) memcpy(target, data, size);
    if (bytes) bytes->lpVtbl->Release(bytes);
    if (FAILED(hr) && *result) { (*result)->lpVtbl->Release(*result); *result = NULL; }
    if (factory) factory->lpVtbl->Release(factory);
    WindowsDeleteString(class_name);
    return hr;
}

static notification_handler *create_handler(int drop_oldest) {
    notification_handler *handler = calloc(1, sizeof(*handler));
    if (!handler) return NULL;
    handler->handler.lpVtbl = &handler_vtable; handler->references = 1;
    handler->drop_oldest = drop_oldest;
    InitializeSRWLock(&handler->lock);
    handler->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!handler->event) { free(handler); return NULL; }
    return handler;
}

static HRESULT read_handler(notification_handler *handler, BYTE *data, UINT32 capacity, UINT32 *size,
                            ULONGLONG deadline, cdx_cancel_handle cancel) {
    unsigned steps;
    *size = 0;
    for (steps = 0; steps < 2401 && GetTickCount64() < deadline; ++steps) {
        DWORD wait;
        if (cancelled(cancel)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        wait = WaitForSingleObject(handler->event, 25);
        if (wait == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
        if (wait == WAIT_OBJECT_0) {
            HRESULT hr = S_OK;
            AcquireSRWLockExclusive(&handler->lock);
            if (handler->overflow) hr = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            else {
                *size = handler->size < capacity ? handler->size : capacity;
                memcpy(data, handler->bytes, *size);
                handler->size -= *size;
                memmove(handler->bytes, handler->bytes + *size, handler->size);
                if (!handler->size) ResetEvent(handler->event);
            }
            ReleaseSRWLockExclusive(&handler->lock);
            return hr;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

static HRESULT communication_result(enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattCommunicationStatus status) {
    if (status == GattCommunicationStatus_Success) return S_OK;
    if (status == GattCommunicationStatus_AccessDenied) return E_ACCESSDENIED;
    if (status == GattCommunicationStatus_Unreachable) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

enum { ANCS_MAX_CONNECTION_RETRIES = 5, ANCS_CONNECTION_BACKOFF_MS = 500 };
typedef HRESULT (*ancs_discovery_attempt)(void *, ULONGLONG, cdx_cancel_handle);
typedef struct ancs_discovery_context {
    ble_device3 *device;
    service_result *result;
} ancs_discovery_context;

static HRESULT discover_ancs_once(void *context, ULONGLONG deadline, cdx_cancel_handle cancel) {
    ancs_discovery_context *discovery = context;
    service_operation *operation = NULL;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattCommunicationStatus status =
        GattCommunicationStatus_Unreachable;
    HRESULT hr;
    if (discovery->result) { discovery->result->lpVtbl->Release(discovery->result); discovery->result = NULL; }
    hr = discovery->device->lpVtbl->GetGattServicesForUuidWithCacheModeAsync(
        discovery->device, ancs_service, BluetoothCacheMode_Uncached, &operation);
    if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)operation, deadline, cancel);
    if (SUCCEEDED(hr)) hr = operation->lpVtbl->GetResults(operation, &discovery->result);
    if (SUCCEEDED(hr)) hr = discovery->result->lpVtbl->get_Status(discovery->result, &status);
    if (SUCCEEDED(hr)) hr = communication_result(status);
    if (operation) operation->lpVtbl->Release(operation);
    if (FAILED(hr) && discovery->result) {
        discovery->result->lpVtbl->Release(discovery->result); discovery->result = NULL;
    }
    return hr;
}

static HRESULT discover_ancs_services(ancs_discovery_attempt attempt, void *context, ULONGLONG deadline,
                                      cdx_cancel_handle cancel, unsigned *attempts, int *exhausted) {
    unsigned index;
    HRESULT hr = S_OK;
    *attempts = 0; *exhausted = 0;
    for (index = 0; index <= ANCS_MAX_CONNECTION_RETRIES; ++index) {
        ULONGLONG now;
        DWORD delay, wait;
        if (cancelled(cancel)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (GetTickCount64() >= deadline) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        ++*attempts;
        hr = attempt(context, deadline, cancel);
        if (hr != HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)) return hr;
        if (index == ANCS_MAX_CONNECTION_RETRIES) { *exhausted = 1; return hr; }
        if (cancelled(cancel)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        now = GetTickCount64();
        if (now >= deadline) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        delay = deadline - now < ANCS_CONNECTION_BACKOFF_MS ? (DWORD)(deadline - now) : ANCS_CONNECTION_BACKOFF_MS;
        if (cancel) {
            wait = WaitForSingleObject(cancel, delay);
            if (wait == WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (wait == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
        } else Sleep(delay);
    }
    return hr;
}

#ifdef CDX_PHONE_TEST
typedef struct ancs_discovery_fixture {
    const HRESULT *results;
    unsigned count, next;
    HANDLE attempted;
} ancs_discovery_fixture;

static HRESULT fixture_discovery_attempt(void *context, ULONGLONG deadline, cdx_cancel_handle cancel) {
    ancs_discovery_fixture *fixture = context;
    HRESULT hr = fixture->next < fixture->count ? fixture->results[fixture->next] : E_UNEXPECTED;
    (void)deadline; (void)cancel;
    ++fixture->next;
    if (fixture->attempted) SetEvent(fixture->attempted);
    return hr;
}

HRESULT cdx_phone_test_ancs_discovery(const HRESULT *results, unsigned count, unsigned timeout_ms,
                                    HANDLE cancel, HANDLE attempted, unsigned *attempts, int *exhausted) {
    ancs_discovery_fixture fixture = {results, count, 0, attempted};
    return discover_ancs_services(fixture_discovery_attempt, &fixture, GetTickCount64() + timeout_ms,
                                  cancel, attempts, exhausted);
}
#endif

static HRESULT get_characteristic(gatt_service *service, const GUID *uuid, ULONGLONG deadline,
                                  cdx_cancel_handle cancel, gatt_characteristic **result) {
    static const IID service3_id = {0xb293a950,0x0c53,0x437c,{0xa9,0xb3,0x5c,0x32,0x10,0xc6,0xe5,0x69}};
    gatt_service3 *service3 = NULL;
    characteristic_operation *operation = NULL;
    characteristic_result *characteristics_result = NULL;
    characteristic_vector *items = NULL;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattCommunicationStatus status =
        GattCommunicationStatus_Unreachable;
    UINT32 count = 0;
    HRESULT hr = service->lpVtbl->QueryInterface(service, &service3_id, (void **)&service3);
    *result = NULL;
    if (SUCCEEDED(hr)) hr = service3->lpVtbl->GetCharacteristicsForUuidWithCacheModeAsync(
        service3, *uuid, BluetoothCacheMode_Uncached, &operation);
    if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)operation, deadline, cancel);
    if (SUCCEEDED(hr)) hr = operation->lpVtbl->GetResults(operation, &characteristics_result);
    if (SUCCEEDED(hr) && !characteristics_result) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = characteristics_result->lpVtbl->get_Status(characteristics_result, &status);
    if (SUCCEEDED(hr)) hr = communication_result(status);
    if (SUCCEEDED(hr)) hr = characteristics_result->lpVtbl->get_Characteristics(characteristics_result, &items);
    if (SUCCEEDED(hr)) hr = items->lpVtbl->get_Size(items, &count);
    if (SUCCEEDED(hr) && count) hr = items->lpVtbl->GetAt(items, 0, result);
    if (items) items->lpVtbl->Release(items);
    if (characteristics_result) characteristics_result->lpVtbl->Release(characteristics_result);
    if (operation) operation->lpVtbl->Release(operation);
    if (service3) service3->lpVtbl->Release(service3);
    return SUCCEEDED(hr) && *result ? S_OK : FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT subscribe(gatt_characteristic *characteristic, notification_handler *handler, EventRegistrationToken *token,
                         ULONGLONG deadline, cdx_cancel_handle cancel) {
    HRESULT hr = characteristic->lpVtbl->add_ValueChanged(characteristic, &handler->handler, token);
    int registered = SUCCEEDED(hr);
    communication_operation *operation = NULL;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattCommunicationStatus status =
        GattCommunicationStatus_Unreachable;
    if (SUCCEEDED(hr)) hr = characteristic->lpVtbl->WriteClientCharacteristicConfigurationDescriptorAsync(
        characteristic, GattClientCharacteristicConfigurationDescriptorValue_Notify, &operation);
    if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)operation, deadline, cancel);
    if (SUCCEEDED(hr)) hr = operation->lpVtbl->GetResults(operation, &status);
    if (operation) operation->lpVtbl->Release(operation);
    if (FAILED(hr) || status != GattCommunicationStatus_Success) {
        if (SUCCEEDED(hr)) hr = communication_result(status);
        if (registered) (void)characteristic->lpVtbl->remove_ValueChanged(characteristic, *token);
    }
    return hr;
}

static void append_attribute(cdx_json_output *out, const char *key, const char *value) {
    cdx_json_raw(out, ","); cdx_json_quote(out, key, strlen(key)); cdx_json_raw(out, ":");
    cdx_json_quote(out, value, strlen(value));
}

static void emit_event(cdx_json_output *out, unsigned uid, const BYTE source[8], const cdx_phone_ancs_attributes *attributes) {
    cdx_json_raw(out, "{\"uid\":"); cdx_json_uint(out, uid);
    cdx_json_raw(out, ",\"category\":"); cdx_json_uint(out, source[2]);
    cdx_json_raw(out, ",\"event_id\":"); cdx_json_uint(out, source[0]);
    cdx_json_raw(out, ",\"flags\":"); cdx_json_uint(out, source[1]);
    cdx_json_raw(out, ",\"preexisting\":"); cdx_json_raw(out, source[1] & 4 ? "true" : "false");
    if (source[0] != 2) {
        append_attribute(out, "app", attributes->app); append_attribute(out, "title", attributes->title);
        append_attribute(out, "message", attributes->message);
    }
    cdx_json_raw(out, ",\"source\":\"ancs\"}");
}

static HRESULT read_attributes(gatt_characteristic *control, notification_handler *handler, uint32_t uid,
                               ULONGLONG deadline, cdx_cancel_handle cancel, cdx_phone_ancs_attributes *attributes) {
    BYTE command[12], packet[4096];
    UINT32 size = 0;
    buffer *payload = NULL;
    communication_operation *operation = NULL;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattCommunicationStatus status = GattCommunicationStatus_Unreachable;
    HRESULT hr;
    unsigned step;
    cdx_phone_ancs_command(uid, command);
    hr = create_buffer(command, sizeof(command), &payload);
    if (SUCCEEDED(hr)) hr = control->lpVtbl->WriteValueAsync(control, payload, &operation);
    if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)operation, deadline, cancel);
    if (SUCCEEDED(hr)) hr = operation->lpVtbl->GetResults(operation, &status);
    if (SUCCEEDED(hr)) hr = communication_result(status);
    if (operation) operation->lpVtbl->Release(operation);
    if (payload) payload->lpVtbl->Release(payload);
    if (FAILED(hr)) return hr;
    for (step = 0; step < sizeof(packet) && size < sizeof(packet); ++step) {
        UINT32 received;
        int parsed;
        hr = read_handler(handler, packet + size, (UINT32)sizeof(packet) - size, &received, deadline, cancel);
        if (FAILED(hr)) return hr;
        size += received;
        parsed = cdx_phone_ancs_parse(packet, size, uid, attributes);
        if (parsed == 1) return S_OK;
        if (parsed == -1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
}

static HRESULT collect_notifications(notification_handler *source, gatt_characteristic *control,
                                     notification_handler *data, unsigned max_entries, ULONGLONG deadline,
                                     cdx_cancel_handle cancel, cdx_json_output *events, unsigned *count,
                                     unsigned *source_dropped, unsigned *preexisting_discarded,
                                     const char **stage) {
    HRESULT hr = S_OK;
    unsigned preexisting_kept = 0;
    unsigned preexisting_limit = max_entries > 1 ? max_entries - 1 : 0;
    *count = 0;
    *source_dropped = source->dropped_bytes / 8;
    *preexisting_discarded = 0;
    cdx_json_raw(events, "[");
    for (;;) {
        BYTE packet[8];
        UINT32 size = 0;
        unsigned uid;
        int preexisting;
        int keep;
        cdx_phone_ancs_attributes attributes = {0};
        *stage = "notification_wait";
        hr = read_handler(source, packet, sizeof(packet), &size, deadline, cancel);
        if (hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT)) { hr = S_OK; break; }
        if (FAILED(hr)) break;
        if (size != sizeof(packet) || packet[0] > 2) { hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); break; }
        uid = (unsigned)packet[4] | ((unsigned)packet[5] << 8) | ((unsigned)packet[6] << 16) | ((unsigned)packet[7] << 24);
        preexisting = (packet[1] & 4) != 0;
        if (!preexisting && *count >= max_entries) break;
        keep = !preexisting || preexisting_kept < preexisting_limit;
        if (!keep) { ++*preexisting_discarded; continue; }
        if (packet[0] != 2) {
            *stage = "notification_attributes";
            hr = read_attributes(control, data, uid, deadline, cancel, &attributes);
            if (FAILED(hr)) break;
        }
        if (*count) cdx_json_raw(events, ",");
        emit_event(events, uid, packet, &attributes);
        if (events->failed) { hr = E_OUTOFMEMORY; break; }
        ++*count;
        if (preexisting) ++preexisting_kept;
        if (!preexisting && *count >= max_entries) break;
    }
    cdx_json_raw(events, "]");
    return events->failed ? E_OUTOFMEMORY : hr;
}

#ifdef CDX_PHONE_TEST
HRESULT cdx_phone_test_notifications(const unsigned char *bytes, unsigned size, unsigned max_entries,
                                    unsigned timeout_ms, HANDLE cancel, cdx_json_output *events, unsigned *count) {
    notification_handler *source = create_handler(1);
    const char *stage = NULL;
    unsigned source_dropped, preexisting_discarded;
    HRESULT hr;
    if (!source) return E_OUTOFMEMORY;
    /* Removal-event fixtures exercise the real FIFO and collector without GATT attribute reads. */
    if (size > sizeof(source->bytes)) {
        source->dropped_bytes = size - (UINT32)sizeof(source->bytes);
        memcpy(source->bytes, bytes + size - sizeof(source->bytes), sizeof(source->bytes));
        source->size = sizeof(source->bytes);
    } else { memcpy(source->bytes, bytes, size); source->size = size; }
    if (size) SetEvent(source->event);
    hr = collect_notifications(source, NULL, NULL, max_entries, GetTickCount64() + timeout_ms,
                               cancel, events, count, &source_dropped, &preexisting_discarded, &stage);
    (void)handler_release(&source->handler);
    return hr;
}
#endif

static void close_object(IUnknown *object) {
    static const IID closable_id = {0x30d5a829,0x7fa4,0x4026,{0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e}};
    __x_ABI_CWindows_CFoundation_CIClosable *closable = NULL;
    if (object && SUCCEEDED(object->lpVtbl->QueryInterface(object, &closable_id, (void **)&closable))) {
        (void)closable->lpVtbl->Close(closable); closable->lpVtbl->Release(closable);
    }
}

void cdx_phone_app_notifications_platform(const cdx_phone_request *request, cdx_cancel_handle cancel, cdx_json_output *out) {
    ble_factory *factory = NULL;
    ble_device *device = NULL;
    ble_device2 *device2 = NULL;
    ble_device4 *device4 = NULL;
    bluetooth_device_id *bluetooth_id = NULL;
    __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformation *device_info = NULL;
    __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformation2 *device_info2 = NULL;
    __x_ABI_CWindows_CDevices_CEnumeration_CIDeviceInformationPairing *pairing = NULL;
    ancs_discovery_context discovery = {0};
    service_vector *services = NULL;
    gatt_service *service = NULL;
    gatt_service3 *service3 = NULL;
    gatt_session *session = NULL;
    gatt_session_factory *session_factory = NULL;
    gatt_session_operation *session_lookup = NULL;
    gatt_session *device_session = NULL;
    __FIAsyncOperation_1_Windows__CDevices__CBluetooth__CGenericAttributeProfile__CGattOpenStatus *open_operation = NULL;
    gatt_characteristic *source = NULL, *control = NULL, *data = NULL;
    ble_operation *lookup = NULL;
    notification_handler *source_handler = NULL, *data_handler = NULL;
    EventRegistrationToken source_token = {0}, data_token = {0};
    HSTRING class_name = NULL;
    HSTRING session_class_name = NULL;
    HRESULT hr = S_OK;
    const char *stage = "runtime";
    UINT64 ble_address = 0;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CBluetoothAddressType address_type = BluetoothAddressType_Unspecified;
    enum __x_ABI_CWindows_CDevices_CBluetooth_CBluetoothConnectionStatus connection_status = BluetoothConnectionStatus_Disconnected;
    boolean paired = FALSE;
    ULONGLONG deadline = GetTickCount64() + request->timeout_ms;
    int source_registered = 0, data_registered = 0;
    int metadata_available = 0;
    unsigned notification_count = 0;
    unsigned source_dropped = 0, preexisting_discarded = 0;
    unsigned connection_attempts = 0;
    int connection_retry_exhausted = 0;
    cdx_json_output events;
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) { cdx_phone_error(out, "ble_unavailable", 0); return; }
    cdx_json_output_init(&events);
    if (cancelled(cancel)) { cdx_phone_error(out, "cancelled", 0); goto done; }
    stage = "ble_identity_resolution";
    hr = resolve_ble_address(request, cancel, deadline, &ble_address);
    if (SUCCEEDED(hr)) stage = "ble_device_lookup";
    if (SUCCEEDED(hr)) hr = WindowsCreateString(L"Windows.Devices.Bluetooth.BluetoothLEDevice", 43, &class_name);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(class_name, &ble_factory_id, (void **)&factory);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->FromBluetoothAddressAsync(factory, ble_address, &lookup);
    if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)lookup, deadline, cancel);
    if (SUCCEEDED(hr)) hr = lookup->lpVtbl->GetResults(lookup, &device);
    if (SUCCEEDED(hr) && !device) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        static const IID device2_id = {0x26f062b3,0x7aee,0x4d31,{0xba,0xba,0xb1,0xb9,0x77,0x5f,0x59,0x16}};
        static const IID info2_id = {0xf156a638,0x7997,0x48d9,{0xa1,0x0c,0x26,0x9d,0x46,0x53,0x3f,0x48}};
        hr = device->lpVtbl->QueryInterface(device, &device2_id, (void **)&device2);
        if (SUCCEEDED(hr)) hr = device2->lpVtbl->get_BluetoothAddressType(device2, &address_type);
        if (SUCCEEDED(hr)) hr = device2->lpVtbl->get_DeviceInformation(device2, &device_info);
        if (SUCCEEDED(hr)) hr = device_info->lpVtbl->QueryInterface(device_info, &info2_id, (void **)&device_info2);
        if (SUCCEEDED(hr)) hr = device_info2->lpVtbl->get_Pairing(device_info2, &pairing);
        if (SUCCEEDED(hr)) hr = pairing->lpVtbl->get_IsPaired(pairing, &paired);
        if (SUCCEEDED(hr)) hr = device->lpVtbl->get_ConnectionStatus(device, &connection_status);
        metadata_available = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        stage = "ble_session_open";
        hr = device->lpVtbl->QueryInterface(device, &ble_device4_id, (void **)&device4);
        if (SUCCEEDED(hr)) hr = device4->lpVtbl->get_BluetoothDeviceId(device4, &bluetooth_id);
        if (SUCCEEDED(hr)) hr = WindowsCreateString(
            L"Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession",
            (UINT32)(_countof(L"Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession") - 1),
            &session_class_name);
        if (SUCCEEDED(hr)) hr = RoGetActivationFactory(
            session_class_name, &gatt_session_statics_id, (void **)&session_factory);
        if (SUCCEEDED(hr)) hr = session_factory->lpVtbl->FromDeviceIdAsync(
            session_factory, bluetooth_id, &session_lookup);
        if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)session_lookup, deadline, cancel);
        if (SUCCEEDED(hr)) hr = session_lookup->lpVtbl->GetResults(session_lookup, &device_session);
        if (SUCCEEDED(hr)) hr = device_session->lpVtbl->put_MaintainConnection(device_session, TRUE);
    }
    if (SUCCEEDED(hr)) stage = "ancs_service_discovery";
    if (SUCCEEDED(hr)) hr = device->lpVtbl->QueryInterface(device, &ble_device3_id, (void **)&discovery.device);
    if (SUCCEEDED(hr)) hr = discover_ancs_services(discover_ancs_once, &discovery, deadline, cancel,
                                                  &connection_attempts, &connection_retry_exhausted);
    if (SUCCEEDED(hr)) hr = discovery.result->lpVtbl->get_Services(discovery.result, &services);
    if (SUCCEEDED(hr)) {
        UINT32 count = 0;
        hr = services->lpVtbl->get_Size(services, &count);
        if (SUCCEEDED(hr) && count) hr = services->lpVtbl->GetAt(services, 0, &service);
        if (SUCCEEDED(hr) && !service) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    if (SUCCEEDED(hr)) {
        static const IID service3_id = {0xb293a950,0x0c53,0x437c,{0xa9,0xb3,0x5c,0x32,0x10,0xc6,0xe5,0x69}};
        enum __x_ABI_CWindows_CDevices_CBluetooth_CGenericAttributeProfile_CGattOpenStatus open_status = GattOpenStatus_Unspecified;
        stage = "ancs_service_open";
        hr = service->lpVtbl->QueryInterface(service, &service3_id, (void **)&service3);
        if (SUCCEEDED(hr)) hr = service3->lpVtbl->OpenAsync(service3, GattSharingMode_SharedReadAndWrite, &open_operation);
        if (SUCCEEDED(hr)) hr = await_operation((IUnknown *)open_operation, deadline, cancel);
        if (SUCCEEDED(hr)) hr = open_operation->lpVtbl->GetResults(open_operation, &open_status);
        if (SUCCEEDED(hr) && open_status != GattOpenStatus_Success && open_status != GattOpenStatus_AlreadyOpened)
            hr = open_status == GattOpenStatus_AccessDenied ? E_ACCESSDENIED : HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (SUCCEEDED(hr)) hr = service3->lpVtbl->get_Session(service3, &session);
        if (SUCCEEDED(hr)) hr = session->lpVtbl->put_MaintainConnection(session, TRUE);
    }
    if (SUCCEEDED(hr)) stage = "ancs_characteristics";
    if (SUCCEEDED(hr)) hr = get_characteristic(service, &ancs_source, deadline, cancel, &source);
    if (SUCCEEDED(hr)) hr = get_characteristic(service, &ancs_control, deadline, cancel, &control);
    if (SUCCEEDED(hr)) hr = get_characteristic(service, &ancs_data, deadline, cancel, &data);
    if (SUCCEEDED(hr)) {
        source_handler = create_handler(1); data_handler = create_handler(0);
        if (!source_handler || !data_handler) hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) stage = "data_subscription";
    if (SUCCEEDED(hr)) hr = subscribe(data, data_handler, &data_token, deadline, cancel);
    if (SUCCEEDED(hr)) data_registered = 1;
    if (SUCCEEDED(hr)) stage = "notification_subscription";
    if (SUCCEEDED(hr)) hr = subscribe(source, source_handler, &source_token, deadline, cancel);
    if (SUCCEEDED(hr)) source_registered = 1;
    if (SUCCEEDED(hr)) hr = collect_notifications(source_handler, control, data_handler, request->max_entries,
        deadline, cancel, &events, &notification_count, &source_dropped, &preexisting_discarded, &stage);
    if (SUCCEEDED(hr)) {
        cdx_json_raw(out, "{\"status\":\"ok\",\"service\":\"ancs\",\"notification_access\":\"granted\",\"source_subscribed\":true,\"data_subscribed\":true");
        append_attribute(out, "wait_status", notification_count ? "event" : "timed_out");
        append_attribute(out, "collection_status", notification_count == request->max_entries ? "max_entries" : "timed_out");
    } else {
        const char *status = cancelled(cancel) || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? "cancelled" : hr == E_ACCESSDENIED ? "permission_denied" :
            hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? "ancs_unavailable" :
            hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT) ? "timed_out" :
            hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) ? "connection_failed" :
            hr == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW) ? "output_limit" : "ble_protocol_error";
        cdx_json_raw(out, "{\"status\":"); cdx_json_quote(out, status, strlen(status));
        append_attribute(out, "reason", status); append_attribute(out, "stage", stage);
        cdx_json_raw(out, ",\"hresult\":"); cdx_json_uint(out, (unsigned long)hr);
        cdx_json_raw(out, ",\"ble_address_type\":");
        if (metadata_available) cdx_json_uint(out, (unsigned)address_type); else cdx_json_raw(out, "null");
        cdx_json_raw(out, ",\"ble_paired\":"); cdx_json_raw(out, !metadata_available ? "null" : paired ? "true" : "false");
        cdx_json_raw(out, ",\"ble_connected_at_lookup\":");
        cdx_json_raw(out, !metadata_available ? "null" : connection_status == BluetoothConnectionStatus_Connected ? "true" : "false");
        cdx_json_raw(out, ",\"submitted\":false,\"audio_available\":false");
    }
    cdx_json_raw(out, ",\"connection_attempts\":"); cdx_json_uint(out, connection_attempts);
    cdx_json_raw(out, ",\"connection_retries\":"); cdx_json_uint(out, connection_attempts ? connection_attempts - 1 : 0);
    cdx_json_raw(out, ",\"max_connection_retries\":"); cdx_json_uint(out, ANCS_MAX_CONNECTION_RETRIES);
    cdx_json_raw(out, ",\"connection_retry_exhausted\":"); cdx_json_raw(out, connection_retry_exhausted ? "true" : "false");
    cdx_json_raw(out, ",\"auto_retry\":"); cdx_json_raw(out, connection_attempts > 1 ? "true" : "false");
    cdx_json_raw(out, ",\"partial\":"); cdx_json_raw(out, FAILED(hr) && notification_count ? "true" : "false");
    cdx_json_raw(out, ",\"notification_count\":"); cdx_json_uint(out, notification_count);
    cdx_json_raw(out, ",\"source_events_dropped\":"); cdx_json_uint(out, source_dropped);
    cdx_json_raw(out, ",\"preexisting_events_discarded\":"); cdx_json_uint(out, preexisting_discarded);
    cdx_json_raw(out, ",\"notifications\":");
    if (events.data && !events.failed) cdx_json_rawn(out, events.data, events.size); else cdx_json_raw(out, "[]");
    cdx_json_raw(out, "}");
    if (events.failed) out->failed = 1;
done:
    cdx_json_output_free(&events);
    if (data_registered) (void)data->lpVtbl->remove_ValueChanged(data, data_token);
    if (source_registered) (void)source->lpVtbl->remove_ValueChanged(source, source_token);
    if (data_handler) (void)handler_release(&data_handler->handler);
    if (source_handler) (void)handler_release(&source_handler->handler);
    if (data) data->lpVtbl->Release(data); if (control) control->lpVtbl->Release(control); if (source) source->lpVtbl->Release(source);
    if (session) { (void)session->lpVtbl->put_MaintainConnection(session, FALSE); session->lpVtbl->Release(session); }
    if (open_operation) open_operation->lpVtbl->Release(open_operation);
    if (service3) service3->lpVtbl->Release(service3);
    close_object((IUnknown *)service); close_object((IUnknown *)device);
    if (service) service->lpVtbl->Release(service); if (services) services->lpVtbl->Release(services);
    if (discovery.result) discovery.result->lpVtbl->Release(discovery.result);
    if (pairing) pairing->lpVtbl->Release(pairing); if (device_info2) device_info2->lpVtbl->Release(device_info2);
    if (device_info) device_info->lpVtbl->Release(device_info); if (device2) device2->lpVtbl->Release(device2);
    if (device_session) { (void)device_session->lpVtbl->put_MaintainConnection(device_session, FALSE); device_session->lpVtbl->Release(device_session); }
    if (session_lookup) session_lookup->lpVtbl->Release(session_lookup);
    if (session_factory) session_factory->lpVtbl->Release(session_factory);
    if (bluetooth_id) bluetooth_id->lpVtbl->Release(bluetooth_id);
    if (device4) device4->lpVtbl->Release(device4);
    if (discovery.device) discovery.device->lpVtbl->Release(discovery.device);
    if (device) device->lpVtbl->Release(device); if (lookup) lookup->lpVtbl->Release(lookup);
    if (factory) factory->lpVtbl->Release(factory); WindowsDeleteString(session_class_name); WindowsDeleteString(class_name); RoUninitialize();
}
