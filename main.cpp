#include "mbed.h"
#include "LITE_CDH.h"
#include "LITE_EPS.h"
#include "LITE_SENSOR.h"
#include "LITE_COM.h"
#include "BME280.h"

// センサまわりのインスタンス
LITE_CDH    cdh(PB_5, PB_4, PB_3, PA_8, "sd", PA_3);
LITE_EPS    eps(PA_0, PA_4);
LITE_SENSOR sensor(PA_7, PB_7, PB_6);
LITE_COM    com(PA_9,PA_10,9600);
BME280      tenkisensor(PB_7, PB_6);   // SDA, SCL

RawSerial   pc(USBTX, USBRX, 9600);
Ticker      ticker;
DigitalOut  indicator(PB_1);

static const float LOG_INTERVAL = 60.0f;
volatile bool  log_pending = false;
int i = 0,rcmd=0,cmdflag=0;

// 日付時刻入力用バッファ
char time_buf[32];

// 1行分読み込んでバッファに格納。改行文字は含まず、NULL終端する
int readline(RawSerial &ser, char *buf, int buflen) {
    int idx = 0;
    while (true) {
        if (!ser.readable()) continue;
        char c = ser.getc();
        if (c == '\r' || c == '\n') break;
        if (idx < buflen - 1) buf[idx++] = c;
    }
    buf[idx] = '\0';
    return idx;
}

void init_time() {
    pc.printf("Enter date/time as: YYYY MM DD hh mm ss\r\n");
    // 例入力: 2025 05 01 14 30 00
    int len = readline(pc, time_buf, sizeof(time_buf));
    int y, mo, d, h, mi, s;
    if (len > 0 && sscanf(time_buf, "%d %d %d %d %d %d",
                        &y, &mo, &d, &h, &mi, &s) == 6) {
        struct tm t = {0};
        t.tm_year = y - 1900;
        t.tm_mon  = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min  = mi;
        t.tm_sec  = s;
        time_t epoch = mktime(&t);
        set_time(epoch);
        pc.printf("RTC set: %04d/%02d/%02d %02d:%02d:%02d\r\n",
                  y, mo, d, h, mi, s);
    } else {
        pc.printf("Invalid format, keeping default time\r\n");
    }
}

// 割り込みコンテキスト：フラグ立てるだけ
void on_tick() {
    log_pending = true;
}

// メインコンテキストで実行するログ処理
void log_task() {
    indicator = 1;

    // 電源ON
    eps.turn_on_regulator();
    wait(0.05f);
    tenkisensor.initialize();
    wait(0.05f);
    
    // LITE_SENSOR から
    float batvol, temp1;
    eps.vol(&batvol);
    sensor.temp_sense(&temp1);

    // BME280 から
    // (initialize は起動時1回だけでOK)
    float temp2   = tenkisensor.getTemperature();
    float humid   = tenkisensor.getHumidity();
    float pres    = tenkisensor.getPressure();

    // タイムスタンプ
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    // ファイル追記
    FILE *fp = fopen("/sd/mydir/data_01.csv", "a");
    if (fp) {
        fprintf(fp,
            "%04d-%02d-%02d %02d:%02d:%02d,%.3f,%.2f,%.2f,%.2f,%.2f\r\n",
            tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec,
            batvol, temp1, temp2, humid, pres
        );
        fclose(fp);
    }else{
       pc.printf("file open error\r\n");
    }

    // シリアルログ
    pc.printf("%02d:%02d:%02d  V=%.3fV  T=%.2f°C TT=%.2f H=%.2f P=%.2f\r\n",
        tm->tm_hour, tm->tm_min, tm->tm_sec,
        batvol, temp1,temp2,humid, pres
    );
    com.printf("%02d:%02d:%02d  V=%.3fV  T=%.2f°C TT=%.2f H=%.2f P=%.2f\r\n",
        tm->tm_hour, tm->tm_min, tm->tm_sec,
        batvol, temp1,temp2,humid, pres
    );

    // 電源OFF
    eps.shut_down_regulator();
    indicator = 0;
}

int main() {
    // 起動時デバッグ１回だけ
    pc.printf("Start logging every %.0f s with both sensors\r\n", LOG_INTERVAL);
    cdh.turn_on_analogSW();//turn on transceiver
    init_time();//起動時に時刻を設定 入力例: 2025 05 01 14 30 00
    wait_ms(10);

    // ディレクトリ＆ヘッダ行作成
    mkdir("/sd/mydir", 0777);
    FILE *hf = fopen("/sd/mydir/data_01.csv","r");
    if (!hf) {
        hf = fopen("/sd/mydir/data_01.csv","w");
        fprintf(hf,"timestamp,battV,temp_lm60,temp_bme,humidity,pressure\r\n");
        fclose(hf);
    } else {
        fclose(hf);
    }
 
    // 最初のログ実行
    log_task();

    // UART2 をオフ(pc.printfをOFF)
    __HAL_RCC_USART2_CLK_DISABLE();
    DigitalIn tx2(USBTX); tx2.mode(PullNone);
    DigitalIn rx2(USBRX); rx2.mode(PullNone);

    // Ticker 割り込みでフラグだけ立てる
    ticker.attach(on_tick, LOG_INTERVAL);

    // メインループ：sleep → フラグ検知 → log_task
    while (true) {
        sleep();
        com.xbee_receive(&rcmd,&cmdflag);
        if (log_pending) {
            log_pending = false;
            log_task();
        }
        if (cmdflag == 1) {
            if (rcmd == 'a') {
                com.printf("Hello!Hello!Hello!\r\n");
            }
            com.initialize();
        }
    }
}