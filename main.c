#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/rtc.h"
#include "lib/MPU6050.h"
#include "lib/buzzer.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "ff.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "my_debug.h"
#include "rtc.h"
#include "sd_card.h"

#define MPU_PORT i2c0 
#define MPU_SDA 0
#define MPU_SCL 1
#define DISP_PORT i2c1
#define DISP_SDA 14
#define DISP_SCL 15
#define DISP_ADDRESS 0x3C
#define GREEN_LED 11
#define BLUE_LED 12
#define RED_LED 13
#define BUTTON_A 5
#define BUTTON_B 6
#define DEBOUNCE_DELAY 200
#define ALARM_ERROR_DURATION 500
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define STATUS_BAR_HEIGHT 10
#define MAX_TEXT_LEN 32

ssd1306_t ssd;

uint32_t last_time_a = 0;
uint32_t last_time_b = 0;
bool sd_mounted = false;

mpu6050_t mpu;
float accel[3], gyro[3];

static bool logger_enabled;
static const uint32_t period = 1000;
static absolute_time_t next_log_time;

static char filename[20] = "data.csv";
static char filename_base[20] = "med_imu";
static int med_count = 1;

static bool capture_in_progress = false;
static bool should_stop_capture = false;

static int current_menu_page = 0;
static const int MAX_MENU_PAGES = 3;
static bool alarm_enabled = false;

static char status_message[MAX_TEXT_LEN] = "";
static absolute_time_t status_message_timeout = 0;

void play_error_alarm() {
    gpio_put(RED_LED, true);
    set_buzzer_tone(BUZZER_A, A4);
    sleep_ms(ALARM_ERROR_DURATION);
    stop_buzzer(BUZZER_A);
    gpio_put(RED_LED, false);
    sleep_ms(ALARM_ERROR_DURATION);
    gpio_put(RED_LED, true);
    set_buzzer_tone(BUZZER_A, A4);
    sleep_ms(ALARM_ERROR_DURATION);
    stop_buzzer(BUZZER_A);
    gpio_put(RED_LED, false);
}

void disp_init() {
    i2c_init(DISP_PORT, 400 * 1000);
    gpio_set_function(DISP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DISP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DISP_SDA);
    gpio_pull_up(DISP_SCL);
    
    ssd1306_init(&ssd, SCREEN_WIDTH, SCREEN_HEIGHT, false, DISP_ADDRESS, DISP_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "IMU Logger v1.0", 1, 5);
    ssd1306_draw_string(&ssd, "Initializing...", 1, 20);
    ssd1306_send_data(&ssd);
    sleep_ms(2000);
}

void display_status_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(status_message, MAX_TEXT_LEN, format, args);
    va_end(args);
    status_message_timeout = make_timeout_time_ms(3000);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, status_message, 1, 20);
    ssd1306_send_data(&ssd);
}

