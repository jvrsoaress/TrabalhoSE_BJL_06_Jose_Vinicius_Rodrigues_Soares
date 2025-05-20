// Painel de Automação Residencial Inteligente - parte 2
// Controla LEDs RGB e matriz WS2812 dividida em 4 cômodos, com webserver HTTP otimizado
// Desenvolvido por José Vinicius

#include <stdio.h>                     // biblioteca padrão para entrada/saída 
#include <string.h>                    // manipulação de strings 
#include <stdlib.h>                    // funções padrão 
#include "pico/stdlib.h"               // funções básicas do Pico SDK 
#include "hardware/gpio.h"             // controle de GPIOs 
#include "hardware/i2c.h"              // comunicação I2C para o display OLED 
#include "hardware/adc.h"              // leitura do ADC para sensor de temperatura interno
#include "pico/cyw43_arch.h"           // suporte ao módulo Wi-Fi CYW43439 
#include "lwip/pbuf.h"                 // buffers de dados para comunicação TCP 
#include "lwip/tcp.h"                  // protocolo TCP para implementar o webserver
#include "lwip/netif.h"                // interface de rede para obter endereço IP
#include "generated/ws2812.pio.h"      // controlar matriz WS2812 via PIO
#include "lib/ssd1306.h"               // biblioteca para display OLED SSD1306 

// credenciais Wi-Fi
#define WIFI_SSID "Apartamento 01"     // ssid (nome) da rede Wi-Fi para conexão
#define WIFI_PASSWORD "12345678"       // senha da rede Wi-Fi para autenticação

// definições de pinos
#define BUTTON_A 5                     // gpio para botão A (alterna cômodos ou desliga LEDs com pressão longa)
#define BUTTON_B 6                     // GPIO para Botão B (desliga emergência)
#define WS2812_PIN 7                   // GPIO para matriz de LEDs WS2812
#define BUZZER 10                      // GPIO para buzzer (alarme de emergência)
#define LED_G 11                       // GPIO do LED RGB verde
#define LED_B 12                       // GPIO do LED RGB azul
#define LED_R 13                       // GPIO do LED RGB vermelho
#define I2C_SDA 14                     // GPIO para pino SDA do I2C (OLED)
#define I2C_SCL 15                     // GPIO para pino SCL do I2C (OLED)  
#define I2C_PORT i2c1                  // porta I2C usada para o display OLED
#define OLED_ADDRESS 0x3C              // endereço I2C do display OLED SSD1306
#define JOYSTICK 22                    // GPIO para botão do joystick (muda cor)
#define WIDTH 128                      // largura do display OLED 
#define HEIGHT 64                      // altura do display OLED 

// variáveis globais
typedef enum { VERMELHO, VERDE, AZUL, AMARELO, CIANO, LILAS } Cor; // enum para representar cores do LED RGB e matriz
typedef enum { QUARTO_1, QUARTO_2, COZINHA, BANHEIRO } Comodo;   // enum para representar os 4 cômodos controlados
static Cor cor_atual = VERMELHO;       // cor inicial do LED RGB e matriz (
static Comodo comodo_atual = QUARTO_1;// cômodo inicial 
static bool led_ligado = false;        // estado inicial do LED RGB e matriz (desligado)
static bool emergencia = false;        // estado inicial do modo de emergência (desativado)
static ssd1306_t disp;                 // estrutura para controlar o display OLED 
static uint32_t ultima_leitura_temperatura = 0; // timestamp da última leitura de temperatura
static uint32_t ultimo_botao = 0;      // timestamp da última verificação de botões
static uint32_t ultima_atualizacao_oled = 0; // timestamp da última atualização do OLED
static uint32_t ultimo_buzzer = 0;     // timestamp da última alternância do buzzer
static uint32_t botao_a_pressao_inicio = 0; // timestamp do início da pressão do botão A
static bool botao_a_pressionado = false; // estado do botão A 

