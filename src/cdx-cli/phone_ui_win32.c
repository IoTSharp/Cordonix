#include "cdx/phone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define COBJMACROS
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.ui.viewmanagement.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum { UI_ADAPTER = 101, UI_DEVICES, UI_SCAN, UI_PAIR, UI_CONNECT, UI_DISCONNECT, UI_CANCEL, UI_STATUS, UI_LOG, UI_UNPAIR };
enum { UI_CHECK = 120, UI_CAPS, UI_SYNC, UI_PREVIOUS, UI_NEXT, UI_WAIT, UI_HELP, UI_DETAILS, UI_PREVIEW,
    UI_TITLE, UI_SUBTITLE, UI_DEVICE_TITLE, UI_PC_LABEL, UI_PERMISSION_TITLE, UI_PERMISSION_NOTE, UI_GUIDE,
    UI_PREVIEW_TITLE, UI_ACTIVITY, UI_AUDIO_NOTE };
#define UI_DONE (WM_APP + 1)
#define UI_TRAY_MESSAGE (WM_APP + 2)
#define UI_MAX_LIFETIME_MS (8ull * 60ull * 60ull * 1000ull)
#define UI_TRAY_OPEN 1901
#define UI_TRAY_DISCONNECT 1902
#define UI_TRAY_EXIT 1903

typedef struct phone_ui {
    HWND window, adapter, devices, status, log, caps, preview;
    HFONT font, heading, title;
    HBRUSH background_brush, surface_brush;
    COLORREF background, surface, foreground, secondary, border, accent;
    UINT dpi;
    int dark, high_contrast, details;
    HANDLE worker, cancel;
    cdx_phone_session *session;
    cdx_json_output request, response;
    char action[32], radios[16][13], addresses[50][13], connected_address[13];
    char selected_address[13], sms_hash[65];
    wchar_t device_names[50][256];
    int paired[50], checking, next_check, capability, preview_capability, last_incoming, has_page, page_full;
    unsigned offset, sms_offset;
    wchar_t target[256];
    unsigned radio_count, device_count;
    int closing, smoke, smoke_ok, pending_scan, busy;
    int tray_mode, tray_added;
    NOTIFYICONDATAW tray_icon;
    ULONGLONG started;
} phone_ui;

