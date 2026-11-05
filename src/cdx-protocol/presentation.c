#include "cdx/presentation.h"
#include <stdio.h>
#include <string.h>

static const char *text_arg(const cdx_json *json, int object, const char *key) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING ? json->nodes[node].string : "";
}

static unsigned number_arg(const cdx_json *json, int object, const char *key, unsigned fallback) {
    int node = cdx_json_get(json, object, key);
    double value = node >= 0 ? json->nodes[node].number : -1;
    return node >= 0 && json->nodes[node].type == CDX_JSON_NUMBER && value >= 0 && value <= 1000000000 &&
        value == (double)(unsigned)value ? (unsigned)value : fallback;
}

static int first_arg(const cdx_json *json, int object, const char *key) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_ARRAY ? json->nodes[node].first : -1;
}

static const char *arg_text(const cdx_json *json, int node) {
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING ? json->nodes[node].string : "";
}

static int separator(char value) { return value == '/' || value == '\\'; }

static void short_text(char *output, size_t capacity, const char *text, size_t length) {
    size_t input = 0, used = 0, step;
    /* The JSON parser established UTF-8 validity; keep truncation on code point boundaries. */
    for (step = 0; step < capacity && input < length; ++step) {
        unsigned char value = (unsigned char)text[input];
        size_t width = value < 0x80 ? 1u : value < 0xe0 ? 2u : value < 0xf0 ? 3u : 4u;
        if (input + width > length || used + width + 4u > capacity) break;
        if (value < 0x20 || value == 0x7f) output[used++] = ' ';
        else { memcpy(output + used, text + input, width); used += width; }
        input += width;
    }
    if (input < length) { memcpy(output + used, "...", 3); used += 3; }
    output[used] = 0;
}

static void short_path(char *output, size_t capacity, const char *path, unsigned parts) {
    size_t end = strlen(path), begin, walked;
    unsigned found = 0;
    if ((end == 1u && separator(path[0])) || (end == 3u && path[1] == ':' && separator(path[2]))) {
        short_text(output, capacity, path, end); return;
    }
    for (walked = 0; walked < 32768u && end > 3u && separator(path[end - 1u]); ++walked) --end;
    begin = end;
    for (walked = 0; walked < 32768u && begin; ++walked) {
        if (separator(path[begin - 1u]) && ++found == parts) break;
        --begin;
    }
    short_text(output, capacity, path + begin, end - begin);
}

static int named(const char *program, const char *name) {
    size_t i, length = strlen(name);
    for (i = 0; i < length && i < 64u; ++i) {
        unsigned char value = (unsigned char)program[i];
        if (value >= 'A' && value <= 'Z') value = (unsigned char)(value + ('a' - 'A'));
        if (value != (unsigned char)name[i]) return 0;
    }
    return i == length && (!program[i] || !strcmp(program + i, ".exe") || !strcmp(program + i, ".EXE"));
}

static const char *git_command(const cdx_json *json, int node, char *context, size_t capacity) {
    unsigned count;
    for (count = 0; node >= 0 && count < 32u; ++count) {
        const char *arg = arg_text(json, node);
        if (*arg != '-') return arg;
        if (!strcmp(arg, "-C") || !strcmp(arg, "-c") || !strcmp(arg, "--git-dir") || !strcmp(arg, "--work-tree")) {
            node = json->nodes[node].next;
            if (node < 0) return "";
            if (!strcmp(arg, "-C")) short_path(context, capacity, arg_text(json, node), 1);
        } else if (strcmp(arg, "--no-optional-locks") && strcmp(arg, "--no-pager") &&
                   strcmp(arg, "--literal-pathspecs") && strcmp(arg, "--bare")) return "";
        node = json->nodes[node].next;
    }
    return "";
}

static const char *command_action(const char *command, int dotnet) {
    static const struct { const char *command, *action; } git[] = {
        {"status", "查看仓库修改状态"}, {"diff", "查看代码差异"}, {"log", "查看提交历史"},
        {"show", "查看提交或文件内容"}, {"rev-parse", "查询仓库或提交标识"}, {"ls-remote", "查询远端引用"},
        {"check-ignore", "检查忽略规则"}, {"fetch", "获取远端提交"}, {"pull", "拉取并整合远端提交"},
        {"push", "推送提交到远端"}, {"add", "暂存文件改动"}, {"commit", "创建 Git 提交"}
    };
    static const struct { const char *command, *action; } dotnet_actions[] = {
        {"build", "构建 .NET 项目"}, {"test", "运行 .NET 测试"}, {"restore", "还原 .NET 依赖"},
        {"publish", "生成 .NET 发布产物"}, {"run", "运行 .NET 项目"}
    };
    size_t i;
    if (dotnet) {
        for (i = 0; i < sizeof(dotnet_actions) / sizeof(dotnet_actions[0]); ++i)
            if (!strcmp(command, dotnet_actions[i].command)) return dotnet_actions[i].action;
    } else {
        for (i = 0; i < sizeof(git) / sizeof(git[0]); ++i)
            if (!strcmp(command, git[i].command)) return git[i].action;
    }
    return "运行程序";
}