// mapeamento da matriz de LEDS
static const int pixel_map[5][5] = {   // índices dos LEDs na matriz 
    {24, 23, 22, 21, 20},            // Linha 1: índices dos LEDs
    {15, 16, 17, 18, 19},            // Linha 2: índices dos LEDs
    {14, 13, 12, 11, 10},            // Linha 3: índices dos LEDs
    {5,  6,  7,  8,  9},             // Linha 4: índices dos LEDs
    {4,  3,  2,  1,  0}              // Linha 5: índices dos LEDs
}; 

// LEDs por cômodo (4 LEDs por cômodo, total de 16 LEDs)
static const int comodos[4][4] = {     // mapeamento dos LEDs para cada cômodo
    {24, 23, 15, 16},                  // quarto 1: canto superior esquerdo
    {21, 20, 18, 19},                  // quarto 2: canto superior direito
    {5, 6, 4, 3},                      // cozinha: canto inferior esquerdo
    {8, 9, 1, 0}                       // banheiro: canto inferior direito
};

// LEDs da cruz central (9 LEDs, brancos fixos)
static const int cruz[] = {22, 17, 12, 7, 2, 14, 13, 11, 10}; // índices dos LEDs que formam uma cruz no centro

// protótipos de funções
void inicializar_perifericos(void);     // inicializa GPIOs para LED RGB, botões, e buzzer
float ler_temperatura(void);            // lê temperatura do sensor interno via ADC
void configurar_led_rgb(Cor cor, bool estado); // configura LED RGB com cor e estado
void atualizar_matriz(void);            // atualiza matriz WS2812 com base no cômodo, cor e estado
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err); // aceita conexões TCP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err); // processa requisições HTTP
void processar_requisicao(char *requisicao, uint16_t len); // interpreta comandos HTTP
void atualizar_display(void);           // atualiza display OLED com informações do sistema