static void draw_status_bar() {
    char status[20];
    snprintf(status, sizeof(status), "%s | %s", 
             sd_mounted ? "SD:OK" : "SD:ERR", 
             alarm_enabled ? "ALM:ON" : "ALM:OFF");
    ssd1306_draw_string(&ssd, status, 1, SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
}

void display_menu_page(int page) {
    ssd1306_fill(&ssd, false);

    if (status_message[0] != '\0' && absolute_time_diff_us(get_absolute_time(), status_message_timeout) > 0) {
        status_message[0] = '\0';
    }
    if (status_message[0] != '\0') {
        ssd1306_draw_string(&ssd, status_message, 1, 20);
        ssd1306_send_data(&ssd);
        return;
    }

    switch (page) {
        case 0:
            ssd1306_draw_string(&ssd, "> Main Menu", 1, 5);
            ssd1306_draw_string(&ssd, "1. SD Card Stat", 4, 20);
            ssd1306_draw_string(&ssd, "2. IMU Data", 4, 30);
            ssd1306_draw_string(&ssd, "3. Config", 4, 40);
            break;
        case 1:
            ssd1306_draw_string(&ssd, "> SD Card Stat", 1, 5);
            ssd1306_draw_string(&ssd, sd_mounted ? "Mounted" : "Not Mounted", 10, 20);
            ssd1306_draw_string(&ssd, "File:", 10, 30);
            ssd1306_draw_string(&ssd, filename, 10, 40);
            break;
        case 2:
            ssd1306_draw_string(&ssd, "> IMU Data", 1, 5);
            char accel_str[32];
            snprintf(accel_str, sizeof(accel_str), "A: %6.2f %6.2f %6.2f", 
                     accel[0], accel[1], accel[2]);
            ssd1306_draw_string(&ssd, accel_str, 10, 20);
            char gyro_str[32];
            snprintf(gyro_str, sizeof(gyro_str), "G: %6.2f %6.2f %6.2f", 
                     gyro[0], gyro[1], gyro[2]);
            ssd1306_draw_string(&ssd, gyro_str, 10, 40);
            break;
        default:
            ssd1306_draw_string(&ssd, "Invalid Page", 1, 20);
            break;
    }

    draw_status_bar();
    ssd1306_send_data(&ssd);
}

void toggle_alarm() {
    alarm_enabled = !alarm_enabled;
    
    if (alarm_enabled) {
        set_buzzer_tone(BUZZER_A, C5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, E5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, G5);
        sleep_ms(200);
        stop_buzzer(BUZZER_A);
        display_status_message("Alarm Enabled");
    } else {
        set_buzzer_tone(BUZZER_A, G5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, E5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, C5);
        sleep_ms(200);
        stop_buzzer(BUZZER_A);
        display_status_message("Alarm Disabled");
    }
}

static sd_card_t *sd_get_by_name(const char *const name) {
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return sd_get_by_num(i);
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}

static FATFS *sd_get_fs_by_name(const char *name) {
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return &sd_get_by_num(i)->fatfs;
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}

static void run_setrtc() {
    const char *dateStr = strtok(NULL, " ");
    if (!dateStr) {
        display_status_message("Missing Date");
        return;
    }
    int date = atoi(dateStr);

    const char *monthStr = strtok(NULL, " ");
    if (!monthStr) {
        display_status_message("Missing Month");
        return;
    }
    int month = atoi(monthStr);

    const char *yearStr = strtok(NULL, " ");
    if (!yearStr) {
        display_status_message("Missing Year");
        return;
    }
    int year = atoi(yearStr) + 2000;

    const char *hourStr = strtok(NULL, " ");
    if (!hourStr) {
        display_status_message("Missing Hour");
        return;
    }
    int hour = atoi(hourStr);

    const char *minStr = strtok(NULL, " ");
    if (!minStr) {
        display_status_message("Missing Minute");
        return;
    }
    int min = atoi(minStr);

    const char *secStr = strtok(NULL, " ");
    if (!secStr) {
        display_status_message("Missing Second");
        return;
    }
    int sec = atoi(secStr);

    datetime_t t = {
        .year = (int16_t)year,
        .month = (int8_t)month,
        .day = (int8_t)date,
        .dotw = 0,
        .hour = (int8_t)hour,
        .min = (int8_t)min,
        .sec = (int8_t)sec
    };
    rtc_set_datetime(&t);
    display_status_message("RTC Set");
}

static void run_format() {
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs) {
        display_status_message("Unknown Drive");
        return;
    }
    FRESULT fr = f_mkfs(arg1, 0, 0, FF_MAX_SS * 2);
    if (FR_OK != fr) {
        display_status_message("Format Error");
        return;
    }
    display_status_message("SD Formatted");
}

static void run_mount() {
    gpio_put(RED_LED, true);
    gpio_put(GREEN_LED, true);
    sleep_ms(500);
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs) {
        play_error_alarm();
        display_status_message("Unknown Drive");
        return;
    }
    FRESULT fr = f_mount(p_fs, arg1, 1);
    if (FR_OK != fr) {
        play_error_alarm();
        display_status_message("Mount Error");
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = true;
    sd_mounted = true;
    gpio_put(RED_LED, false);
    gpio_put(GREEN_LED, true);
    display_status_message("SD Mounted");
}

static void run_unmount() {
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs) {
        display_status_message("Unknown Drive");
        return;
    }
    FRESULT fr = f_unmount(arg1);
    if (FR_OK != fr) {
        display_status_message("Unmount Error");
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = false;
    pSD->m_Status |= STA_NOINIT;
    sd_mounted = false;
    gpio_put(GREEN_LED, false);
    gpio_put(BLUE_LED, false);
    gpio_put(RED_LED, false);
    display_status_message("SD Unmounted");
}

