#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"


#define WIFI_SSID "lgl-64356-B" 
#define WIFI_PASSWORD "fc99-jiuf-bcbq-zygn"

#define SERVER_IP       "192.168.1.108"
#define SERVER_PORT     8080

#define ZERO_HEIGHT 200
#define MAX_LAYERS 10
#define MAX_WIDTH 500
#define LINE_BUFFER_SIZE 16

typedef struct{
    uint16_t x;
    uint8_t l;
    absolute_time_t layer_start;
}layer_info_t;

typedef struct {
    char line_buffer[LINE_BUFFER_SIZE];
    uint8_t line_length;
    bool discard_line;
    uint8_t current_layer;
    uint16_t current_x;
    bool layer_selected;
} receiver_state_t;

uint8_t frame[MAX_LAYERS][MAX_WIDTH] = {ZERO_HEIGHT}; 
uint8_t layers = 0;
layer_info_t layer_info;

static receiver_state_t receiver_state = {0};

inline uint8_t rev_byte(uint8_t in) {
    in = ((in & 0xAA) >> 1) | ((in & 0x55) << 1);
    in = ((in & 0xCC) >> 2) | ((in & 0x33) << 2);
    in = ((in & 0xF0) >> 4) | ((in & 0x0F) << 4);
    return in;
}

int64_t draw_rising_edge(alarm_id_t id, void* user_data);
int64_t draw_falling_edge(alarm_id_t id, void* user_data);
int64_t draw_layer(alarm_id_t id, void* user_data);
 
int64_t draw_rising_edge(alarm_id_t id, void* user_data) {
    layer_info_t* layer_info = (layer_info_t*)user_data;
    add_alarm_at(layer_info->layer_start + 50, draw_falling_edge, layer_info, true);
    gpio_set_mask(0xFF);
    return 0;
}

int64_t draw_falling_edge(alarm_id_t id, void* user_data){
    layer_info_t* layer_info = (layer_info_t*)user_data;
    add_alarm_at(layer_info->layer_start + 2000, draw_layer, layer_info, true);
    gpio_put_masked(0xFF, rev_byte(ZERO_HEIGHT));
    return 0;

}
 
int64_t draw_layer(alarm_id_t id, void* user_data){
    layer_info_t* layer_info = (layer_info_t*)user_data;
    if (layer_info->x >= MAX_WIDTH){
        layer_info->x = 0;
        layer_info->l = (layer_info->l + 1) % layers;
        layer_info->layer_start = time_us_64() + 2000;
        gpio_put_masked(0xFF, rev_byte(ZERO_HEIGHT));
        add_alarm_at(layer_info->layer_start, draw_rising_edge, layer_info, true);
    }else{
        add_alarm_at(layer_info->layer_start + 2050 + 10 * layer_info->x, draw_layer, layer_info , true);
        gpio_put_masked(0xFF, rev_byte(frame[layer_info->l][layer_info->x]));
        layer_info->x++;
    }
    return 0;
}

void begin_drawing(){
    absolute_time_t now = time_us_64();
 
    layer_info.x = 0;
    layer_info.l = 0;
    layer_info.layer_start = now + 100;
 
    add_alarm_at(layer_info.layer_start, draw_rising_edge, &layer_info, true);
}

static void process_line(receiver_state_t *state)
{
    if (state->line_length == 0) {
        return;
    }

    state->line_buffer[state->line_length] = '\0';

    if (state->line_buffer[0] == 'l') {
        char *number_start = &state->line_buffer[1];
        char *end;

        unsigned long layer = strtoul(number_start, &end, 10);

        if (
            end == number_start ||
            *end != '\0' ||
            layer >= MAX_LAYERS
        ) {
            printf(
                "Invalid layer command: %s\n",
                state->line_buffer
            );

            state->layer_selected = false;
            return;
        }

        layers = layer + 1 > layers ? layer + 1 : layers;

        state->current_layer = (uint8_t)layer;
        state->current_x = 0;
        state->layer_selected = true;

        printf(
            "Receiving layer %u\n",
            (unsigned int)state->current_layer
        );

        return;
    }

    if (!state->layer_selected) {
        printf(
            "Ignoring value before layer selection: %s\n",
            state->line_buffer
        );

        return;
    }

    char *number_start = state->line_buffer;
    char *end;

    unsigned long value = strtoul(number_start, &end, 10);

    if (
        end == number_start ||
        *end != '\0' ||
        value > UINT8_MAX
    ) {
        printf(
            "Invalid pixel value: %s\n",
            state->line_buffer
        );

        return;
    }

    frame[state->current_layer][state->current_x] =
        (uint8_t)value;

    state->current_x++;

    if (state->current_x >= MAX_WIDTH) {
        printf(
            "Layer %u complete\n",
            (unsigned int)state->current_layer
        );

        state->current_x = 0;
        state->layer_selected = false;
    }
}