// função principal
int main() {
    stdio_init_all();                   // inicializa UART para logs no Serial Monitor
    sleep_ms(2000);                     // aguarda 2 segundos para estabilizar a inicialização

    // inicializa periféricos e sensores
    inicializar_perifericos();          // configura GPIOs para LED RGB, botões, e buzzer
    adc_init();                         // inicializa módulo ADC para leitura de temperatura
    adc_set_temp_sensor_enabled(true);  // ativa sensor de temperatura interno do RP2040

    // inicializa I2C e OLED
    i2c_init(I2C_PORT, 400 * 1000);     // configura I2C a 400kHz para comunicação rápida
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // define pino SDA como função I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // define pino SCL como função I2C
    gpio_pull_up(I2C_SDA);              // habilita pull-up interno para SDA
    gpio_pull_up(I2C_SCL);              // habilita pull-up interno para SCL
    sleep_ms(500);                      // aguarda 500ms para estabilizar I2C
    ssd1306_init(&disp, WIDTH, HEIGHT, false, OLED_ADDRESS, I2C_PORT); // inicializa estrutura do OLED
    ssd1306_config(&disp);              // configura parâmetros do display OLED
    ssd1306_fill(&disp, 0);             // limpa o buffer do display
    ssd1306_send_data(&disp);           // envia buffer inicial ao OLED

    // inicializa WS2812
    PIO pio = pio0;                     // usa PIO0 para controlar a matriz WS2812
    uint offset = pio_add_program(pio, &ws2812_program); // carrega programa PIO para WS2812
    ws2812_program_init(pio, 0, offset, WS2812_PIN, 800000, false); // inicializa WS2812 

    // inicializa Wi-Fi
    if (cyw43_arch_init()) {            // inicializa módulo Wi-Fi CYW43439
        printf("Falha na inicialização do Wi-Fi\n"); // loga erro no Serial Monitor
        return -1;                      // encerra programa em caso de falha
    }
    cyw43_arch_enable_sta_mode();       // ativa modo estação (cliente Wi-Fi)
    printf("Conectando ao Wi-Fi...\n"); // loga tentativa de conexão
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) { // tenta conectar com timeout de 20s
        printf("Falha na conexão Wi-Fi\n"); // loga erro se a conexão falhar
        return -1;                      // encerra programa em caso de falha
    }
    printf("Conectado ao Wi-Fi\n");     // confirma conexão bem-sucedida
    if (netif_default) {                // verifica se a interface de rede está ativa
        printf("IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr)); // exibe endereço IP no Serial Monitor
    }

    // configura servidor TCP
    struct tcp_pcb *server = tcp_new(); // cria um novo PCB (Protocol Control Block) para o webserver
    if (!server) {                      // verifica se a criação do PCB falhou
        printf("Falha na criação do servidor TCP\n"); // loga erro
        return -1;                      // encerra programa em caso de falha
    }
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) { // associa o servidor à porta 80 (HTTP)
        printf("Falha no bind TCP\n");  // loga erro se o bind falhar
        return -1;                      // encerra programa em caso de falha
    }
    server = tcp_listen(server);        // coloca o servidor em modo escuta
    tcp_accept(server, tcp_server_accept); // define callback para aceitar conexões
    printf("Servidor escutando na porta 80\n\n"); // loga que o servidor está ativo

    // loop principal
    while (true) {
        cyw43_arch_poll();              // processa eventos de rede (lwIP) para manter o webserver ativo
        uint32_t agora = to_ms_since_boot(get_absolute_time()); // obtém tempo atual em milissegundos

        // verifica botões a cada 10ms
        if (agora - ultimo_botao >= 10) { // verifica botões a cada 10ms para responsividade
            static bool botao_joystick_pressionado = false; // estado anterior do joystick
            static bool botao_b_pressionado = false; // estado anterior do botão B

            bool estado_joystick = !gpio_get(JOYSTICK); // lê estado do joystick 
            bool estado_botao_a = !gpio_get(BUTTON_A);  // lê estado do botão A 
            bool estado_botao_b = !gpio_get(BUTTON_B);  // lê estado do botão B 

            // joystick: alterna cores
            if (estado_joystick && !botao_joystick_pressionado) { // detecta nova pressão do joystick
                cor_atual = (cor_atual + 1) % 6; // cicla para a próxima cor (0 a 5)
                printf("Botão Joystick: cor alterada para %s\n\n", // loga a nova cor
                       cor_atual == VERMELHO ? "vermelho" :
                       cor_atual == VERDE ? "verde" :
                       cor_atual == AZUL ? "azul" :
                       cor_atual == AMARELO ? "amarelo" :
                       cor_atual == CIANO ? "ciano" : "lilás");
                botao_joystick_pressionado = true; // marca joystick como pressionado
                sleep_ms(200);                 // debounce de 200ms para evitar múltiplas leituras
            } else if (!estado_joystick) {     // joystick liberado
                botao_joystick_pressionado = false; // reseta estado do joystick
            }

            // botão A: alterna cômodos ou desliga com pressão longa
            if (estado_botao_a && !botao_a_pressionado) { // detecta nova pressão do botão A
                botao_a_pressionado = true;    // marca botão A como pressionado
                botao_a_pressao_inicio = agora; // registra timestamp do início da pressão
                printf("Botão A: pressionado\n\n"); // loga ação
            } else if (estado_botao_a && botao_a_pressionado) { // botão A mantido pressionado
                if (agora - botao_a_pressao_inicio >= 3000) { // se pressão longa (≥3s)
                    led_ligado = false;        // desliga LEDs do cômodo
                    printf("Botão A: LEDs do cômodo desligados (pressão longa)\n\n"); // loga ação
                }
            } else if (!estado_botao_a && botao_a_pressionado) { // botão A liberado
                if (agora - botao_a_pressao_inicio < 3000) { // se pressão curta (<3s)
                    comodo_atual = (comodo_atual + 1) % 4; // cicla para o próximo cômodo
                    led_ligado = true;         // liga LEDs do novo cômodo
                    printf("Botão A: cômodo alterado para %s\n\n", // loga mudança de cômodo
                           comodo_atual == QUARTO_1 ? "Quarto 1" :
                           comodo_atual == QUARTO_2 ? "Quarto 2" :
                           comodo_atual == COZINHA ? "Cozinha" : "Banheiro");
                }
                botao_a_pressionado = false;   // reseta estado do botão A
                sleep_ms(200);                 // debounce de 200ms
            }

            // botão B: desliga emergência
            if (estado_botao_b && !botao_b_pressionado) { // detecta nova pressão do botão B
                emergencia = false;            // desativa modo de emergência
                printf("Botão B: alarme desligado\n\n"); // loga ação
                botao_b_pressionado = true;    // marca botão B como pressionado
                sleep_ms(200);                 // debounce de 200ms
            } else if (!estado_botao_b) {      // botão B liberado
                botao_b_pressionado = false;   // reseta estado do botão B
            }

            ultimo_botao = agora;              // atualiza timestamp da verificação de botões
        }

        // lê temperatura a cada 1000ms
        if (agora - ultima_leitura_temperatura >= 1000) { // verifica temperatura a cada 1s
            float temperatura = ler_temperatura(); // lê temperatura do sensor interno
            if (temperatura > 40.0f) {         // se temperatura exceder 40°C
                emergencia = true;             // ativa modo de emergência
            }
            ultima_leitura_temperatura = agora; // atualiza timestamp da leitura
        }

        // atualiza display a cada 1000ms
        if (agora - ultima_atualizacao_oled >= 1000) { // atualiza OLED a cada 1s
            atualizar_display();                // exibe cômodo, temperatura, emergência e IP
            ultima_atualizacao_oled = agora;   // atualiza timestamp do OLED
        }

        // controla buzzer em emergência
        if (emergencia && agora - ultimo_buzzer >= 1000) { // se emergência ativa, alterna buzzer a cada 1s
            gpio_put(BUZZER, !gpio_get(BUZZER)); // inverte estado do buzzer (liga/desliga)
            ultimo_buzzer = agora;             // atualiza timestamp do buzzer
        } else if (!emergencia && gpio_get(BUZZER)) { // se emergência desativada e buzzer ligado
            gpio_put(BUZZER, 0);               // desliga buzzer
        }

        // atualiza LED RGB e matriz
        if (!emergencia) {                     // se não estiver em emergência
            configurar_led_rgb(cor_atual, led_ligado); // configura LED RGB com cor atual e estado
        } else {                               // em emergência
            configurar_led_rgb(cor_atual, false); // desliga LED RGB
        }
        atualizar_matriz();                     // atualiza matriz WS2812 (cômodo + cruz)

        sleep_ms(10);                          // delay de 10ms para evitar sobrecarga do loop
    }

    cyw43_arch_deinit();                       // desinicializa Wi-Fi 
    return 0;                                  // retorno padrão 
}

