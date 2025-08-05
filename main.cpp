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
RawSerial   pc(USBTX, USBRX, 9600);
BME280      bme280(PB_7, PB_6);  

int i = 0, rcmd=0, cmdflag=0;
float LOG_INTERVAL = 1; //x秒おきに記録
float batvol, temp;
Timer sattime;

int main() {
    // 起動時デバッグ１回だけ
    pc.printf("Start operation every %f\r\n", LOG_INTERVAL);
    sattime.start();
    cdh.turn_on_analogSW();//turn on transceiver
    
    //メモリにファイルを作成，
    mkdir("/sd/mydir", 0777); //メモリ内にmysirの階層を作成
    FILE *fp = fopen("/sd/mydir/data.csv","r"); //メモリを読み出しモード(r)で開く
    if (!fp) {
        fp = fopen("/sd/mydir/data.csv","w"); //メモリを書き込みモード(w)で開く
        fprintf(fp,"time[s],battery[V],temp lite[deg],temp bme280[deg],humidity[%],pressure[hPa]\r\n"); //ヘッダーを作成
        fclose(fp);
    } else {
        fclose(fp);
    }
    eps.turn_on_regulator();
    wait(0.05);
    bme280.initialize();//BME280のイニシャライズ（1回でOK)
    wait(0.05);
    
    while (true) {
        com.xbee_receive(&rcmd,&cmdflag);

        // バッテリ電圧，HEPTAーSAT LITE上の温度センサの値を取得
        eps.vol(&batvol);
        sensor.temp_sense(&temp);

        // BME280から温度・湿度・気圧を取得
        float temp_bme280   = bme280.getTemperature();
        float humid_bme280   = bme280.getHumidity();
        float pressure_bme280    = bme280.getPressure();

        // ファイル追記
        FILE *fp = fopen("/sd/mydir/data.csv", "a");//メモリを追記モード(a)で開く
        if (fp) {
            fprintf(fp,"%f,%f,%f,%f,%f,%f\r\n",
                sattime.read(),batvol,temp,temp_bme280,humid_bme280,pressure_bme280);
            fclose(fp);
        }

        // シリアルログ(TeraTerm出力 / Xbeeで送信)
        pc.printf("Time=%f  V=%.3fV  LITE temp=%.2f°C BME280 temp=%.2f Humid=%.2f Pressure=%.2f\r\n",
            sattime.read(),batvol,temp,temp_bme280,humid_bme280,pressure_bme280);
        com.printf("Time=%f  V=%.3fV  LITE temp=%.2f°C BME280 temp=%.2f Humid=%.2f Pressure=%.2f\r\n",
            sattime.read(),batvol,temp,temp_bme280,humid_bme280,pressure_bme280);

        wait(LOG_INTERVAL);

        if (cmdflag == 1) {
            if (rcmd == 'a') {//コマンドaを受信したときの処理
            }
            com.initialize();
        }

        
    }
}