static void process_received_byte(receiver_state_t *state,char received) {
    if (received == '\n') {
        if (!state->discard_line) {
            process_line(state);
        }

        state->line_length = 0;
        state->discard_line = false;

        return;
    }

    if (state->discard_line) {
        return;
    }

    if (state->line_length < LINE_BUFFER_SIZE - 1) {
        state->line_buffer[state->line_length] = received;
        state->line_length++;
    } else {
        printf("Received line is too long\n");

        state->line_length = 0;
        state->discard_line = true;
    }
}


static err_t tcp_received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    receiver_state_t *state = (receiver_state_t *)arg;

    if (state == NULL) {
        printf("TCP receiver state is NULL\n");

        if (p != NULL) {
            pbuf_free(p);
        }

        return ERR_ARG;
    }

    if (p == NULL) {
        printf("Server closed the connection\n");

        state->layer_selected = false;
        state->current_x = 0;
        state->line_length = 0;
        state->discard_line = false;

        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_arg(pcb, NULL);

        err_t close_error = tcp_close(pcb);

        if (close_error != ERR_OK) {
            printf(
                "tcp_close() failed: %d\n",
                close_error
            );
        }

        for (int x = 0; x < MAX_WIDTH; x++) {
            printf("%u\n", frame[0][x]);
        }
        begin_drawing();
        return ERR_OK;
    }

    if (err != ERR_OK) {
        printf("TCP receive error: %d\n", err);
        pbuf_free(p);

        return err;
    }

    for (
        struct pbuf *buffer = p;
        buffer != NULL;
        buffer = buffer->next
    ) {
        const uint8_t *data =
            (const uint8_t *)buffer->payload;

        for (uint16_t i = 0; i < buffer->len; i++) {
            process_received_byte(
                state,
                (char)data[i]
            );
        }
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}


static void tcp_connection_error(void *arg, err_t err) {
    receiver_state_t *state = (receiver_state_t *)arg;

    printf("TCP connection error: %d\n", err);

    if (state != NULL) {
        state->layer_selected = false;
        state->current_x = 0;
        state->line_length = 0;
        state->discard_line = false;
    }
}

static err_t tcp_connected(
    void *arg,
    struct tcp_pcb *pcb,
    err_t err
)
{
    receiver_state_t *state = (receiver_state_t *)arg;

    if (err != ERR_OK) {
        printf("TCP connection failed: %d\n", err);
        return err;
    }

    if (state == NULL) {
        printf("TCP state is NULL\n");
        return ERR_ARG;
    }

    printf("TCP connected\n");

    state->current_layer = 0;
    state->current_x = 0;
    state->line_length = 0;
    state->discard_line = false;
    state->layer_selected = false;

    tcp_recv(pcb, tcp_received);
    tcp_err(pcb, tcp_connection_error);

    return ERR_OK;
}


int main(void)
{
    stdio_init_all();

    gpio_init_mask(0xFF);
    gpio_set_dir_masked(0xFF, 0xFF);

    gpio_put(0, 1);

    printf("Starting\n");

    if (cyw43_arch_init() != 0) {
        printf("Wi-Fi initialization failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi\n");

    int wifi_result = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        30000
    );

    if (wifi_result != 0) {
        printf(
            "Wi-Fi connection failed: %d\n",
            wifi_result
        );

        cyw43_arch_deinit();
        return 1;
    }

    printf("Wi-Fi connected\n");

    ip_addr_t server_ip;

    if (!ipaddr_aton(SERVER_IP, &server_ip)) {
        printf("Invalid server IP address\n");
        cyw43_arch_deinit();

        return 1;
    }

    cyw43_arch_lwip_begin();

    struct tcp_pcb *pcb = tcp_new();

    if (pcb == NULL) {
        cyw43_arch_lwip_end();

        printf("tcp_new() failed\n");
        cyw43_arch_deinit();

        return 1;
    }

    tcp_arg(pcb, &receiver_state);

    err_t connect_error = tcp_connect(
        pcb,
        &server_ip,
        SERVER_PORT,
        tcp_connected
    );

    cyw43_arch_lwip_end();

    if (connect_error != ERR_OK) {
        printf(
            "tcp_connect() failed immediately: %d\n",
            connect_error
        );

        cyw43_arch_lwip_begin();
        tcp_arg(pcb, NULL);
        tcp_abort(pcb);
        cyw43_arch_lwip_end();

        cyw43_arch_deinit();
        return 1;
    }

    while (true) {
        sleep_ms(1000);
    }
}