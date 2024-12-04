#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "lwip/apps/mqtt_priv.h"

// Configuración de Wi-Fi y MQTT
#define WIFI_SSID "TeleCentro-54ad-5G"
#define WIFI_PASSWORD "aluf2737"
#define MQTT_BROKER_URL "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_TOPIC "environment/sensordata"

// Sensores y hardware
#define RL_VALUE 10.0
#define VCC 3.3
#define ADC_MAX 4095
#define ADC_PIN 28
#define NTC_PIN 22
#define BUZZER 14
#define LED_1 4
#define LED_2 5
#define MIN_CO_DETECTION 10.0
#define MAX_TEMP_LIMIT 40.0

// MQTT
static ip_addr_t mqtt_ip;
typedef struct {
    mqtt_client_t *mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
} MQTT_CLIENT_DATA_T;

MQTT_CLIENT_DATA_T *mqtt;

void inicializar_hardware();
double leer_sensor_gas();
double leer_sensor_temperatura();
void manejar_alarma(double temperatura, double concentracion_co);
void publicar_datos(mqtt_client_t *client, double temperatura, double concentracion_co);
void conectar_mqtt(MQTT_CLIENT_DATA_T *mqtt);

void inicializar_hardware() {
    stdio_init_all();
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_gpio_init(NTC_PIN);

    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);
    gpio_put(BUZZER, 0);

    gpio_init(LED_1);
    gpio_set_dir(LED_1, GPIO_OUT);
    gpio_put(LED_1, 0);

    gpio_init(LED_2);
    gpio_set_dir(LED_2, GPIO_OUT);
    gpio_put(LED_2, 1); // LED_2 encendido por defecto para "estado seguro"
}

double leer_sensor_gas() {
    adc_select_input(ADC_PIN);
    int adcValue = adc_read();
    double rs = ((VCC * RL_VALUE) / ((double)adcValue / ADC_MAX * VCC)) - RL_VALUE;
    return rs;
}

double leer_sensor_temperatura() {
    adc_select_input(NTC_PIN);
    int adcValue = adc_read();
    double resistencia = (10.0 * (VCC - ((double)adcValue / ADC_MAX * VCC))) / ((double)adcValue / ADC_MAX * VCC);
    const double BETA = 3950.0, T0 = 298.15, R0 = 10.0;
    return 1.0 / ((1.0 / T0) + (1.0 / BETA) * log(resistencia / R0)) - 273.15;
}

void manejar_alarma(double temperatura, double concentracion_co) {
    if (concentracion_co > MIN_CO_DETECTION || temperatura > MAX_TEMP_LIMIT) {
        gpio_put(LED_1, 1); // LED de alarma
        gpio_put(LED_2, 0); // LED de estado seguro
        gpio_put(BUZZER, 1); // Activar buzzer
        printf("¡ALERTA! Temperatura: %.2f °C, CO: %.2f ppm\n", temperatura, concentracion_co);
    } else {
        gpio_put(LED_1, 0); // Apagar LED de alarma
        gpio_put(LED_2, 1); // LED de estado seguro
        gpio_put(BUZZER, 0); // Apagar buzzer
        printf("Estado seguro. Temperatura: %.2f °C, CO: %.2f ppm\n", temperatura, concentracion_co);
    }
}

void conectar_mqtt(MQTT_CLIENT_DATA_T *mqtt) {
    err_t err;
    mqtt->mqtt_client_inst = mqtt_client_new();
    if (!ip4addr_aton(MQTT_BROKER_URL, &mqtt_ip)) {
        printf("IP del broker no válida\n");
        return;
    }

    err = mqtt_client_connect(mqtt->mqtt_client_inst, &mqtt_ip, MQTT_PORT, NULL, mqtt, &mqtt->mqtt_client_info);
    if (err != ERR_OK) {
        printf("Error conectando al broker MQTT: %d\n", err);
    } else {
        printf("Conectado al broker MQTT\n");
    }
}

void publicar_datos(mqtt_client_t *client, double temperatura, double concentracion_co) {
    char mensaje[128];
    snprintf(mensaje, sizeof(mensaje), "{\"temperature\": %.2f, \"co\": %.2f}", temperatura, concentracion_co);
    err_t err = mqtt_publish(client, MQTT_TOPIC, mensaje, strlen(mensaje), 1, 0, NULL, NULL);
    if (err != ERR_OK) {
        printf("Error publicando datos: %d\n", err);
    } else {
        printf("Datos publicados: %s\n", mensaje);
    }
}

int main() {
    inicializar_hardware();

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("Error inicializando Wi-Fi\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Error conectando a Wi-Fi\n");
        return -1;
    }
    printf("Conectado a Wi-Fi\n");

    mqtt = calloc(1, sizeof(MQTT_CLIENT_DATA_T));
    conectar_mqtt(mqtt);

    while (true) {
        if (!mqtt->mqtt_client_inst->conn_state) {
            printf("Reconectando a MQTT...\n");
            conectar_mqtt(mqtt);
        }

        double temperatura = leer_sensor_temperatura();
        double concentracion_co = leer_sensor_gas();

        manejar_alarma(temperatura, concentracion_co);
        publicar_datos(mqtt->mqtt_client_inst, temperatura, concentracion_co);
        sleep_ms(1000);
    }
}