static void program_summary(const cdx_json *json, int arguments, char *summary) {
    char program[72], context[96], target[160] = "";
    int first = first_arg(json, arguments, "args"), node = first;
    const char *arg = arg_text(json, first), *action = "运行程序";
    unsigned count;
    short_path(program, sizeof(program), text_arg(json, arguments, "program"), 1);
    short_path(context, sizeof(context), text_arg(json, arguments, "cwd"), 1);
    if (!strcmp(arg, "--version")) action = "查询程序版本";
    else if (!strcmp(arg, "--help") || !strcmp(arg, "-h") || !strcmp(arg, "/?")) action = "查看程序帮助";
    else if (named(program, "git")) action = command_action(git_command(json, first, context, sizeof(context)), 0);
    else if (named(program, "dotnet")) action = command_action(arg, 1);
    else if (named(program, "where")) {
        char second[72] = "";
        action = "查找程序路径";
        if (!strncmp(arg, "$PATH:", 6)) arg += 6;
        short_text(target, 72, arg, strlen(arg));
        if (first >= 0 && json->nodes[first].next >= 0) {
            arg = arg_text(json, json->nodes[first].next);
            if (!strncmp(arg, "$PATH:", 6)) arg += 6;
            short_text(second, sizeof(second), arg, strlen(arg));
            (void)snprintf(target + strlen(target), sizeof(target) - strlen(target), "、%s", second);
        }
    } else if (named(program, "rg")) action = !strcmp(arg, "--files") ? "列出文件路径" : "搜索文件文本";
    else if (named(program, "cl") || named(program, "gcc") || named(program, "clang") || named(program, "cc")) action = "编译 C/C++ 源码";
    else if (named(program, "cmake")) action = !strcmp(arg, "--build") ? "构建 CMake 项目" : "运行 CMake";
    else if (named(program, "pwsh")) {
        action = "运行 PowerShell 脚本";
        for (count = 0; node >= 0 && count < 32u; ++count, node = json->nodes[node].next) {
            if (!strcmp(arg_text(json, node), "-File") || !strcmp(arg_text(json, node), "-file")) {
                short_path(target, sizeof(target), arg_text(json, json->nodes[node].next), 2); break;
            }
        }
    } else if (named(program, "node") || named(program, "python") || named(program, "python3")) {
        action = "运行脚本或模块";
        if (*arg && *arg != '-') short_path(target, sizeof(target), arg, 2);
        else if (!strcmp(arg, "-m")) short_path(target, sizeof(target), arg_text(json, first >= 0 ? json->nodes[first].next : -1), 1);
    }
    (void)snprintf(summary, CDX_CALL_SUMMARY_BYTES, "%s：%s%s%s%s%s", *program ? program : "未指定程序", action,
        *target ? " · " : "", target, *context ? " · " : "", context);
}