static const char *const capability_actions[] = {"call_status", "sms_list", "contacts", "call_history", "app_notifications"};
static const wchar_t *const capability_names[] = {L"来电状态", L"短信", L"联系人", L"最近通话", L"应用通知"};
/* ANCS uses a separate LE channel; check it before RFCOMM/MAP/PBAP traffic. */
static const unsigned check_order[] = {4, 0, 1, 2, 3};
static void start_action(phone_ui *, const char *);
static void layout(phone_ui *);
static void apply_theme(phone_ui *);
static void select_device(phone_ui *);
static LRESULT draw_control(phone_ui *, NMHDR *);
static LRESULT CALLBACK themed_control(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

static const char *text_field(const cdx_json *json, int object, const char *key) {
    int n = cdx_json_get(json, object, key);
    return n >= 0 && json->nodes[n].type == CDX_JSON_STRING ? json->nodes[n].string : "";
}
static int bool_field(const cdx_json *json, int object, const char *key) {
    int n = cdx_json_get(json, object, key);
    return n >= 0 && json->nodes[n].type == CDX_JSON_BOOL && json->nodes[n].boolean;
}
static void wide_text(const char *text, wchar_t *wide, int count) {
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) wide[0] = 0;
}
static void append_log(phone_ui *ui, const wchar_t *message) {
    SYSTEMTIME time;
    wchar_t line[1400];
    int size = GetWindowTextLengthW(ui->log);
    GetLocalTime(&time);
    if (size > 24000) { SetWindowTextW(ui->log, L""); size = 0; }
    (void)swprintf_s(line, _countof(line), L"%02u:%02u:%02u  %s\r\n", time.wHour, time.wMinute, time.wSecond, message);
    SendMessageW(ui->log, EM_SETSEL, (WPARAM)size, size);
    SendMessageW(ui->log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SetWindowTextW(GetDlgItem(ui->window, UI_ACTIVITY), message);
}
static void tray_update(phone_ui *ui) {
    wchar_t status[256], tip[128];
    if (!ui->tray_added) return;
    GetWindowTextW(ui->status, status, _countof(status));
    (void)swprintf_s(tip, _countof(tip), L"Cordonix · %s", status[0] ? status : L"手机连接");
    wcsncpy_s(ui->tray_icon.szTip, _countof(ui->tray_icon.szTip), tip, _TRUNCATE);
    (void)Shell_NotifyIconW(NIM_MODIFY, &ui->tray_icon);
}
static int tray_add(phone_ui *ui) {
    memset(&ui->tray_icon, 0, sizeof(ui->tray_icon));
    ui->tray_icon.cbSize = sizeof(ui->tray_icon);
    ui->tray_icon.hWnd = ui->window;
    ui->tray_icon.uID = 1;
    ui->tray_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    ui->tray_icon.uCallbackMessage = UI_TRAY_MESSAGE;
    ui->tray_icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(ui->tray_icon.szTip, _countof(ui->tray_icon.szTip), L"Cordonix · 手机连接", _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &ui->tray_icon)) return 0;
    ui->tray_added = 1;
    ui->tray_icon.uVersion = NOTIFYICON_VERSION_4;
    (void)Shell_NotifyIconW(NIM_SETVERSION, &ui->tray_icon);
    return 1;
}
static void tray_remove(phone_ui *ui) {
    if (ui->tray_added) (void)Shell_NotifyIconW(NIM_DELETE, &ui->tray_icon);
    ui->tray_added = 0;
}
static void tray_open(phone_ui *ui) {
    ShowWindow(ui->window, SW_SHOWNORMAL);
    ShowWindow(ui->window, SW_RESTORE);
    SetForegroundWindow(ui->window);
}
static void controls(phone_ui *ui) {
    int selected = ListView_GetNextItem(ui->devices, -1, LVNI_SELECTED);
    int has_device = selected >= 0 && (unsigned)selected < ui->device_count;
    EnableWindow(GetDlgItem(ui->window, UI_SCAN), !ui->busy && ui->radio_count != 0);
    EnableWindow(GetDlgItem(ui->window, UI_PAIR), !ui->busy && has_device);
    EnableWindow(GetDlgItem(ui->window, UI_CONNECT), !ui->busy && has_device);
    EnableWindow(GetDlgItem(ui->window, UI_UNPAIR), !ui->busy && has_device);
    EnableWindow(GetDlgItem(ui->window, UI_DISCONNECT), !ui->busy && ui->connected_address[0] != 0);
    EnableWindow(GetDlgItem(ui->window, UI_CANCEL), ui->busy);
    EnableWindow(ui->adapter, !ui->busy && !ui->connected_address[0]);
    EnableWindow(GetDlgItem(ui->window, UI_CHECK), !ui->busy && has_device && ui->paired[selected]);
    EnableWindow(GetDlgItem(ui->window, UI_SYNC), !ui->busy && has_device && ui->paired[selected]);
    EnableWindow(GetDlgItem(ui->window, UI_PREVIOUS), !ui->busy && ui->has_page && ui->offset >= 20);
    EnableWindow(GetDlgItem(ui->window, UI_NEXT), !ui->busy && ui->has_page && ui->page_full && ui->offset <= 65515);
    EnableWindow(GetDlgItem(ui->window, UI_WAIT), !ui->busy && ui->sms_hash[0] && ui->capability == 1);
    SetWindowTextW(GetDlgItem(ui->window, UI_CHECK), ui->checking ? L"正在检查…" : L"检查全部权限");
}
static DWORD WINAPI ui_worker(void *context) {
    phone_ui *ui = context;
    cdx_json json;
    if (cdx_json_parse(&json, ui->request.data, ui->request.size))
        cdx_phone_session_call(ui->session, &json, 0, ui->cancel, &ui->response);
    else cdx_phone_error(&ui->response, "validation_error", 0);
    cdx_json_free(&json);
    PostMessageW(ui->window, UI_DONE, 0, 0);
    return 0;
}
static void start_action(phone_ui *ui, const char *action) {
    int device = ListView_GetNextItem(ui->devices, -1, LVNI_SELECTED);
    int radio = (int)SendMessageW(ui->adapter, CB_GETCURSEL, 0, 0);
    const char *address = NULL;
    if (ui->busy || ui->closing) return;
    if (strcmp(action, "adapters") && strcmp(action, "devices") && strcmp(action, "discover") &&
        strcmp(action, "connection_status") && strcmp(action, "disconnect")) {
        if (device < 0 || (unsigned)device >= ui->device_count) return;
        address = ui->addresses[device];
    } else if (!strcmp(action, "disconnect")) address = ui->connected_address;
    if (!strcmp(action, "unpair")) {
        wchar_t selected[640], confirmation[1024];
        wcscpy_s(selected, _countof(selected), ui->device_names[device]);
        (void)swprintf_s(confirmation, _countof(confirmation), L"删除这台手机与电脑的配对？\n\n%s\n\n删除后需要重新配对。本窗口与这台手机的连接也会断开。", selected);
        if (MessageBoxW(ui->window, confirmation, L"Cordonix · 删除配对", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) return;
    }
    cdx_json_output_init(&ui->request); cdx_json_output_init(&ui->response);
    cdx_json_raw(&ui->request, "{\"action\":"); cdx_json_quote(&ui->request, action, strlen(action));
    if (address) { cdx_json_raw(&ui->request, ",\"device_address\":"); cdx_json_quote(&ui->request, address, strlen(address)); }
    if (radio >= 0 && (unsigned)radio < ui->radio_count) {
        cdx_json_raw(&ui->request, ",\"radio_address\":"); cdx_json_quote(&ui->request, ui->radios[radio], 12);
    }
    cdx_json_raw(&ui->request, ",\"dry_run\":false,\"max_entries\":");
    cdx_json_uint(&ui->request, !strcmp(action, "devices") || !strcmp(action, "discover") ? 50 : 20);
    cdx_json_raw(&ui->request, ",\"offset\":");
    cdx_json_uint(&ui->request, !strcmp(action, "wait_sms") ? ui->sms_offset : ui->offset);
    if (!strcmp(action, "wait_sms")) {
        cdx_json_raw(&ui->request, ",\"expected_sha256\":"); cdx_json_quote(&ui->request, ui->sms_hash, 64);
    }
    cdx_json_raw(&ui->request, ",\"timeout_ms\":");
    cdx_json_uint(&ui->request, !strcmp(action, "pair") || !strcmp(action, "app_notifications") ? 60000 :
        !strcmp(action, "wait_sms") ? 30000 : !strcmp(action, "discover") ? 15000 : 10000);
    cdx_json_raw(&ui->request, ",\"connection_ttl_ms\":3600000}");
    strcpy_s(ui->action, sizeof(ui->action), action);
    ResetEvent(ui->cancel); ui->busy = 1; controls(ui);
    if (!strcmp(action, "discover")) append_log(ui, L"正在搜索附近设备，请保持手机蓝牙设置页面打开。");
    else if (!strcmp(action, "pair")) append_log(ui, L"请在置顶的 Cordonix 确认框和手机上核对配对码。配对最多等待 60 秒。");
    else if (!strcmp(action, "unpair")) append_log(ui, L"正在删除所选手机与电脑的配对。");
    else if (!strcmp(action, "connect")) append_log(ui, L"正在连接所选手机，成功后保持连接一小时。");
    else if (!strcmp(action, "disconnect")) append_log(ui, L"正在断开本窗口保持的连接。");
    else if (!strcmp(action, "wait_sms")) append_log(ui, L"正在等待短信列表变化，最多 30 秒。可随时取消。");
    else if (!strcmp(action, "sms_list") || !strcmp(action, "contacts") || !strcmp(action, "call_history") ||
             !strcmp(action, "call_status") || !strcmp(action, "app_notifications")) {
        wchar_t line[256];
        if (!strcmp(action, "app_notifications"))
            wcscpy_s(line, _countof(line), L"正在连接通知通道，连接失败后最多重试 5 次；本次最多 60 秒或 20 条，结束后显示结果。可随时取消。");
        else
            (void)swprintf_s(line, _countof(line), L"正在读取%s。若手机出现访问请求，请在手机上确认。",
                capability_names[ui->capability]);
        append_log(ui, line);
    }
    if (!strcmp(action, "pair")) SetForegroundWindow(ui->window);
    ui->worker = CreateThread(NULL, 0, ui_worker, ui, 0, NULL);
    if (!ui->worker) {
        ui->busy = ui->checking = 0; controls(ui); append_log(ui, L"无法启动操作，请重试。");
        cdx_json_output_free(&ui->request); cdx_json_output_free(&ui->response);
    }
}
static void show_adapters(phone_ui *ui, const cdx_json *json) {
    int array = cdx_json_get(json, 0, "adapters"), n;
    SendMessageW(ui->adapter, CB_RESETCONTENT, 0, 0); ui->radio_count = 0;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return;
    for (n = json->nodes[array].first; n >= 0 && ui->radio_count < 16; n = json->nodes[n].next) {
        wchar_t name[512], address[32], label[600];
        const char *a = text_field(json, n, "radio_address");
        if (strlen(a) != 12) continue;
        strcpy_s(ui->radios[ui->radio_count++], 13, a);
        wide_text(text_field(json, n, "name"), name, _countof(name)); wide_text(a, address, _countof(address));
        (void)swprintf_s(label, _countof(label), L"%s", name);
        SendMessageW(ui->adapter, CB_ADDSTRING, 0, (LPARAM)label);
    }
    if (ui->radio_count) { SendMessageW(ui->adapter, CB_SETCURSEL, 0, 0); ui->pending_scan = 1; }
    else append_log(ui, L"未发现可用蓝牙适配器。请在 Windows 中开启蓝牙后重新打开此窗口。");
}
static void show_devices(phone_ui *ui, const cdx_json *json) {
    int array = cdx_json_get(json, 0, "devices"), n, selected = -1, matches = 0;
    int previous = ListView_GetNextItem(ui->devices, -1, LVNI_SELECTED), previous_index = -1;
    char previous_address[13] = {0};
    wchar_t summary[128];
    if (previous >= 0 && (unsigned)previous < ui->device_count) strcpy_s(previous_address, sizeof(previous_address), ui->addresses[previous]);
    ListView_DeleteAllItems(ui->devices); ui->device_count = 0;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return;
    for (n = json->nodes[array].first; n >= 0 && ui->device_count < 50; n = json->nodes[n].next) {
        wchar_t name[256];
        LVITEMW item = {0};
        const char *a = text_field(json, n, "device_address");
        if (strlen(a) != 12) continue;
        strcpy_s(ui->addresses[ui->device_count], 13, a);
        if (!strcmp(a, previous_address)) previous_index = (int)ui->device_count;
        wide_text(text_field(json, n, "name"), name, _countof(name));
        wcscpy_s(ui->device_names[ui->device_count], 256, *name ? name : L"未命名设备");
        ui->paired[ui->device_count] = bool_field(json, n, "paired");
        if (*ui->target && !wcscmp(name, ui->target)) { selected = (int)ui->device_count; ++matches; }
        item.mask = LVIF_TEXT; item.iItem = (int)ui->device_count; item.pszText = ui->device_names[ui->device_count];
        ListView_InsertItem(ui->devices, &item);
        ListView_SetItemText(ui->devices, item.iItem, 1, ui->paired[ui->device_count] ? L"已配对" : L"未配对");
        ++ui->device_count;
    }
    if (previous_index >= 0) selected = previous_index;
    else if (matches != 1) selected = -1;
    if (selected >= 0) ListView_SetItemState(ui->devices, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    select_device(ui);
    (void)swprintf_s(summary, _countof(summary), L"搜索结果：%u 个设备。请选中手机后点击“配对”。", ui->device_count);
    append_log(ui, summary);
    if (*ui->target && !matches && previous_index < 0) append_log(ui, L"预设名称未匹配，请从列表核对实际设备名称后选择。");
    if (!ui->connected_address[0]) SetWindowTextW(ui->status, L"未保持连接。选中未配对手机进行配对；已配对手机可直接连接。");
}
static void show_connection(phone_ui *ui, const cdx_json *json) {
    wchar_t name[512], line[1000], detail[128], battery_text[96];
    const char *state = text_field(json, 0, "connection_state");
    const char *call = text_field(json, 0, "call_state");
    const char *error = text_field(json, 0, "last_error");
    int remaining = cdx_json_get(json, 0, "remaining_ms");
    int battery_supported = bool_field(json, 0, "battery_supported");
    int battery = cdx_json_get(json, 0, "battery_percent");
    unsigned seconds = remaining >= 0 ? (unsigned)(json->nodes[remaining].number / 1000) : 0;
    int was_connected = ui->connected_address[0] != 0;
    wide_text(text_field(json, 0, "phone_name"), name, _countof(name));
    if (battery_supported && battery >= 0 && json->nodes[battery].type == CDX_JSON_NUMBER)
        (void)swprintf_s(battery_text, _countof(battery_text), L"；电量 %d%%", (int)json->nodes[battery].number);
    else wcscpy_s(battery_text, _countof(battery_text), L"；电量未提供");
    if (!strcmp(state, "connected")) {
        const char *a = text_field(json, 0, "device_address");
        if (strlen(a) == 12) strcpy_s(ui->connected_address, 13, a);
        (void)swprintf_s(line, _countof(line), L"已连接：%s\r\n剩余 %u 分 %u 秒；电话状态：%s。关闭窗口会断开。",
            name, seconds / 60, seconds % 60, !strcmp(call, "incoming") ? L"来电" : !strcmp(call, "active") ? L"通话中" : L"空闲 / 呼叫状态");
        if (!was_connected) append_log(ui, L"连接已建立，正在保持并定期检查。可以在手机蓝牙页面核对电脑名称。");
        if (!strcmp(call, "incoming") && !ui->last_incoming) {
            FLASHWINFO flash = {sizeof(flash), ui->window, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
            append_log(ui, L"手机有来电。"); FlashWindowEx(&flash);
        }
        ui->last_incoming = !strcmp(call, "incoming");
    } else {
        ui->connected_address[0] = 0; ui->last_incoming = 0;
        wide_text(error, detail, _countof(detail));
        (void)swprintf_s(line, _countof(line), L"%s%s%s",
            !strcmp(state, "expired") ? L"连接时间已到，请重新连接。" : L"未保持连接。配对成功后请点击“连接并保持”。",
            *detail ? L"\r\n原因：" : L"", detail);
        if (was_connected) append_log(ui, L"连接已断开。需要时可手动重新连接。");
    }
    if (!strcmp(state, "connected")) wcsncat_s(line, _countof(line), battery_text, _TRUNCATE);
    SetWindowTextW(ui->status, line);
    tray_update(ui);
}
static void capability_guide(phone_ui *ui) {
    static const wchar_t *const guides[] = {
        L"连接后会定期检查来电，并在任务栏提示。此项检查电话状态读取；实际来电提醒需用来电验证。",
        L"在 iPhone「设置 → 蓝牙」点电脑旁的 ⓘ，检查“显示通知”。先同步短信，再等待列表变化；只读取手机提供的内容。",
        L"在 iPhone「设置 → 蓝牙」点电脑旁的 ⓘ，开启“同步通讯录”，并选择需要共享的联系人分组。",
        L"在同一蓝牙详情页开启“同步通讯录”，并允许“最近通话”（若显示）。通话记录与联系人分别检测。",
        L"应用通知每次最多 60 秒或 20 条，建链失败最多重试 5 次。请在 iPhone 蓝牙详情页允许通知共享；已有通知会单独标记。"
    };
    SetWindowTextW(GetDlgItem(ui->window, UI_GUIDE), guides[ui->capability]);
    SetWindowTextW(GetDlgItem(ui->window, UI_SYNC),
        ui->capability == 0 ? L"读取电话状态" : ui->capability == 4 ? L"等待应用通知" : L"同步所选");
}

static void select_device(phone_ui *ui) {
    int selected = ListView_GetNextItem(ui->devices, -1, LVNI_SELECTED), i;
    const char *address = selected >= 0 && (unsigned)selected < ui->device_count ? ui->addresses[selected] : "";
    if (!strcmp(address, ui->selected_address)) return;
    strcpy_s(ui->selected_address, sizeof(ui->selected_address), address);
    ui->sms_hash[0] = 0; ui->offset = 0; ui->has_page = ui->page_full = 0;
    ListView_DeleteAllItems(ui->preview);
    SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), L"本次同步 · 内容仅保留在本窗口");
    for (i = 0; i < 5; ++i) {
        ListView_SetItemText(ui->caps, i, 1, L"尚未检测");
        ListView_SetItemText(ui->caps, i, 2, L"配对不会自动授予访问权限");
    }
    capability_guide(ui); controls(ui);
}

static void show_notifications(phone_ui *ui, const cdx_json *json) {
    int notifications = cdx_json_get(json, 0, "notifications"), notification_count = 0, new_count = 0;
    wchar_t result[384];
    ListView_DeleteAllItems(ui->preview);
    ui->has_page = ui->page_full = 0;
    if (notifications >= 0 && json->nodes[notifications].type == CDX_JSON_ARRAY) {
        int item_node;
        for (item_node = json->nodes[notifications].first; item_node >= 0 && notification_count < 50;
             item_node = json->nodes[item_node].next) {
            wchar_t app[256], title[512], message[1024], info[1200];
            LVITEMW item = {0};
            int event_node = cdx_json_get(json, item_node, "event_id");
            int removed = event_node >= 0 && json->nodes[event_node].number == 2;
            int modified = event_node >= 0 && json->nodes[event_node].number == 1;
            int preexisting = bool_field(json, item_node, "preexisting");
            wide_text(text_field(json, item_node, "app"), app, _countof(app));
            wide_text(text_field(json, item_node, "title"), title, _countof(title));
            wide_text(text_field(json, item_node, "message"), message, _countof(message));
            if (!*app) wcscpy_s(app, _countof(app), L"应用");
            if (!*title) wcscpy_s(title, _countof(title), L"应用通知");
            if (!*message) wcscpy_s(message, _countof(message), L"（无正文）");
            if (removed) wcscpy_s(info, _countof(info), L"手机通知已移除；不读取已移除通知的正文。");
            else (void)swprintf_s(info, _countof(info), L"%s：%s", title, message);
            item.mask = LVIF_TEXT; item.iItem = notification_count++; item.pszText = app;
            ListView_InsertItem(ui->preview, &item);
            ListView_SetItemText(ui->preview, item.iItem, 1, removed ? L"已移除" :
                preexisting ? L"已有通知" : modified ? L"已更新" : L"新通知");
            ListView_SetItemText(ui->preview, item.iItem, 2, info);
            if (!removed && !modified && !preexisting) ++new_count;
        }
    }
    (void)swprintf_s(result, _countof(result), L"%s · 收到 %d 条事件，其中 %d 条新通知",
        bool_field(json, 0, "partial") ? L"监听未完成，已保留收到的内容" :
        !strcmp(text_field(json, 0, "collection_status"), "max_entries") ? L"已达本次数量上限，监听结束" : L"本次监听结束",
        notification_count, new_count);
    SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), result);
    append_log(ui, result);
    if (new_count) {
        FLASHWINFO flash = {sizeof(flash), ui->window, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&flash);
    }
}

