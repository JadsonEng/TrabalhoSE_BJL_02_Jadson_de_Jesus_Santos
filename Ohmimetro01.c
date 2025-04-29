/*
 * Por: Wilton Lacerda Silva
 *    Ohmímetro utilizando o ADC da BitDogLab
 *
 * Neste exemplo, utilizamos o ADC do RP2040 para medir a resistência de um resistor
 * desconhecido, utilizando um divisor de tensão com dois resistores.
 * O resistor conhecido é de 10k ohm e o desconhecido é o que queremos medir.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <math.h>
 #include "pico/stdlib.h"
 #include "hardware/adc.h"
 #include "hardware/i2c.h"
 #include "lib/ssd1306.h"
 #include "lib/font.h"
 #include "pico/bootrom.h"
 
 #define I2C_PORT        i2c1
 #define I2C_SDA         14
 #define I2C_SCL         15
 #define endereco        0x3C
 #define ADC_PIN         28  // GPIO para o voltímetro
 #define Botao_A         5   // GPIO para botão A
 #define Botao_B         6   // GPIO para botão B
 
 // Resistor conhecido e resolução do ADC
 int    R_conhecido    = 10000;   // 10kΩ
 float  R_x            = 0.0f;    // Resistência calculada
 int    ADC_RESOLUTION = 4095;
 
 // Resistores da Série E24 (510 Ω a 100 kΩ)
 const int E24_values[] = {
     510, 560, 620, 680, 750, 820, 910, 1000,1100,1200,1300,1500,1600,1800,2000,
     2200,2400,2700,3000,3300,3600,3900,4300,4700,5100,5600,6200,6800,7500,8200,
     9100,10000,11000,12000,13000,15000,16000,18000,20000,22000,24000,27000,30000,
     36000,39000,43000,47000,51000,56000,62000,68000,75000,82000,91000,100000
 };

 #define E24_COUNT (sizeof(E24_values)/sizeof(E24_values[0]))   // Determina o número de elementos no vetor
 
 // Debounce e seleção de tela
 absolute_time_t last_interrupt_time = 0;
 bool codigo_cores = false;
 
 // Interrupção com debounce
 void gpio_irq_handler(uint gpio, uint32_t events) {
     absolute_time_t now = get_absolute_time();

     if (absolute_time_diff_us(last_interrupt_time, now) < 250000) return;
     last_interrupt_time = now;

     if (gpio == Botao_A) {
        codigo_cores = !codigo_cores;

     } else if (gpio == Botao_B) {
        reset_usb_boot(0, 0);
     } 
 }
 
 // Estrutura de cores
 typedef struct { char *nome; int valor; } Cor;
 const Cor cores[] = {
     {"Preto",      0},
     {"Marrom",     1},
     {"Vermelho",   2},
     {"Laranja",    3},
     {"Amarelo",    4},
     {"Verde",      5},
     {"Azul",       6},
     {"Violeta",    7},
     {"Cinza",      8},
     {"Branco",     9}
 };
 
 // Converte valor float em código de cores (mantido para debug)
 void obter_cores_resistor(float resistencia, char *c1, char *c2, char *c3) {
     if (resistencia < 1.0f) {
         strcpy(c1, "N/A"); strcpy(c2, "N/A"); strcpy(c3, "N/A");
         return;
     }
     int mult = 0;
     float tmp = resistencia;
     while (tmp >= 100.0f) { tmp /= 10.0f; mult++; }
     while (tmp < 10.0f)   { tmp *= 10.0f; mult--; }
     int norm = (int)(tmp + 0.5f);
     int d1 = norm / 10;
     int d2 = norm % 10;
     strcpy(c1, cores[d1].nome);
     strcpy(c2, cores[d2].nome);
     if      (mult >= 0 && mult < 10) strcpy(c3, cores[mult].nome);
     else if (mult == -1)             strcpy(c3, "Dourado");
     else if (mult == -2)             strcpy(c3, "Prata");
     else                             strcpy(c3, "N/A");
 }
 
 // Converte valor nominal E24 inteiro em código de cores
 void obter_cores_resistor_int(int R_nominal, char *c1, char *c2, char *c3) {
     if (R_nominal < 10) {
         strcpy(c1, "N/A"); strcpy(c2, "N/A"); strcpy(c3, "N/A");
         return;
     }
     int tmp = R_nominal;
     int mult = 0;
     while (tmp >= 100) { tmp /= 10; mult++; }
     int d1 = tmp / 10;
     int d2 = tmp % 10;
     strcpy(c1, cores[d1].nome);
     strcpy(c2, cores[d2].nome);
     if      (mult >= 0 && mult < 10) strcpy(c3, cores[mult].nome);
     else if (mult == -1)             strcpy(c3, "Dourado");
     else if (mult == -2)             strcpy(c3, "Prata");
     else                             strcpy(c3, "N/A");
 }
 
 int main() {
    // Configuração de botões e interrupção
    gpio_init(Botao_A); 
    gpio_init(Botao_B);
    gpio_set_dir(Botao_A, GPIO_IN); 
    gpio_set_dir(Botao_B, GPIO_IN);
    gpio_pull_up(Botao_A); 
    gpio_pull_up(Botao_B);
    gpio_set_irq_enabled_with_callback(Botao_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(Botao_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Serial e display
    stdio_init_all(); sleep_ms(2000);
    printf("Ohmímetro iniciado\n");
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA); gpio_pull_up(I2C_SCL);
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Configuração ADC
    adc_init(); adc_gpio_init(ADC_PIN);

    // Buffers
    char str_x[8], str_y[8], cor1[20], cor2[20], cor3[20];
 
    while (true) {
        // Leitura média do ADC
        adc_select_input(2);
        float soma = 0;
        for (int i = 0; i < 500; i++) { soma += adc_read(); sleep_ms(1); }
        float media = soma / 500.0f;

        // Calcula resistência
        R_x = (R_conhecido * media) / (ADC_RESOLUTION - media);

        // Verifica se a resistencia está dentro da escala solicitada
        float R_display;
        if (R_x > 100000.0f) {
        R_display = 999999.0f;      // Valor muito alto

        } else if (R_x < 510.0f) {
        R_display = 0.0f;           // Valor muito baixo

        } else {
        R_display = R_x;            // Valor dentro da escala
        }

        // Encontra valor nominal E24 mais próximo
        int R_nominal = E24_values[0];
        float best = fabsf(R_x - (float)R_nominal);
        for (int i = 1; i < E24_COUNT; i++) {
        float d = fabsf(R_x - (float)E24_values[i]);
        if (d < best) { 
        best = d; R_nominal = E24_values[i]; 
        }
        }

        // Strings para display
        sprintf(str_x, "%1.0f", media);
        sprintf(str_y, "%1.0f", R_display);

        // Cores utilizando versão inteira para evitar erros
        obter_cores_resistor_int(R_nominal, cor1, cor2, cor3);

        // Atualiza display
        ssd1306_fill(&ssd, false);
        if (codigo_cores) {
        ssd1306_rect(&ssd, 3,3,122,60,true,false);
        ssd1306_line(&ssd,3,25,123,25,true);
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37",8,6);
        ssd1306_draw_string(&ssd, "EMBARCATECH",20,16);
        ssd1306_draw_string(&ssd, "codigo de cor",10,27);
        ssd1306_draw_string(&ssd, "1 - ",8,36); ssd1306_draw_string(&ssd, cor1,40,36);
        ssd1306_draw_string(&ssd, "2 - ",8,44); ssd1306_draw_string(&ssd, cor2,40,44);
        ssd1306_draw_string(&ssd, "3 - ",8,52); ssd1306_draw_string(&ssd, cor3,40,54);
        } else {
        ssd1306_rect(&ssd,3,3,122,60,true,false);
        ssd1306_line(&ssd,3,25,123,25,true);
        ssd1306_line(&ssd,3,37,123,37,true);
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37",8,6);
        ssd1306_draw_string(&ssd, "EMBARCATECH",20,16);
        ssd1306_draw_string(&ssd, "  Ohmimetro",10,28);
        ssd1306_draw_string(&ssd, "ADC",13,41);
        ssd1306_draw_string(&ssd, "Resisten.",50,41);
        ssd1306_line(&ssd,44,37,44,60,true);
        ssd1306_draw_string(&ssd, str_x,8,52);
        ssd1306_draw_string(&ssd, str_y,59,52);
        }
        ssd1306_send_data(&ssd);
        sleep_ms(500);
     }
 }
 