// inicializa periféricos
void inicializar_perifericos(void) {
    gpio_init(LED_R);                          // inicializa GPIO do LED vermelho
    gpio_set_dir(LED_R, GPIO_OUT);             // define como saída
    gpio_put(LED_R, 0);                        // desliga LED vermelho
    gpio_init(LED_G);                          // inicializa GPIO do LED verde
    gpio_set_dir(LED_G, GPIO_OUT);             // define como saída
    gpio_put(LED_G, 0);                        // desliga LED verde
    gpio_init(LED_B);                          // inicializa GPIO do LED azul
    gpio_set_dir(LED_B, GPIO_OUT);             // define como saída
    gpio_put(LED_B, 0);                        // desliga LED azul
    gpio_init(JOYSTICK);                       // inicializa GPIO do joystick
    gpio_set_dir(JOYSTICK, GPIO_IN);           // define como entrada
    gpio_pull_up(JOYSTICK);                    // habilita pull-up interno
    gpio_init(BUTTON_A);                       // inicializa GPIO do botão A
    gpio_set_dir(BUTTON_A, GPIO_IN);           // define como entrada
    gpio_pull_up(BUTTON_A);                    // habilita pull-up interno
    gpio_init(BUTTON_B);                       // inicializa GPIO do botão B
    gpio_set_dir(BUTTON_B, GPIO_IN);           // define como entrada
    gpio_pull_up(BUTTON_B);                    // habilita pull-up interno
    gpio_init(BUZZER);                         // inicializa GPIO do buzzer
    gpio_set_dir(BUZZER, GPIO_OUT);            // define como saída
    gpio_put(BUZZER, 0);                       // desliga buzzer
}