static void show_capability(phone_ui *ui, const cdx_json *json) {
    const char *status = text_field(json, 0, "status");
    const wchar_t *label = !strcmp(status, "ok") ? L"可访问" : !strcmp(status, "permission_denied") ? L"访问被拒绝" :
        !strcmp(status, "ancs_unavailable") ? L"未找到应用通知通道" :
        !strcmp(status, "connection_failed") ? L"通道连接失败" :
        !strcmp(status, "ble_unavailable") ? L"蓝牙通道不可用" :
        !strcmp(status, "ble_protocol_error") ? L"应用通知协议错误" :
        !strcmp(status, "pairing_required") ? L"需要配对" : !strcmp(status, "timed_out") ? L"检查超时" :
        !strcmp(status, "cancelled") ? L"已取消" : !strcmp(status, "not_supported") ? L"不支持" : L"检查失败";
    SYSTEMTIME time;
    wchar_t detail[256], result[384], reason[128];
    int cap = ui->capability, n, array, count = 0;
    GetLocalTime(&time);
    wide_text(status, reason, _countof(reason));
    (void)swprintf_s(detail, _countof(detail), L"%02u:%02u:%02u · %s",
        time.wHour, time.wMinute, time.wSecond, strcmp(status, "ok") ? reason :
        cap == 4 && !strcmp(text_field(json, 0, "wait_status"), "timed_out") ? L"订阅成功，等待期间未收到事件" : L"实际读取成功");
    ListView_SetItemText(ui->caps, cap, 1, (wchar_t *)label);
    ListView_SetItemText(ui->caps, cap, 2, detail);
    (void)swprintf_s(result, _countof(result), L"%s：%s。", capability_names[cap], label);
    append_log(ui, result);
    if (cap == 4) {
        int attempts = cdx_json_get(json, 0, "connection_attempts");
        int retries = cdx_json_get(json, 0, "connection_retries");
        if (attempts >= 0 && retries >= 0) {
            (void)swprintf_s(result, _countof(result), L"通知通道建链尝试 %u 次，其中重试 %u 次（最多 5 次）。%s",
                (unsigned)json->nodes[attempts].number, (unsigned)json->nodes[retries].number,
                bool_field(json, 0, "connection_retry_exhausted") ? L"重试次数已用完，连接失败。" : L"");
            append_log(ui, result);
        }
    }
    if (strcmp(status, "ok")) {
        if (!strcmp(status, "permission_denied")) append_log(ui, L"手机明确拒绝了本次访问。请在手机蓝牙详情页检查授权，再重新检测。");
        else if (cap == 4) {
            wchar_t stage[128], diagnostic[256];
            int error = cdx_json_get(json, 0, "hresult");
            wide_text(text_field(json, 0, "stage"), stage, _countof(stage));
            (void)swprintf_s(diagnostic, _countof(diagnostic), L"通知检测阶段：%s；错误码：0x%08X。", stage,
                error >= 0 ? (unsigned)json->nodes[error].number : 0);
            append_log(ui, diagnostic);
            append_log(ui, L"电话连接和通知连接分别检测。此结果不能证明手机关闭了通知权限。");
        }
        else if (strcmp(status, "cancelled")) append_log(ui, L"本次未能验证访问能力；连接或协议错误不能证明手机关闭了权限。");
        if (cap == 4 && bool_field(json, 0, "partial")) { show_notifications(ui, json); return; }
        ui->has_page = ui->page_full = 0;
        if (cap == 1) ui->sms_hash[0] = 0;
        ListView_DeleteAllItems(ui->preview);
        SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), L"本次读取未完成 · 未显示旧数据");
        return;
    }
    if (cap == 0) {
        const char *call = text_field(json, 0, "call_state");
        (void)swprintf_s(result, _countof(result), L"电话状态：%s · 实际来电通知尚需验证",
            !strcmp(call, "incoming") ? L"有来电" : !strcmp(call, "active") ? L"通话中" :
            !strcmp(call, "idle") ? L"空闲" : L"呼叫中");
        ListView_DeleteAllItems(ui->preview); ui->has_page = 0;
        SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), result); return;
    }
    if (cap == 4) {
        show_notifications(ui, json);
        return;
    }
    if (cap == 1) {
        const char *hash = text_field(json, 0, "sha256");
        if (strlen(hash) == 64) { strcpy_s(ui->sms_hash, 65, hash); ui->sms_offset = ui->offset; }
    }
    ListView_DeleteAllItems(ui->preview);
    ui->has_page = ui->page_full = 0;
    if (strcmp(text_field(json, 0, "preview_status"), "ok")) {
        SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), L"已读取数据 · 手机返回的格式暂不支持列表预览");
        return;
    }
    array = cdx_json_get(json, 0, "items");
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return;
    for (n = json->nodes[array].first; n >= 0 && count < 50; n = json->nodes[n].next) {
        wchar_t name[1024], number[1024], info[1200], date[128], extra[1024];
        LVITEMW item = {0};
        const char *kind = text_field(json, n, "direction");
        int numbers = cdx_json_get(json, n, "number_count");
        wide_text(text_field(json, n, cap == 1 ? "sender_name" : "name"), name, _countof(name));
        wide_text(text_field(json, n, cap == 1 ? "sender_addressing" : "number"), number, _countof(number));
        wide_text(text_field(json, n, "datetime"), date, _countof(date));
        wide_text(text_field(json, n, "subject"), extra, _countof(extra));
        if (!*name) wcscpy_s(name, _countof(name), *number ? number : L"未提供名称");
        if (cap == 1) (void)swprintf_s(info, _countof(info), L"%s  %s", date, extra);
        else if (cap == 2) (void)swprintf_s(info, _countof(info), numbers >= 0 && json->nodes[numbers].number > 1 ?
            L"含 %u 个号码；此处显示首个" : L"%u 个号码", numbers >= 0 ? (unsigned)json->nodes[numbers].number : 0);
        else (void)swprintf_s(info, _countof(info), L"%s  %s", date,
            !strcmp(kind, "missed") ? L"未接" : !strcmp(kind, "incoming") ? L"呼入" : !strcmp(kind, "outgoing") ? L"呼出" : L"类型未提供");
        item.mask = LVIF_TEXT; item.iItem = count++; item.pszText = name; ListView_InsertItem(ui->preview, &item);
        ListView_SetItemText(ui->preview, item.iItem, 1, number); ListView_SetItemText(ui->preview, item.iItem, 2, info);
    }
    ui->has_page = 1; ui->page_full = count == 20; ui->preview_capability = cap;
    (void)swprintf_s(result, _countof(result), L"%s · 第 %u 页，%d 条 · 仅当前页，不代表完整历史", capability_names[cap], ui->offset / 20 + 1, count);
    SetWindowTextW(GetDlgItem(ui->window, UI_PREVIEW_TITLE), result);
    if (!count) {
        ListView_SetItemText(ui->caps, cap, 1, L"可访问（空）");
        append_log(ui, L"读取成功，手机返回空列表。空列表不能证明完整历史已同步。");
    }
    if (!strcmp(ui->action, "wait_sms")) {
        FLASHWINFO flash = {sizeof(flash), ui->window, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        append_log(ui, L"短信列表发生变化；可能是新增、删除或其他变化。"); FlashWindowEx(&flash);
    }
}