static void run_getfree() {
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs) {
        display_status_message("Unknown Drive");
        return;
    }
    FRESULT fr = f_getfree(arg1, &fre_clust, &p_fs);
    if (FR_OK != fr) {
        display_status_message("Getfree Error");
        return;
    }
    tot_sect = (p_fs->n_fatent - 2) * p_fs->csize;
    fre_sect = fre_clust * p_fs->csize;
    char msg[32];
    snprintf(msg, sizeof(msg), "%lu/%lu KiB Free", fre_sect / 2, tot_sect / 2);
    display_status_message("%s", msg);
}

static void run_ls() {
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = "";
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr;
    char const *p_dir;
    if (arg1[0]) {
        p_dir = arg1;
    } else {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr) {
            display_status_message("Dir List Error");
            return;
        }
        p_dir = cwdbuf;
    }
    DIR dj;
    FILINFO fno;
    memset(&dj, 0, sizeof dj);
    memset(&fno, 0, sizeof fno);
    fr = f_findfirst(&dj, &fno, p_dir, "*");
    if (FR_OK != fr) {
        display_status_message("Dir List Error");
        return;
    }
    while (fr == FR_OK && fno.fname[0]) {
        const char *pcWritableFile = "writable file",
                   *pcReadOnlyFile = "read only file",
                   *pcDirectory = "directory";
        const char *pcAttrib = (fno.fattrib & AM_DIR) ? pcDirectory :
                               (fno.fattrib & AM_RDO) ? pcReadOnlyFile : pcWritableFile;
        printf("%s [%s] [size=%llu]\n", fno.fname, pcAttrib, fno.fsize);
        fr = f_findnext(&dj, &fno);
    }
    f_closedir(&dj);
    display_status_message("Dir Listed");
}

static void run_cat() {
    char *arg1 = strtok(NULL, " ");
    if (!arg1) {
        display_status_message("Missing Filename");
        return;
    }
    FIL fil;
    FRESULT fr = f_open(&fil, arg1, FA_READ);
    if (FR_OK != fr) {
        display_status_message("File Open Error");
        return;
    }
    char buf[256];
    while (f_gets(buf, sizeof buf, &fil)) {
        printf("%s", buf);
    }
    fr = f_close(&fil);
    if (FR_OK != fr)
        display_status_message("File Close Error");
    else
        display_status_message("File Read");
}

void read_file(const char *filename) {
    gpio_put(BLUE_LED, true);
    gpio_put(GREEN_LED, false);
    gpio_put(RED_LED, false);
    FIL file;
    FRESULT res = f_open(&file, filename, FA_READ);
    if (res != FR_OK) {
        display_status_message("File Open Error");
        return;
    }
    char buffer[128];
    UINT br;
    printf("Content of %s:\n", filename);
    while (f_read(&file, buffer, sizeof(buffer) - 1, &br) == FR_OK && br > 0) {
        buffer[br] = '\0';
        printf("%s", buffer);
    }
    f_close(&file);
    gpio_put(BLUE_LED, false);
    gpio_put(GREEN_LED, true);
    gpio_put(RED_LED, false);
    display_status_message("File Read: %s", filename);
}

static void run_help() {
    printf("\nAvailable Commands:\n\n");
    printf("a: Mount SD card\n");
    printf("b: Unmount SD card\n");
    printf("c: List files\n");
    printf("d: Show file content\n");
    printf("e: Get free space\n");
    printf("f: Capture IMU data\n");
    printf("g: Format SD card\n");
    printf("h: Show help\n");
    display_status_message("Help Displayed");
}

typedef void (*p_fn_t)();
typedef struct {
    char const *const command;
    p_fn_t const function;
    char const *const help;
} cmd_def_t;

static cmd_def_t cmds[] = {
    {"setrtc", run_setrtc, "setrtc <DD> <MM> <YY> <hh> <mm> <ss>: Set Real Time Clock"},
    {"format", run_format, "format [<drive#:>]: Format SD card"},
    {"mount", run_mount, "mount [<drive#:>]: Mount SD card"},
    {"unmount", run_unmount, "unmount <drive#:>: Unmount SD card"},
    {"getfree", run_getfree, "getfree [<drive#:>]: Free space"},
    {"ls", run_ls, "ls: List files"},
    {"cat", run_cat, "cat <filename>: Show file content"},
    {"help", run_help, "help: Show available commands"}
};