// lê temperatura do sensor interno
float ler_temperatura(void) {
    adc_select_input(4);                       // seleciona canal 4 do ADC
    uint16_t valor_bruto = adc_read();         // lê valor bruto (12 bits, 0-4095)
    const float fator_conversao = 3.3f / (1 << 12); // fator para converter ADC para tensão (Vref = 3.3V)
    float temperatura = 27.0f - ((valor_bruto * fator_conversao) - 0.706f) / 0.001721f; // converte para °C (equação do RP2040)
    return temperatura;                        // retorna temperatura em °C
}

// configura LED RGB
void configurar_led_rgb(Cor cor, bool estado) {
    uint8_t r = 0, g = 0, b = 0;              // inicializa componentes RGB como 0
    if (estado) {                              // se LED deve estar ligado
        switch (cor) {                         // define valores RGB com base na cor
            case VERMELHO: r = 32; break;     // vermelho
            case VERDE: g = 32; break;         // verde
            case AZUL: b = 32; break;          // azul
            case AMARELO: r = 32; g = 32; break; // amarelo
            case CIANO: g = 32; b = 32; break; // ciano
            case LILAS: r = 32; b = 32; break; // lilás
        }
    }
    gpio_put(LED_R, r > 0);                    // liga/desliga vermelho
    gpio_put(LED_G, g > 0);                    // liga/desliga verde
    gpio_put(LED_B, b > 0);                    // liga/desliga azul
}

// atualiza matriz de LEDs WS2812
void atualizar_matriz(void) {
    uint32_t pixels[25] = {0};                 // inicializa array de 25 LEDs como apagados
    // configura cruz branca (RGB 10, 10, 10)
    for (int i = 0; i < 9; i++) {             // itera pelos 9 LEDs da cruz
        pixels[cruz[i]] = ((uint32_t)(10) << 8) | ((uint32_t)(10) << 16) | (uint32_t)(10); // define cor branca
    }
    // configura LEDs do cômodo atual
    if (emergencia) {                          // se emergência ativa
        for (int i = 0; i < 4; i++) {         // itera pelos 4 LEDs do cômodo
            pixels[comodos[comodo_atual][i]] = ((uint32_t)(32) << 8) | ((uint32_t)(0) << 16) | (uint32_t)(0); // define cor vermelha
        }
        printf("Matriz: Emergência ativada, cômodo %d em vermelho\n", comodo_atual); // loga ação
    } else if (led_ligado) {                   // se LEDs ligados e sem emergência
        uint8_t r = 0, g = 0, b = 0;          // inicializa componentes RGB
        switch (cor_atual) {                   // define valores RGB com base na cor atual
            case VERMELHO: r = 32; break;     // vermelho
            case VERDE: g = 32; break;         // verde
            case AZUL: b = 32; break;          // azul
            case AMARELO: r = 32; g = 32; break; // amarelo
            case CIANO: g = 32; b = 32; break; // ciano
            case LILAS: r = 32; b = 32; break; // lilás
        }
        for (int i = 0; i < 4; i++) {         // itera pelos 4 LEDs do cômodo
            pixels[comodos[comodo_atual][i]] = ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b); // define cor
        }
    }
    // envia dados para a matriz WS2812
    for (int i = 0; i < 25; i++) {            // itera pelos 25 LEDs
        pio_sm_put_blocking(pio0, 0, pixels[i] << 8u); // envia valor RGB para WS2812 via PIO
    }
}