static void complete_action(phone_ui *ui) {
    cdx_json json;
    int parsed;
    if (!ui->worker) return;
    if (WaitForSingleObject(ui->worker, 2000) != WAIT_OBJECT_0) return;
    CloseHandle(ui->worker); ui->worker = NULL; ui->busy = 0;
    parsed = cdx_json_parse(&json, ui->response.data, ui->response.size);
    if (parsed) {
        const char *status = text_field(&json, 0, "status");
        int pairing_result = cdx_json_get(&json, 0, "pairing_result");
        int authentication = cdx_json_get(&json, 0, "authentication");
        if (pairing_result >= 0) {
            static const wchar_t *const labels[] = {
                L"配对成功", L"设备尚未就绪", L"未配对", L"已配对", L"手机拒绝连接",
                L"连接数量已满", L"硬件错误", L"认证超时", L"不允许认证", L"认证失败",
                L"没有支持的服务", L"无法满足保护级别", L"访问被拒绝", L"确认数据无效",
                L"已取消", L"另一项配对正在进行", L"未注册确认处理", L"用户未同意", L"手机仍保留关联", L"系统配对失败"
            };
            wchar_t detail[256], stage[80];
            unsigned value = (unsigned)json.nodes[pairing_result].number;
            wide_text(text_field(&json, 0, "pairing_stage"), stage, _countof(stage));
            (void)swprintf_s(detail, _countof(detail), L"系统配对结果：%s（%u）；阶段：%s。",
                value < _countof(labels) ? labels[value] : L"未知结果", value, stage);
            append_log(ui, detail);
        }
        if (authentication >= 0) {
            wchar_t detail[512];
            int response_error = cdx_json_get(&json, authentication, "response_error");
            int method = cdx_json_get(&json, authentication, "method");
            int confirmation_ms = cdx_json_get(&json, authentication, "confirmation_ms");
            int response_ms = cdx_json_get(&json, authentication, "response_ms");
            (void)swprintf_s(detail, _countof(detail), L"配对确认：请求%s；方式 %u；用户%s；回传%s，错误 %u；确认耗时 %u 毫秒，回传耗时 %u 毫秒。",
                bool_field(&json, authentication, "callback_received") ? L"已收到" : L"未收到",
                method >= 0 ? (unsigned)json.nodes[method].number : 0,
                bool_field(&json, authentication, "user_confirmed") ? L"已同意" : L"未同意",
                bool_field(&json, authentication, "response_sent") ? L"已调用" : L"未调用",
                response_error >= 0 ? (unsigned)json.nodes[response_error].number : 0,
                confirmation_ms >= 0 ? (unsigned)json.nodes[confirmation_ms].number : 0,
                response_ms >= 0 ? (unsigned)json.nodes[response_ms].number : 0);
            append_log(ui, detail);
        }
        if (!strcmp(status, "ok") || !strcmp(status, "output_limit")) {
            if (!strcmp(ui->action, "adapters")) show_adapters(ui, &json);
            else if (!strcmp(ui->action, "discover") || !strcmp(ui->action, "devices")) show_devices(ui, &json);
            else if (!strcmp(ui->action, "pair")) {
                append_log(ui, bool_field(&json, 0, "already_paired") ?
                    L"所选设备之前已经配对，本次没有重新配对。可以直接点击“连接并保持”。" :
                    L"已回读确认配对成功。请在手机上允许通知/联系人访问，再点击“连接并保持”。");
                ui->pending_scan = 2;
            } else if (!strcmp(ui->action, "unpair")) {
                append_log(ui, L"已确认电脑上的配对记录已删除。若手机仍保存此电脑，请在手机上忽略此设备，再重新搜索配对。");
                if (!strcmp(ui->selected_address, ui->connected_address)) {
                    ui->connected_address[0] = 0;
                    SetWindowTextW(ui->status, L"已删除配对。需要使用时请重新配对。");
                }
                ui->selected_address[0] = 0;
                select_device(ui);
                ui->pending_scan = 2;
            } else if (!strcmp(ui->action, "connect") || !strcmp(ui->action, "disconnect") || !strcmp(ui->action, "connection_status"))
                show_connection(ui, &json);
            else show_capability(ui, &json);
        } else {
            wchar_t status_w[128], reason[128], line[512];
            int error = cdx_json_get(&json, 0, "win32_error");
            wide_text(status, status_w, _countof(status_w));
            wide_text(text_field(&json, 0, "reason"), reason, _countof(reason));
            (void)swprintf_s(line, _countof(line), L"操作未完成：%s %s（系统错误 %u）。", status_w, reason,
                error >= 0 ? (unsigned)json.nodes[error].number : 0);
            append_log(ui, line);
            if (!strcmp(ui->action, capability_actions[ui->capability])) show_capability(ui, &json);
            if (!strcmp(ui->action, "wait_sms")) {
                append_log(ui, !strcmp(status, "timed_out") ? L"等待结束：期间未观察到列表变化，短信访问权限未被改判。" :
                    L"短信等待已结束；可重新同步以验证当前访问状态。");
            }
            if (!strcmp(status, "unknown")) append_log(ui, L"结果尚不确定。请查看手机和已配对列表，确认后再操作。");
        }
    } else append_log(ui, L"无法读取操作结果。");
    cdx_json_free(&json); cdx_json_output_free(&ui->request); cdx_json_output_free(&ui->response);
    controls(ui);
    if (ui->closing) { DestroyWindow(ui->window); return; }
    if (ui->checking) {
        if (WaitForSingleObject(ui->cancel, 0) == WAIT_OBJECT_0 || ui->next_check >= 5) {
            ui->checking = 0; controls(ui); append_log(ui, L"权限检查结束。各项结果显示在上方；手机上的授权开关需要你亲自确认。");
        } else {
            ui->capability = (int)check_order[ui->next_check++]; ui->offset = 0;
            ListView_SetItemState(ui->caps, ui->capability, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            capability_guide(ui); start_action(ui, capability_actions[ui->capability]); return;
        }
    }
    if (ui->pending_scan) {
        int scan = ui->pending_scan; ui->pending_scan = 0;
        start_action(ui, scan == 1 ? "discover" : "devices");
    }
}
static int scale(phone_ui *ui, int value) { return MulDiv(value, (int)(ui->dpi ? ui->dpi : 96), 96); }

static HWND control(phone_ui *ui, const wchar_t *class_name, const wchar_t *text, DWORD style, int id) {
    HWND result = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, ui->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    return result;
}

static void column(HWND list, int index, const wchar_t *name) {
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (wchar_t *)name; col.cx = 100;
    ListView_InsertColumn(list, index, &col);
}

static BOOL CALLBACK font_control(HWND window, LPARAM parameter) {
    phone_ui *ui = (phone_ui *)parameter;
    int id = GetDlgCtrlID(window);
    HFONT font = id == UI_TITLE ? ui->title : id == UI_DEVICE_TITLE || id == UI_PERMISSION_TITLE ? ui->heading : ui->font;
    SendMessageW(window, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

static void system_fonts(phone_ui *ui) {
    NONCLIENTMETRICSW metrics = {0};
    HFONT old_font = ui->font, old_title = ui->title, old_heading = ui->heading;
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, ui->dpi)) return;
    if (-metrics.lfMessageFont.lfHeight < scale(ui, 14)) metrics.lfMessageFont.lfHeight = -scale(ui, 14);
    ui->font = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfWeight = FW_SEMIBOLD; metrics.lfMessageFont.lfHeight = -scale(ui, 16);
    ui->heading = CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfHeight = -scale(ui, 26);
    ui->title = CreateFontIndirectW(&metrics.lfMessageFont);
    EnumChildWindows(ui->window, font_control, (LPARAM)ui);
    SendMessageW(ui->adapter, CB_SETITEMHEIGHT, 0, scale(ui, 28));
    SendMessageW(ui->adapter, CB_SETITEMHEIGHT, (WPARAM)-1, scale(ui, 28));
    {
        HWND lists[] = {ui->devices, ui->caps, ui->preview};
        size_t i;
        for (i = 0; i < _countof(lists); ++i) {
            HIMAGELIST spacing = ImageList_Create(1, scale(ui, 32), ILC_COLOR32, 1, 1);
            HIMAGELIST previous = ListView_SetImageList(lists[i], spacing, LVSIL_SMALL);
            if (previous) ImageList_Destroy(previous);
        }
    }
    if (old_font) DeleteObject(old_font);
    if (old_heading) DeleteObject(old_heading);
    if (old_title) DeleteObject(old_title);
}

static void apply_theme(phone_ui *ui) {
    static const IID settings3_id = {0x03021be4,0x5254,0x4781,{0x81,0x94,0x51,0x68,0xf7,0xd0,0x6d,0x7b}};
    HIGHCONTRASTW contrast = {sizeof(contrast), 0, NULL};
    IInspectable *instance = NULL;
    __x_ABI_CWindows_CUI_CViewManagement_CIUISettings3 *settings = NULL;
    struct __x_ABI_CWindows_CUI_CColor foreground = {0}, accent = {0};
    HSTRING name = NULL;
    BOOL dark;
    HWND lists[] = {ui->devices, ui->caps, ui->preview};
    size_t i;
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    ui->high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    ui->dark = 0; ui->accent = GetSysColor(COLOR_HIGHLIGHT);
    if (!ui->high_contrast && SUCCEEDED(WindowsCreateString(L"Windows.UI.ViewManagement.UISettings", 36, &name)) && name &&
        SUCCEEDED(RoActivateInstance(name, &instance)) &&
        SUCCEEDED(instance->lpVtbl->QueryInterface(instance, &settings3_id, (void **)&settings))) {
        if (SUCCEEDED(settings->lpVtbl->GetColorValue(settings, UIColorType_Foreground, &foreground)))
            ui->dark = (unsigned)foreground.R + foreground.G + foreground.B > 384;
        if (SUCCEEDED(settings->lpVtbl->GetColorValue(settings, UIColorType_Accent, &accent)))
            ui->accent = RGB(accent.R, accent.G, accent.B);
    }
    if (settings) settings->lpVtbl->Release(settings);
    if (instance) instance->lpVtbl->Release(instance);
    if (name) WindowsDeleteString(name);
    ui->background = ui->high_contrast ? GetSysColor(COLOR_WINDOW) : ui->dark ? RGB(32,32,32) : RGB(249,249,249);
    ui->surface = ui->high_contrast ? GetSysColor(COLOR_WINDOW) : ui->dark ? RGB(43,43,43) : RGB(255,255,255);
    ui->foreground = ui->high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : ui->dark ? RGB(245,245,245) : RGB(26,26,26);
    ui->secondary = ui->high_contrast ? ui->foreground : ui->dark ? RGB(196,196,196) : RGB(92,92,92);
    ui->border = ui->high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : ui->dark ? RGB(80,80,80) : RGB(220,220,220);
    if (ui->background_brush) DeleteObject(ui->background_brush);
    if (ui->surface_brush) DeleteObject(ui->surface_brush);
    ui->background_brush = CreateSolidBrush(ui->background); ui->surface_brush = CreateSolidBrush(ui->surface);
    dark = ui->dark;
    (void)DwmSetWindowAttribute(ui->window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    for (i = 0; i < _countof(lists); ++i) {
        SetWindowTheme(lists[i], ui->high_contrast || ui->dark ? L"" : L"Explorer", NULL);
        ListView_SetBkColor(lists[i], ui->surface);
        ListView_SetTextBkColor(lists[i], ui->surface);
        ListView_SetTextColor(lists[i], ui->foreground);
    }
    SetWindowTheme(ui->adapter, ui->dark || ui->high_contrast ? L"" : L"Explorer", NULL);
    RedrawWindow(ui->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void place(phone_ui *ui, int id, int x, int y, int width, int height) {
    MoveWindow(GetDlgItem(ui->window, id), scale(ui, x), scale(ui, y), scale(ui, width), scale(ui, height), TRUE);
}

static void layout(phone_ui *ui) {
    RECT rect;
    int width, height, right = 356, available, list_height;
    GetClientRect(ui->window, &rect);
    width = MulDiv(rect.right, 96, (int)ui->dpi); height = MulDiv(rect.bottom, 96, (int)ui->dpi);
    available = width - right - 24; list_height = height - 610;
    if (list_height < 120) list_height = 120;
    place(ui, UI_TITLE, 24, 20, width - 48, 38);
    place(ui, UI_SUBTITLE, 24, 64, width - 48, 26);
    place(ui, UI_DEVICE_TITLE, 24, 112, 304, 28);
    place(ui, UI_PC_LABEL, 24, 154, 304, 24);
    place(ui, UI_ADAPTER, 24, 181, 304, 180);
    place(ui, UI_DEVICES, 24, 224, 304, 164);
    place(ui, UI_SCAN, 24, 404, 146, 34); place(ui, UI_PAIR, 182, 404, 146, 34);
    place(ui, UI_CONNECT, 24, 450, 146, 34); place(ui, UI_DISCONNECT, 182, 450, 146, 34);
    place(ui, UI_UNPAIR, 24, 496, 304, 34);
    place(ui, UI_STATUS, 24, 550, 304, 106);
    place(ui, UI_AUDIO_NOTE, 24, 668, 304, 44);
    place(ui, UI_PERMISSION_TITLE, right, 112, available - 166, 28);
    place(ui, UI_CHECK, width - 178, 106, 154, 34);
    place(ui, UI_PERMISSION_NOTE, right, 154, available, 24);
    place(ui, UI_CAPS, right, 190, available, 172);
    place(ui, UI_GUIDE, right, 378, available, 58);
    place(ui, UI_HELP, right, 450, 100, 34);
    place(ui, UI_SYNC, right + 112, 450, 120, 34);
    place(ui, UI_PREVIOUS, right + 244, 450, 72, 34);
    place(ui, UI_NEXT, right + 328, 450, 72, 34);
    place(ui, UI_WAIT, right + 412, 450, available - 412, 34);
    place(ui, UI_PREVIEW_TITLE, right, 502, available, 24);
    place(ui, UI_PREVIEW, right, 536, available, list_height);
    place(ui, UI_LOG, right, 536, available, list_height);
    place(ui, UI_ACTIVITY, 24, height - 50, width - 304, 34);
    place(ui, UI_DETAILS, width - 264, height - 54, 108, 34);
    place(ui, UI_CANCEL, width - 144, height - 54, 120, 34);
    GetClientRect(ui->devices, &rect);
    ListView_SetColumnWidth(ui->devices, 0, scale(ui, 215)); ListView_SetColumnWidth(ui->devices, 1, rect.right - scale(ui, 215));
    GetClientRect(ui->caps, &rect);
    ListView_SetColumnWidth(ui->caps, 0, scale(ui, 112)); ListView_SetColumnWidth(ui->caps, 1, scale(ui, 100));
    ListView_SetColumnWidth(ui->caps, 2, rect.right - scale(ui, 212));
    GetClientRect(ui->preview, &rect);
    ListView_SetColumnWidth(ui->preview, 0, scale(ui, 142)); ListView_SetColumnWidth(ui->preview, 1, scale(ui, 148));
    ListView_SetColumnWidth(ui->preview, 2, rect.right - scale(ui, 290));
    ShowWindow(ui->log, ui->details ? SW_SHOW : SW_HIDE);
    ShowWindow(ui->preview, ui->details ? SW_HIDE : SW_SHOW);
}

static LRESULT draw_control(phone_ui *ui, NMHDR *header) {
    NMCUSTOMDRAW *draw = (NMCUSTOMDRAW *)header;
    wchar_t class_name[64];
    GetClassNameW(header->hwndFrom, class_name, _countof(class_name));
    if (!ui->dark) return CDRF_DODEFAULT;
    if (!wcscmp(class_name, L"Button") && draw->dwDrawStage == CDDS_PREPAINT) {
        RECT rect = draw->rc;
        wchar_t label[128];
        int disabled = !IsWindowEnabled(header->hwndFrom);
        HBRUSH fill = CreateSolidBrush(draw->uItemState & CDIS_SELECTED ? RGB(60,60,60) :
            draw->uItemState & CDIS_HOT ? RGB(55,55,55) : ui->surface);
        HPEN pen = CreatePen(PS_SOLID, 1, draw->uItemState & CDIS_FOCUS ? ui->accent : ui->border);
        HGDIOBJ old_brush = SelectObject(draw->hdc, fill), old_pen = SelectObject(draw->hdc, pen);
        FillRect(draw->hdc, &rect, ui->background_brush);
        RoundRect(draw->hdc, rect.left, rect.top, rect.right, rect.bottom, scale(ui, 6), scale(ui, 6));
        SelectObject(draw->hdc, old_brush); SelectObject(draw->hdc, old_pen); DeleteObject(fill); DeleteObject(pen);
        SetBkMode(draw->hdc, TRANSPARENT); SetTextColor(draw->hdc, disabled ? RGB(140,140,140) : ui->foreground);
        GetWindowTextW(header->hwndFrom, label, _countof(label));
        DrawTextW(draw->hdc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if ((draw->uItemState & CDIS_FOCUS) && !(SendMessageW(header->hwndFrom, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&rect, -scale(ui, 3), -scale(ui, 3)); DrawFocusRect(draw->hdc, &rect);
        }
        return CDRF_SKIPDEFAULT;
    }
    if (!wcscmp(class_name, WC_HEADERW)) {
        if (draw->dwDrawStage == CDDS_PREPAINT) {
            FillRect(draw->hdc, &draw->rc, ui->surface_brush); return CDRF_NOTIFYITEMDRAW;
        }
        if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            HDITEMW item = {0}; wchar_t label[128]; RECT rect = draw->rc;
            item.mask = HDI_TEXT; item.pszText = label; item.cchTextMax = _countof(label);
            Header_GetItem(header->hwndFrom, (int)draw->dwItemSpec, &item);
            FillRect(draw->hdc, &rect, ui->surface_brush);
            SetBkMode(draw->hdc, TRANSPARENT); SetTextColor(draw->hdc, ui->secondary);
            rect.left += scale(ui, 8); DrawTextW(draw->hdc, label, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return CDRF_SKIPDEFAULT;
        }
    }
    if (!wcscmp(class_name, WC_LISTVIEWW)) {
        if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            RECT row;
            int col, left = 0, columns = Header_GetItemCount(ListView_GetHeader(header->hwndFrom));
            int selected = (ListView_GetItemState(header->hwndFrom, (int)draw->dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            HBRUSH fill = CreateSolidBrush(selected ? RGB(64,64,64) : ui->surface);
            ListView_GetItemRect(header->hwndFrom, (int)draw->dwItemSpec, &row, LVIR_BOUNDS);
            FillRect(draw->hdc, &row, fill); DeleteObject(fill);
            SetBkMode(draw->hdc, TRANSPARENT); SetTextColor(draw->hdc, ui->foreground);
            for (col = 0; col < columns && col < 3; ++col) {
                RECT rect = row; wchar_t text[1200];
                int width = ListView_GetColumnWidth(header->hwndFrom, col);
                ListView_GetItemText(header->hwndFrom, (int)draw->dwItemSpec, col, text, _countof(text));
                rect.left = row.left + left + scale(ui, 8); rect.right = row.left + left + width - scale(ui, 6);
                DrawTextW(draw->hdc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                left += width;
            }
            if (selected && GetFocus() == header->hwndFrom &&
                !(SendMessageW(header->hwndFrom, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) DrawFocusRect(draw->hdc, &row);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

static void combo_face(phone_ui *ui, HWND window, HDC dc) {
    RECT rect, text_rect;
    wchar_t text[600] = L"";
    int item = (int)SendMessageW(window, CB_GETCURSEL, 0, 0), edge = scale(ui, 30);
    HPEN pen = CreatePen(PS_SOLID, 1, ui->border);
    HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, ui->surface_brush);
    HGDIOBJ old_font = SelectObject(dc, ui->font);
    POINT arrow[3];
    GetClientRect(window, &rect); text_rect = rect;
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    if (item >= 0) SendMessageW(window, CB_GETLBTEXT, (WPARAM)item, (LPARAM)text);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(window) ? ui->foreground : ui->secondary);
    text_rect.left += scale(ui, 8); text_rect.right -= edge;
    DrawTextW(dc, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old_pen); DeleteObject(pen); pen = CreatePen(PS_SOLID, 1, ui->secondary); SelectObject(dc, pen);
    arrow[0].x = rect.right - scale(ui, 19); arrow[0].y = rect.bottom / 2 - scale(ui, 2);
    arrow[1].x = rect.right - scale(ui, 15); arrow[1].y = rect.bottom / 2 + scale(ui, 2);
    arrow[2].x = rect.right - scale(ui, 11); arrow[2].y = arrow[0].y;
    Polyline(dc, arrow, 3);
    if (GetFocus() == window && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        InflateRect(&text_rect, -scale(ui, 2), -scale(ui, 3)); DrawFocusRect(dc, &text_rect);
    }
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); SelectObject(dc, old_font); DeleteObject(pen);
}

static LRESULT CALLBACK themed_control(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    phone_ui *ui = (phone_ui *)data;
    if (ui->dark && message == WM_NOTIFY && ((NMHDR *)lparam)->code == NM_CUSTOMDRAW)
        return draw_control(ui, (NMHDR *)lparam);
    if (ui->dark && window == ui->adapter) {
        if (message == WM_PAINT) {
            PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
            combo_face(ui, window, dc); EndPaint(window, &paint); return 0;
        }
        if (message == WM_PRINTCLIENT) { combo_face(ui, window, (HDC)wparam); return 0; }
        if (message == WM_ERASEBKGND) return 1;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, themed_control, id);
    return DefSubclassProc(window, message, wparam, lparam);
}

static void create_controls(phone_ui *ui) {
    int i;
    DWORD list_style = WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER;
    control(ui, L"STATIC", L"手机连接", 0, UI_TITLE);
    control(ui, L"STATIC", L"配对手机，检查访问权限，查看本次同步的内容。", 0, UI_SUBTITLE);
    control(ui, L"STATIC", L"设备连接", 0, UI_DEVICE_TITLE);
    control(ui, L"STATIC", L"本机蓝牙名称 · 在手机上核对", 0, UI_PC_LABEL);
    ui->adapter = control(ui, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, UI_ADAPTER);
    SetWindowSubclass(ui->adapter, themed_control, 1, (DWORD_PTR)ui);
    ui->devices = control(ui, WC_LISTVIEWW, L"手机设备", list_style, UI_DEVICES);
    column(ui->devices, 0, L"设备名称"); column(ui->devices, 1, L"配对状态");
    ListView_SetExtendedListViewStyle(ui->devices, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    control(ui, L"BUTTON", L"搜索手机", WS_TABSTOP | BS_PUSHBUTTON, UI_SCAN);
    control(ui, L"BUTTON", L"配对", WS_TABSTOP | BS_PUSHBUTTON, UI_PAIR);
    control(ui, L"BUTTON", L"连接并保持", WS_TABSTOP | BS_PUSHBUTTON, UI_CONNECT);
    control(ui, L"BUTTON", L"断开", WS_TABSTOP | BS_PUSHBUTTON, UI_DISCONNECT);
    control(ui, L"BUTTON", L"删除配对", WS_TABSTOP | BS_PUSHBUTTON, UI_UNPAIR);
    ui->status = control(ui, L"STATIC", L"尚未连接。\r\n正在读取电脑蓝牙适配器。", 0, UI_STATUS);
    control(ui, L"STATIC", L"通话声音仍在手机端。\r\n连接与各项访问权限分别检查。", 0, UI_AUDIO_NOTE);
    control(ui, L"STATIC", L"权限与功能", 0, UI_PERMISSION_TITLE);
    control(ui, L"BUTTON", L"检查全部权限", WS_TABSTOP | BS_PUSHBUTTON, UI_CHECK);
    control(ui, L"STATIC", L"状态来自实际读取；手机端的开关需要你亲自开启。", 0, UI_PERMISSION_NOTE);
    ui->caps = control(ui, WC_LISTVIEWW, L"各项访问能力", list_style, UI_CAPS);
    column(ui->caps, 0, L"功能"); column(ui->caps, 1, L"访问状态"); column(ui->caps, 2, L"最近检查");
    ListView_SetExtendedListViewStyle(ui->caps, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    for (i = 0; i < 5; ++i) {
        LVITEMW item = {0};
        item.mask = LVIF_TEXT; item.iItem = i; item.pszText = (wchar_t *)capability_names[i];
        ListView_InsertItem(ui->caps, &item); ListView_SetItemText(ui->caps, i, 1, L"尚未检测");
        ListView_SetItemText(ui->caps, i, 2, L"配对不会自动授予访问权限");
    }
    control(ui, L"STATIC", L"", 0, UI_GUIDE);
    control(ui, L"BUTTON", L"授权说明", WS_TABSTOP | BS_PUSHBUTTON, UI_HELP);
    control(ui, L"BUTTON", L"同步所选", WS_TABSTOP | BS_PUSHBUTTON, UI_SYNC);
    control(ui, L"BUTTON", L"上一页", WS_TABSTOP | BS_PUSHBUTTON, UI_PREVIOUS);
    control(ui, L"BUTTON", L"下一页", WS_TABSTOP | BS_PUSHBUTTON, UI_NEXT);
    control(ui, L"BUTTON", L"等待短信", WS_TABSTOP | BS_PUSHBUTTON, UI_WAIT);
    control(ui, L"STATIC", L"本次同步 · 内容仅保留在本窗口", 0, UI_PREVIEW_TITLE);
    ui->preview = control(ui, WC_LISTVIEWW, L"本次同步内容", list_style, UI_PREVIEW);
    column(ui->preview, 0, L"名称 / 发件人"); column(ui->preview, 1, L"电话号码"); column(ui->preview, 2, L"时间 / 摘要");
    ListView_SetExtendedListViewStyle(ui->preview, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowSubclass(ui->devices, themed_control, 1, (DWORD_PTR)ui);
    SetWindowSubclass(ui->caps, themed_control, 1, (DWORD_PTR)ui);
    SetWindowSubclass(ui->preview, themed_control, 1, (DWORD_PTR)ui);
    ui->log = control(ui, L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP, UI_LOG);
    control(ui, L"STATIC", L"就绪", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, UI_ACTIVITY);
    control(ui, L"BUTTON", L"查看诊断", WS_TABSTOP | BS_PUSHBUTTON, UI_DETAILS);
    control(ui, L"BUTTON", L"取消操作", WS_TABSTOP | BS_PUSHBUTTON, UI_CANCEL);
    ui->capability = 1;
    ListView_SetItemState(ui->caps, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    capability_guide(ui);
}

static LRESULT CALLBACK ui_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    phone_ui *ui = (phone_ui *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        ui->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        ui->dpi = GetDpiForWindow(window);
        cdx_phone_session_set_window(ui->session, (uintptr_t)window);
        create_controls(ui); system_fonts(ui); apply_theme(ui); layout(ui); controls(ui);
        SetTimer(window, 1, ui->smoke ? 50 : 1000, NULL);
        if (!ui->smoke) start_action(ui, "adapters");
        else {
            ui->smoke_ok = ui->adapter && ui->devices && ui->status && ui->log && ui->caps && ui->preview &&
                GetDlgItem(window, UI_UNPAIR) && !IsWindowEnabled(GetDlgItem(window, UI_UNPAIR)) &&
                !IsWindowEnabled(GetDlgItem(window, UI_PAIR)) && !IsWindowEnabled(GetDlgItem(window, UI_CHECK)) &&
                !IsWindowEnabled(GetDlgItem(window, UI_DISCONNECT)) && ListView_GetItemCount(ui->caps) == 5 &&
                FindResourceW(NULL, MAKEINTRESOURCEW(1), RT_MANIFEST) != NULL;
        }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) == UI_ADAPTER) { start_action(ui, "devices"); return 0; }
        switch (LOWORD(wparam)) {
        case UI_SCAN: start_action(ui, "discover"); break;
        case UI_PAIR: start_action(ui, "pair"); break;
        case UI_UNPAIR: start_action(ui, "unpair"); break;
        case UI_CONNECT: start_action(ui, "connect"); break;
        case UI_DISCONNECT: start_action(ui, "disconnect"); break;
        case UI_CANCEL: SetEvent(ui->cancel); append_log(ui, L"正在取消并回收当前操作。"); break;
        case UI_CHECK:
            if (!ui->busy) {
                ui->checking = 1; ui->next_check = 1; ui->capability = (int)check_order[0]; ui->offset = 0;
                ListView_SetItemState(ui->caps, ui->capability, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                capability_guide(ui); start_action(ui, capability_actions[ui->capability]);
            }
            break;
        case UI_SYNC: ui->offset = 0; start_action(ui, capability_actions[ui->capability]); break;
        case UI_PREVIOUS: ui->offset -= 20; start_action(ui, capability_actions[ui->capability]); break;
        case UI_NEXT: ui->offset += 20; start_action(ui, capability_actions[ui->capability]); break;
        case UI_WAIT: ui->offset = ui->sms_offset; start_action(ui, "wait_sms"); break;
        case UI_DETAILS:
            ui->details = !ui->details; SetWindowTextW(GetDlgItem(window, UI_DETAILS), ui->details ? L"返回内容" : L"查看诊断");
            layout(ui); break;
        case UI_HELP:
            MessageBoxW(window,
                L"在 iPhone 上完成授权\r\n\r\n1. 打开「设置 → 蓝牙」，点本机蓝牙名称旁的 ⓘ。\r\n"
                L"2. 检查“显示通知”或“共享系统通知”（若显示），并开启“同步通讯录”。\r\n"
                L"3. 若有“最近通话”和联系人分组，请选择需要共享的范围。\r\n"
                L"4. 回到此窗口点击“检查全部权限”。手机若弹出访问请求，请亲自确认。\r\n\r\n"
                L"选项会随 iOS 和连接服务变化；没有某个开关不代表已授权。\r\n"
                L"此版本支持短信列表变化、来电状态提醒，以及通过 ANCS 接收微信等应用的当前窗口通知。\r\n"
                L"应用通知走独立 BLE/GATT 通道，建链失败最多重试 5 次，本次最多 60 秒；可能收到通知中心已有的通知，不代表完整历史。\r\n"
                L"短信与通话记录只显示手机提供的分页数据，不保证完整历史；不会修改已读状态。\r\n"
                L"电脑无法直接替你开启手机权限。",
                L"Cordonix · iPhone 授权说明", MB_OK | MB_ICONINFORMATION);
            break;
        case UI_TRAY_OPEN: tray_open(ui); break;
        case UI_TRAY_DISCONNECT: if (!ui->busy && ui->connected_address[0]) start_action(ui, "disconnect"); break;
        case UI_TRAY_EXIT: ui->closing = 1; SendMessageW(window, WM_CLOSE, 0, 0); break;
        default: break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (header->code == NM_CUSTOMDRAW) return draw_control(ui, header);
        if (header->code == LVN_ITEMCHANGING && ui->busy && (header->idFrom == UI_DEVICES || header->idFrom == UI_CAPS)) {
            NMLISTVIEW *change = (NMLISTVIEW *)lparam;
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED)) return TRUE;
        }
        if (header->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *change = (NMLISTVIEW *)lparam;
            if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                if (header->idFrom == UI_DEVICES) select_device(ui);
                else if (header->idFrom == UI_CAPS && change->iItem >= 0 && change->iItem < 5) {
                    if (ui->capability != change->iItem) { ui->has_page = 0; ui->offset = 0; }
                    ui->capability = change->iItem; capability_guide(ui); controls(ui);
                }
            }
        }
        return 0;
    }
    case UI_TRAY_MESSAGE:
        if (lparam == WM_LBUTTONDBLCLK) tray_open(ui);
        else if (lparam == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            POINT point;
            if (menu) {
                AppendMenuW(menu, MF_STRING, UI_TRAY_OPEN, L"打开手机连接");
                AppendMenuW(menu, ui->connected_address[0] ? MF_STRING : MF_GRAYED, UI_TRAY_DISCONNECT, L"断开连接");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, UI_TRAY_EXIT, L"退出");
                GetCursorPos(&point); SetForegroundWindow(window);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, window, NULL);
                DestroyMenu(menu);
            }
        }
        return 0;
    case WM_MEASUREITEM:
        if (((MEASUREITEMSTRUCT *)lparam)->CtlID == UI_ADAPTER) {
            ((MEASUREITEMSTRUCT *)lparam)->itemHeight = (UINT)scale(ui, 28); return TRUE;
        }
        break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
        if (draw->CtlID == UI_ADAPTER) {
            RECT rect = draw->rcItem; wchar_t text[600] = L"";
            int selected = (draw->itemState & ODS_SELECTED) != 0;
            HBRUSH brush = selected ? GetSysColorBrush(COLOR_HIGHLIGHT) : ui->surface_brush;
            FillRect(draw->hDC, &rect, brush); SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ui->foreground);
            if (draw->itemID != (UINT)-1) SendMessageW(ui->adapter, CB_GETLBTEXT, draw->itemID, (LPARAM)text);
            rect.left += scale(ui, 8);
            DrawTextW(draw->hDC, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }
    case UI_DONE: complete_action(ui); return 0;
    case WM_TIMER:
        if (ui->smoke || GetTickCount64() - ui->started >= UI_MAX_LIFETIME_MS) { ui->closing = 1; SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        if (!ui->busy && ui->connected_address[0]) start_action(ui, "connection_status");
        return 0;
    case WM_SIZE:
        if (ui->adapter && wparam == SIZE_MINIMIZED && ui->tray_mode) { ShowWindow(window, SW_HIDE); return 0; }
        if (ui->adapter && wparam != SIZE_MINIMIZED) layout(ui);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = scale(ui, 1040); limits->ptMinTrackSize.y = scale(ui, 810); return 0;
    }
    case WM_DPICHANGED: {
        RECT *rect = (RECT *)lparam;
        ui->dpi = HIWORD(wparam); system_fonts(ui);
        SetWindowPos(window, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(ui); return 0;
    }
    case WM_SETTINGCHANGE: system_fonts(ui); apply_theme(ui); layout(ui); return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE: apply_theme(ui); return 0;
    case WM_ERASEBKGND: {
        RECT rect; GetClientRect(window, &rect);
        if (ui->background_brush) FillRect((HDC)wparam, &rect, ui->background_brush);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wparam;
        int id = GetDlgCtrlID((HWND)lparam), surface = message != WM_CTLCOLORSTATIC || (HWND)lparam == ui->log;
        COLORREF color = id == UI_TITLE || id == UI_DEVICE_TITLE || id == UI_PERMISSION_TITLE || id == UI_STATUS ? ui->foreground : ui->secondary;
        SetTextColor(dc, surface ? ui->foreground : color);
        SetBkColor(dc, surface ? ui->surface : ui->background);
        return (LRESULT)(surface ? ui->surface_brush : ui->background_brush);
    }
    case WM_CLOSE:
        if (ui->tray_mode && !ui->closing && !ui->smoke) { ShowWindow(window, SW_HIDE); return 0; }
        ui->closing = 1; KillTimer(window, 1);
        if (ui->worker) { SetEvent(ui->cancel); SetWindowTextW(ui->status, L"正在取消操作并断开连接…"); }
        else DestroyWindow(window);
        return 0;
    case WM_DESTROY: tray_remove(ui); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static int phone_ui_run(const wchar_t *target, int smoke, int tray_mode) {
    phone_ui *ui = calloc(1, sizeof(*ui));
    WNDCLASSEXW cls = {0};
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    DPI_AWARENESS_CONTEXT old_dpi;
    HRESULT runtime;
    MSG message;
    RECT bounds = {0,0,1120,800};
    unsigned messages = 0;
    int result = 0;
    if (!ui) return 2;
    old_dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    runtime = RoInitialize(RO_INIT_SINGLETHREADED);
    InitCommonControlsEx(&common);
    ui->session = cdx_phone_session_create(); ui->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    ui->smoke = smoke; ui->tray_mode = tray_mode; ui->started = GetTickCount64(); ui->dpi = GetDpiForSystem();
    if (!ui->session || !ui->cancel || wcslen(target) >= _countof(ui->target)) { result = 2; goto done; }
    wcscpy_s(ui->target, _countof(ui->target), target);
    cls.cbSize = sizeof(cls); cls.lpfnWndProc = ui_proc; cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW); cls.lpszClassName = L"CordonixPhoneWindow";
    if (!RegisterClassExW(&cls)) { result = 2; goto done; }
    bounds.right = scale(ui, bounds.right); bounds.bottom = scale(ui, bounds.bottom);
    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, ui->dpi);
    ui->window = CreateWindowExW(0, cls.lpszClassName, L"Cordonix · 手机连接",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        NULL, NULL, cls.hInstance, ui);
    if (!ui->window) { result = 2; goto done; }
    if (!smoke) {
        if (tray_mode && !tray_add(ui)) { result = 2; goto done; }
        ShowWindow(ui->window, tray_mode ? SW_HIDE : SW_SHOWNORMAL); UpdateWindow(ui->window);
    }
    while (messages++ < 1000000u && GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(ui->window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (ui->worker) {
        SetEvent(ui->cancel);
        if (WaitForSingleObject(ui->worker, 10000) != WAIT_OBJECT_0) ExitProcess(1);
        CloseHandle(ui->worker);
    }
    result = smoke && !ui->smoke_ok ? 1 : 0;
done:
    cdx_phone_session_destroy(ui->session);
    if (ui->cancel) CloseHandle(ui->cancel);
    if (ui->font) DeleteObject(ui->font);
    if (ui->heading) DeleteObject(ui->heading);
    if (ui->title) DeleteObject(ui->title);
    if (ui->background_brush) DeleteObject(ui->background_brush);
    if (ui->surface_brush) DeleteObject(ui->surface_brush);
    cdx_json_output_free(&ui->request); cdx_json_output_free(&ui->response);
    if (SUCCEEDED(runtime)) RoUninitialize();
    if (old_dpi) SetThreadDpiAwarenessContext(old_dpi);
    free(ui);
    return result;
}

int cdx_phone_ui(const wchar_t *target, int smoke) { return phone_ui_run(target, smoke, 0); }
int cdx_phone_ui_tray(const wchar_t *target) { return phone_ui_run(target, 0, 1); }