static void process_stdio(int cRxedChar) {
    static char cmd[256];
    static size_t ix;

    if (!isprint(cRxedChar) && !isspace(cRxedChar) && '\r' != cRxedChar &&
        '\b' != cRxedChar && cRxedChar != (char)127)
        return;
    printf("%c", cRxedChar);
    stdio_flush();
    if (cRxedChar == '\r') {
        printf("%c", '\n');
        stdio_flush();

        if (!strnlen(cmd, sizeof cmd)) {
            printf("> ");
            stdio_flush();
            return;
        }
        char *cmdn = strtok(cmd, " ");
        if (cmdn) {
            size_t i;
            for (i = 0; i < count_of(cmds); ++i) {
                if (0 == strcmp(cmds[i].command, cmdn)) {
                    (*cmds[i].function)();
                    break;
                }
            }
            if (count_of(cmds) == i)
                display_status_message("Unknown Command");
        }
        ix = 0;
        memset(cmd, 0, sizeof cmd);
        printf("\n> ");
        stdio_flush();
    } else {
        if (cRxedChar == '\b' || cRxedChar == (char)127) {
            if (ix > 0) {
                ix--;
                cmd[ix] = '\0';
            }
        } else {
            if (ix < sizeof cmd - 1) {
                cmd[ix] = cRxedChar;
                ix++;
            }
        }
    }
}

void capture_imu_data_and_save() {
    if (capture_in_progress) {
        should_stop_capture = true;
        return;
    }

    capture_in_progress = true;
    should_stop_capture = false;
    
    gpio_put(RED_LED, true);
    gpio_put(GREEN_LED, false);
    display_status_message("Capturing Data...");
    
    const int total_amostras = 1000;
    const int intervalo_ms = 100;
    int tempo_total_s = (total_amostras * intervalo_ms) / 1000;
    printf("Estimated time: %d seconds\n", tempo_total_s);
    
    snprintf(filename, sizeof(filename), "%s%d.csv", filename_base, med_count);
    med_count++;
    
    FIL file;
    FRESULT res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        play_error_alarm();
        display_status_message("File Open Error");
        capture_in_progress = false;
        return;
    }
    
    UINT bw;
    absolute_time_t start_time = get_absolute_time();
    char header[] = "Sample,Accel X,Accel Y,Accel Z,Gyro X,Gyro Y,Gyro Z,Time (s)\n";
    res = f_write(&file, header, strlen(header), &bw);
    
    for (int i = 0; i < total_amostras && !should_stop_capture; i++) {
        mpu6050_read_calibrated(&mpu, accel, gyro);
        char buffer[100];
        uint32_t time = to_ms_since_boot(get_absolute_time());
        char buffer_disp[50];
        snprintf(buffer_disp, sizeof(buffer_disp), "Sample: %d Time: %.2f", i+1, (float)time/1000);
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Capturing...", 1, 5);
        ssd1306_draw_string(&ssd, buffer_disp, 1, 20);
        draw_status_bar();
        ssd1306_send_data(&ssd);
        snprintf(buffer, sizeof(buffer), "%d,%f,%f,%f,%f,%f,%f,%f\n", 
                 i + 1, accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2], (float)time/1000);
        res = f_write(&file, buffer, strlen(buffer), &bw);
        
        if (res != FR_OK) {
            play_error_alarm();
            display_status_message("Write Error");
            break;
        }
        
        if ((i + 1) % 50 == 0 || i == 0) {
            absolute_time_t now = get_absolute_time();
            int elapsed_ms = to_ms_since_boot(now) - to_ms_since_boot(start_time);
            int remaining_ms = (total_amostras - (i + 1)) * intervalo_ms;
            int remaining_s = remaining_ms / 1000;
            printf("Sample %d/%d - Time remaining: %d seconds\n", i + 1, total_amostras, remaining_s);
        }
        
        sleep_ms(intervalo_ms);
    }
    
    f_close(&file);
    
    if (should_stop_capture) {
        display_status_message("Capture Stopped");
    } else {
        display_status_message("Data Saved: %s", filename);
    }
    
    gpio_put(RED_LED, false);
    gpio_put(GREEN_LED, true);
    capture_in_progress = false;
    should_stop_capture = false;
}