// callback de aceitação de conexão TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);         // define callback para processar requisições recebidas
    return ERR_OK;                             // aceita conexão
}

// processa requisições HTTP
void processar_requisicao(char *requisicao, uint16_t len) {
    if (strstr(requisicao, "GET /led_on")) {   // verifica requisição para ligar LED
        led_ligado = true;                     // ativa LED
    } else if (strstr(requisicao, "GET /led_off")) { // verifica requisição para desligar LED
        led_ligado = false;                    // desativa LED
    } else if (strstr(requisicao, "GET /color_red")) { // verifica requisição para cor vermelha
        cor_atual = VERMELHO;                 // define cor como vermelho
    } else if (strstr(requisicao, "GET /color_green")) { // verifica requisição para cor verde
        cor_atual = VERDE;                     // define cor como verde
    } else if (strstr(requisicao, "GET /color_blue")) { // verifica requisição para cor azul
        cor_atual = AZUL;                      // define cor como azul
    } else if (strstr(requisicao, "GET /color_yellow")) { // verifica requisição para cor amarela
        cor_atual = AMARELO;                   // define cor como amarelo
    } else if (strstr(requisicao, "GET /color_cyan")) { // verifica requisição para cor ciano
        cor_atual = CIANO;                     // define cor como ciano
    } else if (strstr(requisicao, "GET /color_lilas")) { // verifica requisição para cor lilás
        cor_atual = LILAS;                     // define cor como lilás
    } else if (strstr(requisicao, "GET /alarm_off")) { // verifica requisição para desligar alarme
        emergencia = false;                    // desativa emergência
    } else if (strstr(requisicao, "GET /room1")) { // verifica requisição para selecionar Quarto 1
        comodo_atual = QUARTO_1;              // define cômodo como Quarto 1
        led_ligado = true;                     // liga LEDs do cômodo
    } else if (strstr(requisicao, "GET /room2")) { // verifica requisição para selecionar Quarto 2
        comodo_atual = QUARTO_2;              // define cômodo como Quarto 2
        led_ligado = true;                     // liga LEDs do cômodo
    } else if (strstr(requisicao, "GET /room3")) { // verifica requisição para selecionar Cozinha
        comodo_atual = COZINHA;                // define cômodo como Cozinha
        led_ligado = true;                     // liga LEDs do cômodo
    } else if (strstr(requisicao, "GET /room4")) { // verifica requisição para selecionar Banheiro
        comodo_atual = BANHEIRO;               // define cômodo como Banheiro
        led_ligado = true;                     // liga LEDs do cômodo
    }
}