void cdx_summarize_call(const char *operation, const cdx_json *json, int arguments,
                        char summary[CDX_CALL_SUMMARY_BYTES]) {
    char target[192], range[96] = "", fallback[80];
    const char *action;
    unsigned first, count;
    if (!strcmp(operation, "execute_program")) { program_summary(json, arguments, summary); return; }
    short_text(fallback, sizeof(fallback), operation, strlen(operation)); action = fallback;
    short_path(target, sizeof(target), text_arg(json, arguments, "path"), 2);
    if (!strcmp(operation, "read_file") || !strcmp(operation, "workspace_read")) {
        int raw = cdx_json_get(json, arguments, "raw");
        action = !strcmp(operation, "read_file") ? "读取文件" : "读取内存文档";
        first = number_arg(json, arguments, "start_line", 1);
        count = number_arg(json, arguments, "line_count", 200);
        if (raw >= 0 && json->nodes[raw].type == CDX_JSON_BOOL && json->nodes[raw].boolean) (void)snprintf(range, sizeof(range), " · 全文");
        else if (cdx_json_get(json, arguments, "tail_lines") >= 0) (void)snprintf(range, sizeof(range), " · 末尾 %u 行", number_arg(json, arguments, "tail_lines", 0));
        else if (!strcmp(operation, "workspace_read") && cdx_json_get(json, arguments, "start_line") < 0 && cdx_json_get(json, arguments, "line_count") < 0)
            (void)snprintf(range, sizeof(range), " · UTF-8 字节 %u 起，最多 %u 字节", number_arg(json, arguments, "offset", 0), number_arg(json, arguments, "limit", 32768));
        else (void)snprintf(range, sizeof(range), " · 第 %u～%u 行", first, first + (count ? count - 1u : 0u));
    } else if (!strcmp(operation, "list_directory")) action = "列出目录";
    else if (!strcmp(operation, "resolve_path")) action = "确认路径是否存在";
    else if (!strcmp(operation, "git_status")) { action = "查看 Git 仓库状态"; short_path(target, sizeof(target), text_arg(json, arguments, "cwd"), 1); }
    else if (!strcmp(operation, "search_text")) {
        char pattern[64];
        const char *value = arg_text(json, first_arg(json, arguments, "patterns"));
        action = "搜索文件文本"; short_path(target, sizeof(target), arg_text(json, first_arg(json, arguments, "roots")), 2);
        short_text(pattern, sizeof(pattern), value, strlen(value));
        if (*pattern) (void)snprintf(range, sizeof(range), " · %s", pattern);
    }
    else if (!strcmp(operation, "create_file")) action = "创建新文件";
    else if (!strcmp(operation, "create_directory")) action = "创建目录";
    else if (!strcmp(operation, "workspace_open")) action = "打开文件到内存";
    else if (!strcmp(operation, "workspace_edit")) action = "编辑内存文档";
    else if (!strcmp(operation, "workspace_commit")) action = "提交内存文档到磁盘";
    else if (!strcmp(operation, "workspace_close")) action = "关闭内存文档";
    else if (!strcmp(operation, "workspace_status")) action = "查看内存文档状态";
    else if (!strcmp(operation, "workspace_search")) action = "搜索内存文档";
    else if (!strcmp(operation, "workspace_convert_encoding")) action = "转换内存文档编码";
    else if (!strcmp(operation, "verify_encoding")) action = "验证文件编码";
    else if (!strcmp(operation, "query_processes")) action = "查询 Windows 进程";
    else if (!strcmp(operation, "screenshot")) action = "截取窗口或屏幕";
    else if (!strcmp(operation, "tool_info")) action = "查看 Cordonix 版本和会话状态";
    else if (!strcmp(operation, "ssh")) action = "远程 SSH 操作";
    if (!strcmp(operation, "workspace_edit") || !strcmp(operation, "workspace_convert_encoding")) {
        int dry_run = cdx_json_get(json, arguments, "dry_run");
        if (dry_run < 0 || (json->nodes[dry_run].type == CDX_JSON_BOOL && json->nodes[dry_run].boolean))
            (void)snprintf(range, sizeof(range), "（预览）");
    }
    if (!*target && cdx_json_get(json, arguments, "document_id") >= 0)
        (void)snprintf(target, sizeof(target), "#%u", number_arg(json, arguments, "document_id", 0));
    (void)snprintf(summary, CDX_CALL_SUMMARY_BYTES, "%s%s%s%s", action, *target ? " · " : "", target, range);
}

int cdx_summary_progress(const cdx_json *json, int params, const char *summary, cdx_json_output *output) {
    int token = cdx_json_get(json, cdx_json_get(json, params, "_meta"), "progressToken");
    if (token < 0 || (json->nodes[token].type != CDX_JSON_STRING && json->nodes[token].type != CDX_JSON_NUMBER)) return 0;
    cdx_json_raw(output, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{\"progressToken\":");
    if (json->nodes[token].type == CDX_JSON_STRING) cdx_json_quote(output, json->nodes[token].string, strlen(json->nodes[token].string));
    else cdx_json_rawn(output, json->nodes[token].number_text, json->nodes[token].number_length);
    cdx_json_raw(output, ",\"progress\":0,\"message\":"); cdx_json_quote(output, summary, strlen(summary)); cdx_json_raw(output, "}}");
    return 1;
}

void cdx_summary_metadata(const char *summary, cdx_json_output *output) {
    cdx_json_raw(output, ",\"_meta\":{\"" CDX_CALL_SUMMARY_META "\":");
    cdx_json_quote(output, summary, strlen(summary)); cdx_json_raw(output, "}");
}