void check_system_errors() {
    static bool last_sd_state = false;
    static bool sd_error_shown = false;
    
    bool current_sd_state = sd_mounted;
    if (last_sd_state != current_sd_state) {
        last_sd_state = current_sd_state;
        sd_error_shown = false;
    }
    
    if (sd_mounted && !sd_get_by_num(0)->mounted && !sd_error_shown) {
        play_error_alarm();
        display_status_message("SD Disconnected!");
        sd_error_shown = true;
    }
}

void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (gpio == BUTTON_A && (current_time - last_time_a >= DEBOUNCE_DELAY)) {
        current_menu_page = (current_menu_page + 1) % MAX_MENU_PAGES;
        display_menu_page(current_menu_page);
        last_time_a = current_time;
    } 
    else if (gpio == BUTTON_B && (current_time - last_time_b >= DEBOUNCE_DELAY)) {
        reset_usb_boot(0, 0);
        last_time_b = current_time;
    }
}

int main() {
    stdio_init_all();

    gpio_init(BUTTON_A);
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled(BUTTON_B, GPIO_IRQ_EDGE_FALL, true);

    init_buzzer_pwm(BUZZER_A);

    gpio_init(GREEN_LED);
    gpio_init(BLUE_LED);
    gpio_init(RED_LED);
    gpio_set_dir(GREEN_LED, GPIO_OUT);
    gpio_set_dir(BLUE_LED, GPIO_OUT);
    gpio_set_dir(RED_LED, GPIO_OUT);
    gpio_pull_up(GREEN_LED);
    gpio_pull_up(BLUE_LED);
    gpio_pull_up(RED_LED);
    gpio_put(GREEN_LED, false);
    gpio_put(BLUE_LED, false);
    gpio_put(RED_LED, false);

    i2c_init(MPU_PORT, 400 * 1000);
    gpio_set_function(MPU_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MPU_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MPU_SDA);
    gpio_pull_up(MPU_SCL);
    
    disp_init();
    
    mpu6050_init(&mpu, MPU_PORT, MPU6050_ADDR, AFS_2G, GFS_250DPS);
    
    int16_t accel_bias[3], gyro_bias[3];
    mpu6050_calibrate(&mpu, accel_bias, gyro_bias, 100);
    gpio_put(RED_LED, true);
    gpio_put(GREEN_LED, true);
    sd_init_driver();
    gpio_put(RED_LED, false);
    gpio_put(GREEN_LED, false);
    stdio_flush();
    run_help();
    display_menu_page(0);
    
    while (true) {
        int cRxedChar = getchar_timeout_us(0);
        if (PICO_ERROR_TIMEOUT != cRxedChar)
            process_stdio(cRxedChar);
        
        static absolute_time_t last_imu_update = 0;
        if (absolute_time_diff_us(last_imu_update, get_absolute_time()) > 100000) {
            mpu6050_read_calibrated(&mpu, accel, gyro);
            last_imu_update = get_absolute_time();
            if (current_menu_page == 2 && !capture_in_progress) {
                display_menu_page(2);
            }
        }
        
        static absolute_time_t last_alarm_time = 0;
        if (alarm_enabled && absolute_time_diff_us(last_alarm_time, get_absolute_time()) > 2000000) {
            set_buzzer_tone(BUZZER_A, C5);
            sleep_ms(100);
            set_buzzer_tone(BUZZER_A, G5);
            sleep_ms(100);
            stop_buzzer(BUZZER_A);
            last_alarm_time = get_absolute_time();
        }
        
        switch (cRxedChar) {
            case 'a':
                display_status_message("Mounting SD...");
                run_mount();
                break;
            case 'b':
                display_status_message("Unmounting SD...");
                run_unmount();
                break;
            case 'c':
                display_status_message("Listing Files...");
                run_ls();
                break;
            case 'd':
                display_status_message("Reading %s", filename);
                read_file(filename);
                break;
            case 'e':
                display_status_message("Checking Space...");
                run_getfree();
                break;
            case 'f':
                capture_imu_data_and_save();
                break;
            case 'g':
                display_status_message("Formatting SD...");
                run_format();
                break;
            case 'h':
                run_help();
                break;
            default:
                break;
        }
        check_system_errors();
        sleep_ms(100);
    }
}