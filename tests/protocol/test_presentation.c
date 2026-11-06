#include "cdx/presentation.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned passed, total;
static void check(const char *name, int valid) {
    ++total; if (valid) ++passed;
    (void)printf("%s: %s\n", name, valid ? "PASS" : "FAIL");
}

int main(int argc, char **argv) {
    static const struct { const char *operation, *arguments, *expected; } cases[] = {
        {"execute_program", "{\"program\":\"C:/Git/git.exe\",\"args\":[\"--no-optional-locks\",\"status\"],\"cwd\":\"D:/GitHub/Cordonix\"}", "git.exe：查看仓库修改状态 · Cordonix"},
        {"execute_program", "{\"program\":\"/usr/bin/git\",\"args\":[\"-C\",\"/src/other\",\"diff\"],\"cwd\":\"/src/Cordonix\"}", "git：查看代码差异 · other"},
        {"execute_program", "{\"program\":\"/usr/bin/git\",\"args\":[\"push\"],\"cwd\":\"/src/Cordonix\"}", "git：推送提交到远端 · Cordonix"},
        {"execute_program", "{\"program\":\"/usr/bin/git\",\"args\":[\"config\",\"alias.push\",\"status\"]}", "git：运行程序"},
        {"execute_program", "{\"program\":\"/bin/git\",\"args\":[\"-c\",\"alias.status=push\",\"status\"]}", "git：查看仓库修改状态"},
        {"execute_program", "{\"program\":\"C:/Windows/where.exe\",\"args\":[\"$PATH:git.exe\",\"$PATH:rg.exe\"]}", "where.exe：查找程序路径 · git.exe、rg.exe"},
        {"execute_program", "{\"program\":\"/bin/rg\",\"args\":[\"--files\"]}", "rg：列出文件路径"},
        {"execute_program", "{\"program\":\"/bin/rg\",\"args\":[\"-n\",\"hello\"]}", "rg：搜索文件文本"},
        {"execute_program", "{\"program\":\"/bin/git\",\"args\":[\"--version\"]}", "git：查询程序版本"},
        {"execute_program", "{\"program\":\"/bin/dotnet\",\"args\":[\"test\"]}", "dotnet：运行 .NET 测试"},
        {"execute_program", "{\"program\":\"C:/VS/cl.exe\",\"args\":[\"/std:c11\",\"main.c\"]}", "cl.exe：编译 C/C++ 源码"},
        {"execute_program", "{\"program\":\"C:/PS/pwsh.exe\",\"args\":[\"-NoProfile\",\"-File\",\"D:/project/eng/test.ps1\"]}", "pwsh.exe：运行 PowerShell 脚本 · eng/test.ps1"},
        {"execute_program", "{\"program\":\"/bin/python3\",\"args\":[\"-m\",\"pytest\"]}", "python3：运行脚本或模块 · pytest"},
        {"execute_program", "{\"program\":\"/bin/custom\",\"args\":[\"secret-value\"],\"stdin\":\"hidden\",\"env\":{\"TOKEN\":\"hidden\"}}", "custom：运行程序"},
        {"read_file", "{\"path\":\"D:/TOLNSD/Services/DingTalkOptions.cs\",\"start_line\":61,\"line_count\":30}", "读取文件 · Services/DingTalkOptions.cs · 第 61～90 行"},
        {"read_file", "{\"path\":\"/src/中文.cs\",\"raw\":true}", "读取文件 · src/中文.cs · 全文"},
        {"read_file", "{\"path\":\"/src/a.cs\",\"tail_lines\":55}", "读取文件 · src/a.cs · 末尾 55 行"},
        {"list_directory", "{\"path\":\"D:/GitHub/Cordonix/eng/\"}", "列出目录 · Cordonix/eng"},
        {"resolve_path", "{\"path\":\"D:/GitHub/Cordonix\"}", "确认路径是否存在 · GitHub/Cordonix"},
        {"create_directory", "{\"path\":\"D:/Temp/query 目录\",\"exist_ok\":true}", "创建目录 · Temp/query 目录"},
        {"hash_file", "{\"path\":\"/tmp/source.bin\"}", "计算文件 SHA-256 · tmp/source.bin"},
        {"copy_file", "{\"source\":\"/tmp/source.bin\",\"destination\":\"/out/copy.bin\"}", "复制文件 · tmp/source.bin"},
        {"move_file", "{\"source\":\"/tmp/source.bin\",\"destination\":\"/out/moved.bin\"}", "移动文件 · tmp/source.bin"},
        {"data_query", "{\"path\":\"/tmp/input.csv\"}", "查询结构化数据 · tmp/input.csv"},
        {"find_files", "{\"roots\":[\"/src/project\"]}", "查找文件 · src/project"},
        {"git_status", "{\"cwd\":\"/src/Cordonix\"}", "查看 Git 仓库状态 · Cordonix"},
        {"git_status", "{\"cwd\":\"/\"}", "查看 Git 仓库状态 · /"},
        {"git_status", "{\"cwd\":\"C:/\"}", "查看 Git 仓库状态 · C:/"},
        {"workspace_read", "{\"document_id\":3,\"offset\":100,\"limit\":200}", "读取内存文档 · #3 · UTF-8 字节 100 起，最多 200 字节"},
        {"execute_program", "{\"program\":null,\"args\":42,\"cwd\":false}", "未指定程序：运行程序"},
        {"read_file", "{\"path\":\"/src/a\\n$(literal).cs\",\"line_count\":1}", "读取文件 · src/a $(literal).cs · 第 1～1 行"}
    };
    time_t deadline = time(NULL) + 5;
    size_t i, limit = argc > 1 && !strcmp(argv[1], "--pilot") ? 1u : sizeof(cases) / sizeof(cases[0]);
    for (i = 0; i < limit && time(NULL) < deadline; ++i) {
        cdx_json json;
        char summary[CDX_CALL_SUMMARY_BYTES];
        int valid = cdx_json_parse(&json, cases[i].arguments, strlen(cases[i].arguments));
        if (valid) cdx_summarize_call(cases[i].operation, &json, 0, summary);
        check(cases[i].expected, valid && !strcmp(summary, cases[i].expected));
        cdx_json_free(&json);
    }
    check("summary cases finish within the fixed deadline", i == limit);
    if (limit > 1u) {
        cdx_json json = {0}, parsed = {0};
        cdx_json_output output;
        char path[1200], input[1400], summary[CDX_CALL_SUMMARY_BYTES];
        for (i = 0; i < 300u && time(NULL) < deadline; ++i) memcpy(path + i * 3u, "中", 3);
        path[i * 3u] = 0;
        (void)snprintf(input, sizeof(input), "{\"path\":\"/src/%s中文.cs\"}", path);
        (void)cdx_json_parse(&json, input, strlen(input));
        cdx_summarize_call("read_file", &json, 0, summary);
        cdx_json_output_init(&output); cdx_json_raw(&output, "{\"content\":[]");
        cdx_summary_metadata(summary, &output); cdx_json_raw(&output, "}");
        check("bounded summary metadata remains strict UTF8 JSON", strlen(summary) < CDX_CALL_SUMMARY_BYTES &&
            !output.failed && cdx_json_parse(&parsed, output.data, output.size));
        cdx_json_free(&parsed); cdx_json_free(&json); cdx_json_output_free(&output);
    }
    (void)printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