// callback de recebimento de dados TCP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {                                  // se não há dados (conexão fechada)
        tcp_close(tpcb);                       // fecha conexão
        tcp_recv(tpcb, NULL);                  // remove callback de recebimento
        return ERR_OK;                         // retorna sucesso
    }

    char requisicao[256];                      // buffer estático para evitar fragmentação
    uint16_t len = p->len < 255 ? p->len : 255; // limita tamanho da requisição a 255 bytes
    memcpy(requisicao, p->payload, len);       // copia dados da requisição
    requisicao[len] = '\0';                    // adiciona terminador nulo à string

    // log de requisições
    if (strstr(requisicao, "GET /led_on")) {   // requisição para ligar LED
        printf("Requisição: led ligado\n\n");  // loga ação no Serial Monitor
    } else if (strstr(requisicao, "GET /led_off")) { // requisição para desligar LED
        printf("Requisição: led desligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_red")) { // requisição para cor vermelha
        printf("Requisição: led vermelho ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_green")) { // requisição para cor verde
        printf("Requisição: led verde ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_blue")) { // requisição para cor azul
        printf("Requisição: led azul ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_yellow")) { // requisição para cor amarela
        printf("Requisição: led amarelo ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_cyan")) { // requisição para cor ciano
        printf("Requisição: led ciano ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /color_lilas")) { // requisição para cor lilás
        printf("Requisição: led lilás ligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /alarm_off")) { // requisição para desligar alarme
        printf("Requisição: alarme desligado\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /room1")) { // requisição para selecionar Quarto 1
        printf("Requisição: selecionado Quarto 1\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /room2")) { // requisição para selecionar Quarto 2
        printf("Requisição: selecionado Quarto 2\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /room3")) { // requisição para selecionar Cozinha
        printf("Requisição: selecionado Cozinha\n\n"); // loga ação
    } else if (strstr(requisicao, "GET /room4")) { // requisição para selecionar Banheiro
        printf("Requisição: selecionado Banheiro\n\n"); // loga ação
    }

    processar_requisicao(requisicao, len);     // processa a requisição para atualizar estados
    float temperatura = ler_temperatura();     // lê temperatura atual para exibir no HTML

    char html[2000];                           // buffer para página HTML 
    snprintf(html, sizeof(html),               // formata página HTML com estado atual
             "HTTP/1.1 200 OK\r\n"            // status HTTP 200
             "Content-Type: text/html\r\n"    // tipo de conteúdo: HTML
             "\r\n"                           // fim do cabeçalho HTTP
             "<!DOCTYPE html>"                // declaração DOCTYPE para HTML5
             "<html>"                         // início do documento HTML
             "<head>"                         // cabeçalho HTML
             "<meta charset=\"UTF-8\">"       // define codificação UTF-8 para acentos
             "<title>Painel Casa Inteligente</title>" // título da página
             "<style>"                        // estilos CSS
             "body{background:#f0f8ff;color:#333;text-align:center;padding:10px;}" // corpo: fundo azul claro, texto centralizado
             "h3{color:#2c3e50;margin:10px 0;}" // título h3 com cor escura
             ".section{margin:10px 0;padding:5px;border:1px solid #ccc;border-radius:5px;}" // seções com borda e arredondamento
             ".section h4{font-size:1.1em;color:#34495e;margin:5px 0;}" // subtítulos h4 nas seções
             "button{background:#3498db;color:white;border:none;padding:5px 10px;border-radius:3px;margin:2px;cursor:pointer;}" // botões azuis
             "button:hover{background:#2980b9;}" // hover dos botões: azul mais escuro
             ".off{background:#e74c3c;}"      // botões "desligar": vermelho
             ".off:hover{background:#c0392b;}" // hover dos botões "desligar": vermelho escuro
             ".on{background:#27ae60;}"       // botão "ligar": verde
             ".on:hover{background:#219653;}" // hover do botão "ligar": verde escuro
             ".status{background:#ecf0f1;padding:5px;border-radius:3px;margin-top:10px;}" // seção de status: fundo cinza claro
             "p{margin:3px 0;}"               // parágrafos com margem pequena
             "</style>"                       // fim dos estilos
             "</head>"                        // fim do cabeçalho
             "<body>"                         // início do corpo HTML
             "<h3>Painel Casa Inteligente</h3>" // título principal
             "<div class=\"section\">"        // seção de cômodos
             "<h4>Cômodos</h4>"              // subtítulo da seção
             "<form action=\"./room1\"><button>Quarto 1</button></form>" // botão para Quarto 1
             "<form action=\"./room2\"><button>Quarto 2</button></form>" // botão para Quarto 2
             "<form action=\"./room3\"><button>Cozinha</button></form>" // botão para Cozinha
             "<form action=\"./room4\"><button>Banheiro</button></form>" // botão para Banheiro
             "</div>"                         // fim da seção de cômodos
             "<div class=\"section\">"        // seção de controle de LEDs
             "<h4>Controle de LEDs</h4>"     // subtítulo da seção
             "<form action=\"./led_on\"><button class=\"on\">Ligar LED</button></form>" // botão para ligar LED
             "<form action=\"./led_off\"><button class=\"off\">Desligar LED</button></form>" // botão para desligar LED
             "</div>"                         // fim da seção de controle
             "<div class=\"section\">"        // seção de cores
             "<h4>Cores</h4>"                // subtítulo da seção
             "<form action=\"./color_red\"><button>Vermelho</button></form>" // botão para cor vermelha
             "<form action=\"./color_green\"><button>Verde</button></form>" // botão para cor verde
             "<form action=\"./color_blue\"><button>Azul</button></form>" // botão para cor azul
             "<form action=\"./color_yellow\"><button>Amarelo</button></form>" // botão para cor amarela
             "<form action=\"./color_cyan\"><button>Ciano</button></form>" // botão para cor ciano
             "<form action=\"./color_lilas\"><button>Lilás</button></form>" // botão para cor lilás
             "</div>"                         // fim da seção de cores
             "<div class=\"section\">"        // seção de alarme
             "<h4>Alarme</h4>"               // subtítulo da seção
             "<form action=\"./alarm_off\"><button class=\"off\">Desligar Alarme</button></form>" // botão para desligar alarme
             "</div>"                         // fim da seção de alarme
             "<div class=\"section status\">" // seção de status
             "<h4>Status</h4>"               // subtítulo da seção
             "<p>LED: %s</p>"                // exibe estado do LED (LIGADO/DESLIGADO)
             "<p>Cor: %s</p>"                // exibe cor atual
             "<p>Temperatura: %.1fC</p>"     // exibe temperatura
             "<p>Emergência: %s</p>"         // exibe estado da emergência (LIGADA/DESLIGADA)
             "</div>"                         // fim da seção de status
             "</body>"                        // fim do corpo
             "</html>",                       // fim do documento HTML
             led_ligado ? "LIGADO" : "DESLIGADO", // estado do LED
             cor_atual == VERMELHO ? "Vermelho" : // nome da cor atual
             cor_atual == VERDE ? "Verde" :
             cor_atual == AZUL ? "Azul" :
             cor_atual == AMARELO ? "Amarelo" :
             cor_atual == CIANO ? "Ciano" : "Lilás",
             temperatura,                     // valor da temperatura
             emergencia ? "LIGADA" : "DESLIGADA"); // estado da emergência

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY); // envia página HTML ao cliente
    tcp_output(tpcb);                         // força envio dos dados
    pbuf_free(p);                             // libera buffer da requisição
    return ERR_OK;                            // retorna sucesso
}

