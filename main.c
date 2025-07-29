#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define ALARM_ERROR_DURATION 500  // Duração do alarme de erro (ms)

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
static char filename_base[20] = "medicoes_imu";
static int med_count = 1;

static bool capture_in_progress = false;
static bool should_stop_capture = false;

static int current_menu_page = 0;
static const int MAX_MENU_PAGES = 3;
static bool alarm_enabled = false;


void play_error_alarm() {
    gpio_put(RED_LED, true);  // Acende LED vermelho
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

void disp_init(){
    // Configuração do display OLED
    i2c_init(DISP_PORT, 400 * 1000);
    gpio_set_function(DISP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DISP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DISP_SDA);
    gpio_pull_up(DISP_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISP_ADDRESS, DISP_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
}

// **FUNÇÃO DO MENU MODIFICADA**
void display_menu_page(int page) {
    ssd1306_fill(&ssd, false);
    char title[20];

    switch(page) {
        case 0: // Menu Principal
            sprintf(title, "Menu Principal (1/%d)", MAX_MENU_PAGES);
            ssd1306_draw_string(&ssd, title, 1, 5);
            ssd1306_draw_string(&ssd, "----------------", 1, 15);
            ssd1306_draw_string(&ssd, "> Status SD", 5, 25);
            ssd1306_draw_string(&ssd, "> Dados IMU", 5, 38);
            ssd1306_draw_string(&ssd, "> Configuracoes", 5, 51);
            break;
            
        case 1: // Status do SD Card
            sprintf(title, "Status SD (2/%d)", MAX_MENU_PAGES);
            ssd1306_draw_string(&ssd, title, 1, 5);
            ssd1306_draw_string(&ssd, "---------------", 1, 15);
            char status_str[20];
            sprintf(status_str, "Estado: %s", sd_mounted ? "Montado" : "N/A");
            ssd1306_draw_string(&ssd, status_str, 5, 28);
            ssd1306_draw_string(&ssd, "Arquivo Atual:", 5, 42);
            ssd1306_draw_string(&ssd, filename, 5, 52);
            break;
            
        case 2: // Dados do IMU
            sprintf(title, "Dados IMU (3/%d)", MAX_MENU_PAGES);
            ssd1306_draw_string(&ssd, title, 1, 5);
            ssd1306_draw_string(&ssd, "---------------", 1, 15);
            
            char accel_str[30];
            sprintf(accel_str, "A:%.2f,%.2f,%.2f", accel[0], accel[1], accel[2]);
            ssd1306_draw_string(&ssd, accel_str, 1, 24);
            
            char gyro_str[30];
            sprintf(gyro_str, "G:%.2f,%.2f,%.2f", gyro[0], gyro[1], gyro[2]);
            ssd1306_draw_string(&ssd, gyro_str, 1, 42);
            break;
            
        default:
            ssd1306_draw_string(&ssd, "Pagina Invalida", 1, 20);
            break;
    }
    
    ssd1306_send_data(&ssd);
}


void toggle_alarm() {
    alarm_enabled = !alarm_enabled;
    
    if (alarm_enabled) {
        // Toca um som de alarme ativado
        set_buzzer_tone(BUZZER_A, C5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, E5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, G5);
        sleep_ms(200);
        stop_buzzer(BUZZER_A);
    } else {
        // Toca um som de alarme desativado
        set_buzzer_tone(BUZZER_A, G5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, E5);
        sleep_ms(200);
        set_buzzer_tone(BUZZER_A, C5);
        sleep_ms(200);
        stop_buzzer(BUZZER_A);
    }
}

static sd_card_t *sd_get_by_name(const char *const name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return sd_get_by_num(i);
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}
static FATFS *sd_get_fs_by_name(const char *name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return &sd_get_by_num(i)->fatfs;
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}

static void run_setrtc()
{
    const char *dateStr = strtok(NULL, " ");
    if (!dateStr)
    {
        printf("Missing argument\n");
        return;
    }
    int date = atoi(dateStr);

    const char *monthStr = strtok(NULL, " ");
    if (!monthStr)
    {
        printf("Missing argument\n");
        return;
    }
    int month = atoi(monthStr);

    const char *yearStr = strtok(NULL, " ");
    if (!yearStr)
    {
        printf("Missing argument\n");
        return;
    }
    int year = atoi(yearStr) + 2000;

    const char *hourStr = strtok(NULL, " ");
    if (!hourStr)
    {
        printf("Missing argument\n");
        return;
    }
    int hour = atoi(hourStr);

    const char *minStr = strtok(NULL, " ");
    if (!minStr)
    {
        printf("Missing argument\n");
        return;
    }
    int min = atoi(minStr);

    const char *secStr = strtok(NULL, " ");
    if (!secStr)
    {
        printf("Missing argument\n");
        return;
    }
    int sec = atoi(secStr);

    datetime_t t = {
        .year = (int16_t)year,
        .month = (int8_t)month,
        .day = (int8_t)date,
        .dotw = 0, // 0 is Sunday
        .hour = (int8_t)hour,
        .min = (int8_t)min,
        .sec = (int8_t)sec};
    rtc_set_datetime(&t);
}

static void run_format()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    /* Format the drive with default parameters */
    FRESULT fr = f_mkfs(arg1, 0, 0, FF_MAX_SS * 2);
    if (FR_OK != fr)
        printf("f_mkfs error: %s (%d)\n", FRESULT_str(fr), fr);
}
static void run_mount() {  
    gpio_put(RED_LED, true); gpio_put(GREEN_LED, true);
    sleep_ms(500);
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs) {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        play_error_alarm();
        return;
    }
    FRESULT fr = f_mount(p_fs, arg1, 1);
    if (FR_OK != fr) {
        printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        play_error_alarm();
        sd_mounted = false; // Garante que o estado seja falso em caso de erro
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = true;
    sd_mounted = true; // Atualiza o estado global
    printf("Processo de montagem do SD ( %s ) concluído\n", pSD->pcName);
    gpio_put(RED_LED, false); gpio_put(GREEN_LED, true);
}
static void run_unmount()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    FRESULT fr = f_unmount(arg1);
    if (FR_OK != fr)
    {
        printf("f_unmount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = false;
    sd_mounted = false; // Atualiza o estado global
    pSD->m_Status |= STA_NOINIT; // in case medium is removed
    printf("SD ( %s ) desmontado\n", pSD->pcName);
    gpio_put(GREEN_LED, false);  gpio_put(BLUE_LED, false); gpio_put(RED_LED, false);
}
static void run_getfree()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    FRESULT fr = f_getfree(arg1, &fre_clust, &p_fs);
    if (FR_OK != fr)
    {
        printf("f_getfree error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    tot_sect = (p_fs->n_fatent - 2) * p_fs->csize;
    fre_sect = fre_clust * p_fs->csize;
    printf("%10lu KiB total drive space.\n%10lu KiB available.\n", tot_sect / 2, fre_sect / 2);
}
static void run_ls()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = "";
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr;
    char const *p_dir;
    if (arg1[0])
    {
        p_dir = arg1;
    }
    else
    {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr)
        {
            printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }
        p_dir = cwdbuf;
    }
    printf("Directory Listing: %s\n", p_dir);
    DIR dj;
    FILINFO fno;
    memset(&dj, 0, sizeof dj);
    memset(&fno, 0, sizeof fno);
    fr = f_findfirst(&dj, &fno, p_dir, "*");
    if (FR_OK != fr)
    {
        printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (fr == FR_OK && fno.fname[0])
    {
        const char *pcWritableFile = "writable file",
                   *pcReadOnlyFile = "read only file",
                   *pcDirectory = "directory";
        const char *pcAttrib;
        if (fno.fattrib & AM_DIR)
            pcAttrib = pcDirectory;
        else if (fno.fattrib & AM_RDO)
            pcAttrib = pcReadOnlyFile;
        else
            pcAttrib = pcWritableFile;
        printf("%s [%s] [size=%llu]\n", fno.fname, pcAttrib, fno.fsize);

        fr = f_findnext(&dj, &fno);
    }
    f_closedir(&dj);
}
static void run_cat()
{
    char *arg1 = strtok(NULL, " ");
    if (!arg1)
    {
        printf("Missing argument\n");
        return;
    }
    FIL fil;
    FRESULT fr = f_open(&fil, arg1, FA_READ);
    if (FR_OK != fr)
    {
        printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    char buf[256];
    while (f_gets(buf, sizeof buf, &fil))
    {
        printf("%s", buf);
    }
    fr = f_close(&fil);
    if (FR_OK != fr)
        printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
}


// Função para ler o conteúdo de um arquivo e exibir no terminal
void read_file(const char *filename)
{  
    gpio_put(BLUE_LED, true); gpio_put(GREEN_LED, false); gpio_put(RED_LED, false);
    FIL file;
    FRESULT res = f_open(&file, filename, FA_READ);
    if (res != FR_OK)
    {
        printf("[ERRO] Não foi possível abrir o arquivo para leitura. Verifique se o Cartão está montado ou se o arquivo existe.\n");

        return;
    }
    char buffer[128];
    UINT br;
    printf("Conteúdo do arquivo %s:\n", filename);
    while (f_read(&file, buffer, sizeof(buffer) - 1, &br) == FR_OK && br > 0)
    {
        buffer[br] = '\0';
        printf("%s", buffer);
    }
    f_close(&file);
    printf("\nLeitura do arquivo %s concluída.\n\n", filename);
    gpio_put(BLUE_LED, false); gpio_put(GREEN_LED, true); gpio_put(RED_LED, false);
}


static void run_help()
{
    printf("\nComandos disponíveis:\n\n");
    printf("Digite 'a' para montar o cartão SD\n");
    printf("Digite 'b' para desmontar o cartão SD\n");
    printf("Digite 'c' para listar arquivos\n");
    printf("Digite 'd' para mostrar conteúdo do arquivo\n");
    printf("Digite 'e' para obter espaço livre no cartão SD\n");
    printf("Digite 'f' para capturar dados do ADC e salvar no arquivo\n");
    printf("Digite 'g' para formatar o cartão SD\n");
    printf("Digite 'h' para exibir os comandos disponíveis\n");
    printf("\nEscolha o comando:  ");
}

typedef void (*p_fn_t)();
typedef struct
{
    char const *const command;
    p_fn_t const function;
    char const *const help;
} cmd_def_t;

static cmd_def_t cmds[] = {
    {"setrtc", run_setrtc, "setrtc <DD> <MM> <YY> <hh> <mm> <ss>: Set Real Time Clock"},
    {"format", run_format, "format [<drive#:>]: Formata o cartão SD"},
    {"mount", run_mount, "mount [<drive#:>]: Monta o cartão SD"},
    {"unmount", run_unmount, "unmount <drive#:>: Desmonta o cartão SD"},
    {"getfree", run_getfree, "getfree [<drive#:>]: Espaço livre"},
    {"ls", run_ls, "ls: Lista arquivos"},
    {"cat", run_cat, "cat <filename>: Mostra conteúdo do arquivo"},
    {"help", run_help, "help: Mostra comandos disponíveis"}};

static void process_stdio(int cRxedChar)
{
    static char cmd[256];
    static size_t ix;

    if (!isprint(cRxedChar) && !isspace(cRxedChar) && '\r' != cRxedChar &&
        '\b' != cRxedChar && cRxedChar != (char)127)
        return;
    printf("%c", cRxedChar); // echo
    stdio_flush();
    if (cRxedChar == '\r')
    {
        printf("%c", '\n');
        stdio_flush();

        if (!strnlen(cmd, sizeof cmd))
        {
            printf("> ");
            stdio_flush();
            return;
        }
        char *cmdn = strtok(cmd, " ");
        if (cmdn)
        {
            size_t i;
            for (i = 0; i < count_of(cmds); ++i)
            {
                if (0 == strcmp(cmds[i].command, cmdn))
                {
                    (*cmds[i].function)();
                    break;
                }
            }
            if (count_of(cmds) == i)
                printf("Command \"%s\" not found\n", cmdn);
        }
        ix = 0;
        memset(cmd, 0, sizeof cmd);
        printf("\n> ");
        stdio_flush();
    }
    else
    {
        if (cRxedChar == '\b' || cRxedChar == (char)127)
        {
            if (ix > 0)
            {
                ix--;
                cmd[ix] = '\0';
            }
        }
        else
        {
            if (ix < sizeof cmd - 1)
            {
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
    
    gpio_put(RED_LED, true); gpio_put(GREEN_LED, false);
    printf("\nIniciando a captura de dados de aceleração e giroscópio.\n");
    
    const int total_amostras = 1000;
    const int intervalo_ms = 100;
    int tempo_total_s = (total_amostras * intervalo_ms) / 1000;
    printf("Tempo estimado: %d segundos\n", tempo_total_s);
    
    snprintf(filename, sizeof(filename), "%s%d.csv", filename_base, med_count);
    med_count++;
    
    FIL file;
    FRESULT res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("\n[ERRO] Não foi possível abrir o arquivo para escrita. Monte o cartão.\n");
        play_error_alarm();
        capture_in_progress = false;
        return;
    }
    
    UINT bw;
    absolute_time_t start_time = get_absolute_time();
    char header[] = "Amostra, Aceleração X, Aceleração Y, Aceleração Z, Giroscópio X, Giroscópio Y, Giroscópio Z, Tempo (s)\n";
    res = f_write(&file, header, strlen(header), &bw);
    
    for (int i = 0; i < total_amostras && !should_stop_capture; i++) {
        mpu6050_read_calibrated(&mpu, accel, gyro);
        char buffer[100];
        uint32_t time = to_ms_since_boot(get_absolute_time());
        char buffer_disp[50];
        sprintf(buffer_disp, "Amostra: %d,Tempo de medição: %1.2f", i+1, time);
        ssd1306_draw_string(&ssd, buffer_disp, 1, 35);
        sprintf(buffer, "%d,%f,%f,%f,%f,%f,%f,%f\n", i + 1, accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2], (float)time/1000);
        res = f_write(&file, buffer, strlen(buffer), &bw);
        
        if (res != FR_OK) {
            printf("[ERRO] Não foi possível escrever no arquivo.\n");
            play_error_alarm();
            break;
        }
        
        if ((i + 1) % 50 == 0 || i == 0) {
            absolute_time_t now = get_absolute_time();
            int elapsed_ms = to_ms_since_boot(now) - to_ms_since_boot(start_time);
            int remaining_ms = (total_amostras - (i + 1)) * intervalo_ms;
            int remaining_s = remaining_ms / 1000;
            printf("Amostra %d/%d - Tempo restante: %d segundos\n", i + 1, total_amostras, remaining_s);
        }
        
        sleep_ms(intervalo_ms);
    }
    
    f_close(&file);
    
    if (should_stop_capture) {
        printf("\nCaptura interrompida pelo usuário. Dados parciais salvos em %s.\n", filename);
    } else {
        printf("\nCaptura concluída. Dados salvos em %s.\n", filename);
    }
    
    gpio_put(RED_LED, false); gpio_put(GREEN_LED, true);
    capture_in_progress = false;
    should_stop_capture = false;
}

void check_system_errors() {
    static bool last_sd_state = false;
    static bool sd_error_shown = false;
    
    // Verifica estado do SD card
    bool current_sd_state = sd_mounted;
    if (last_sd_state != current_sd_state) {
        last_sd_state = current_sd_state;
        sd_error_shown = false;
    }
    
    // Se SD deveria estar montado mas não está
    if (sd_mounted && !sd_get_by_num(0)->mounted && !sd_error_shown) {
        play_error_alarm();
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "ERRO: SD desconectado", 1, 20);
        ssd1306_send_data(&ssd);
        sd_error_shown = true;
    }
}


void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (gpio == BUTTON_A && (current_time - last_time_a >= DEBOUNCE_DELAY)) {
        // Alterna entre páginas do menu
        current_menu_page = (current_menu_page + 1) % MAX_MENU_PAGES;
        display_menu_page(current_menu_page);
        last_time_a = current_time;
    } 
    else if (gpio == BUTTON_B && (current_time - last_time_b >= DEBOUNCE_DELAY)) {
        reset_usb_boot(0, 0);
        last_time_b = current_time;
    }
}

int main()
{
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

    gpio_init(GREEN_LED); gpio_init(BLUE_LED); gpio_init(RED_LED);
    gpio_set_dir(GREEN_LED, GPIO_OUT); gpio_set_dir(BLUE_LED, GPIO_OUT); gpio_set_dir(RED_LED, GPIO_OUT);
    gpio_pull_up(GREEN_LED); gpio_pull_up(BLUE_LED); gpio_pull_up(RED_LED);
    gpio_put(GREEN_LED, false); gpio_put(BLUE_LED, false); gpio_put(RED_LED, false);

    i2c_init(MPU_PORT, 400 * 1000);
    gpio_set_function(MPU_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MPU_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MPU_SDA);
    gpio_pull_up(MPU_SCL);
    disp_init();
    // Inicializa MPU6050
    
    mpu6050_init(&mpu, MPU_PORT, MPU6050_ADDR, AFS_2G, GFS_250DPS);
    
    // Calibração (opcional)
    int16_t accel_bias[3], gyro_bias[3];
    mpu6050_calibrate(&mpu, accel_bias, gyro_bias, 100);
    gpio_put(RED_LED, true); gpio_put(GREEN_LED, true);
    sd_init_driver();
    gpio_put(RED_LED, false); gpio_put(GREEN_LED, false);
    stdio_flush(); 
    run_help();
    display_menu_page(0); // Exibe a primeira página do novo menu

    while (true) {
        int cRxedChar = getchar_timeout_us(0);
        if (PICO_ERROR_TIMEOUT != cRxedChar)
            process_stdio(cRxedChar);
        
        static absolute_time_t last_imu_update = 0;
        if (absolute_time_diff_us(last_imu_update, get_absolute_time()) > 100000) { // 100ms
            mpu6050_read_calibrated(&mpu, accel, gyro);
            last_imu_update = get_absolute_time();
            
            // Se estiver na página de dados IMU, atualiza o display
            if (current_menu_page == 2) {
                display_menu_page(2);
            }
        }
        static absolute_time_t last_alarm_time = 0;
        if (alarm_enabled && absolute_time_diff_us(last_alarm_time, get_absolute_time()) > 2000000) { // 2s
            set_buzzer_tone(BUZZER_A, C5);
            sleep_ms(100);
            set_buzzer_tone(BUZZER_A, G5);
            sleep_ms(100);
            stop_buzzer(BUZZER_A);
            last_alarm_time = get_absolute_time();
        }
        
        switch (cRxedChar) {
            case 'a': // Monta o SD card se pressionar 'a'
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Montando SD...", 1, 25);
                ssd1306_send_data(&ssd);
                printf("\nMontando o SD...\n");
                run_mount();
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "SD Montado!", 1, 25);
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
                display_menu_page(current_menu_page); // Volta para o menu
                printf("\nSd Montado");
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'b': // Desmonta o SD card se pressionar 'b'
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Desmontando SD...", 1, 25);
                ssd1306_send_data(&ssd);
                printf("\nDesmontando o SD. Aguarde...\n");
                run_unmount();
                gpio_put(RED_LED, false); gpio_put(GREEN_LED, false);
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "SD desmontado!", 1, 25);
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
                display_menu_page(current_menu_page); // Volta para o menu
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'c': // Lista diretórios e os arquivos se pressionar 'c'
                printf("\nListando arquivos no cartão SD...\n");
                run_ls();
                printf("\nListagem concluída.\n");
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'd': // Exibe o conteúdo do arquivo se pressionar 'd'
                read_file(filename);
                printf("Escolha o comando (h = help):  ");
                break;
                
            case 'e': // Obtém o espaço livre no SD card se pressionar 'e'
                printf("\nObtendo espaço livre no SD...\n\n");
                run_getfree();
                printf("\nEspaço livre obtido.\n");
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'f': // Captura dados do IMU e salva no arquivo se pressionar 'f'
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Iniciando Gravacao", 1, 15);
                ssd1306_draw_string(&ssd, "Aguarde...", 1, 35);
                ssd1306_send_data(&ssd);
                
                // **NOVO SOM DE INÍCIO DA GRAVAÇÃO**
                set_buzzer_tone(BUZZER_A, C5);
                sleep_ms(150);
                stop_buzzer(BUZZER_A);
                sleep_ms(100);
                set_buzzer_tone(BUZZER_A, C5);
                sleep_ms(150);
                stop_buzzer(BUZZER_A);

                capture_imu_data_and_save();
                
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Dados Salvos!", 1, 25);
                ssd1306_send_data(&ssd);

                // **NOVO SOM DE FIM DA GRAVAÇÃO**
                set_buzzer_tone(BUZZER_A, G4);
                sleep_ms(200);
                set_buzzer_tone(BUZZER_A, E4);
                sleep_ms(200);
                set_buzzer_tone(BUZZER_A, C4);
                sleep_ms(200);
                stop_buzzer(BUZZER_A);

                sleep_ms(1000);
                display_menu_page(current_menu_page); // Volta para o menu
                printf("\nDados Salvos");
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'g': // Formata o SD card se pressionar 'g'
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Formatando SD...", 1, 25);
                ssd1306_send_data(&ssd);
                printf("\nProcesso de formatação do SD iniciado. Aguarde...\n");
                run_format();
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "SD Formatado!", 1, 25);
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
                display_menu_page(current_menu_page); // Volta para o menu
                printf("\nFormatação concluída.\n\n");
                printf("\nEscolha o comando (h = help):  ");
                break;
                
            case 'h': // Exibe os comandos disponíveis se pressionar 'h'
                run_help();
                break;
                
            default:
                // Nenhuma ação para outros caracteres
                break;
        }
        check_system_errors();
        sleep_ms(100); // Reduzido para maior responsividade do loop
    }
}