// atualiza display OLED
void atualizar_display(void) {
    char temp_str[20];                        // buffer para string do cômodo/temperatura
    char ip_str[16];                          // buffer para string do endereço IP
    ssd1306_fill(&disp, 0);                   // limpa o buffer do display
    snprintf(temp_str, sizeof(temp_str), "%s", // formata nome do cômodo atual
             comodo_atual == QUARTO_1 ? "QUARTO 1" :
             comodo_atual == QUARTO_2 ? "QUARTO 2" :
             comodo_atual == COZINHA ? "COZINHA" : "BANHEIRO");
    ssd1306_draw_string(&disp, temp_str, 20, 2); // exibe cômodo na linha 1
    snprintf(temp_str, sizeof(temp_str), "TEMP: %.1fC", ler_temperatura()); // formata temperatura
    ssd1306_draw_string(&disp, temp_str, 20, 18); // exibe temperatura na linha 2
    ssd1306_draw_string(&disp, emergencia ? "EMERGENCIA: ON" : "EMERGENCIA: OFF", 2, 34); // exibe estado da emergência na linha 3
    snprintf(ip_str, sizeof(ip_str), "%s", netif_default ? ipaddr_ntoa(&netif_default->ip_addr) : "N/A"); // formata endereço IP
    ssd1306_draw_string(&disp, ip_str, 6, 50); // exibe IP na linha 4
    ssd1306_send_data(&disp);                 // envia buffer ao display OLED
}