#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include "raylib.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define IMAGE_PATH "C:\\00 - Coding Projects\\05 - DSOPainter\\smiley.png"

#define WIDTH 250
#define HEIGHT 150
#define ZERO_HEIGHT 200

#define PORT 8080

const Color display_colors[] = {BLACK, GREEN, BLUE, RED, PURPLE, ORANGE, YELLOW, MAGENTA, MAROON, DARKGREEN};

typedef struct {
    int top;
    int bottom;
    int left;
    int right;
} image_edge_t;

typedef struct {
    uint8_t b;
    uint8_t t;
} column_t;


image_edge_t get_image_edges(unsigned char pixels[], int width, int height) {
    int top = height;
    int bottom = 0;
    int left = width;
    int right = 0;

    for (int x = 0; x < width - 1; x++) {
        for (int y = height - 1; y >= 0; y--) {
            if (pixels[y * width + x] <= 127) {
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
                left = x < left ? x : left;
                right = x > right ? x : right;
            }
        }
    }

    return (image_edge_t){top -5, bottom + 5, left, right};
}

uint8_t get_num_layers(unsigned char pixels[HEIGHT][WIDTH]){
    uint8_t max = 0;
    uint8_t current = 0;

    for (uint16_t x = 0; x < WIDTH; x++){
        bool in_collum = false;
        for (uint16_t y = 0; y < HEIGHT; y++){
            if (pixels[y][x] <= 127){
                if (!in_collum){
                    in_collum = true;
                    current++;
                }          
            }else{
                in_collum = false;
            }
        }
        if (current > max) {
            max = current;
        }
        current = 0;
    }
    return max;
}
 
column_t* get_columns(unsigned char pixels[HEIGHT][WIDTH], uint8_t num_columns) {
    column_t* columns = malloc(sizeof(column_t) * num_columns * WIDTH);
    if (columns == NULL) {
        perror("Error allocating memory for columns");
        exit(EXIT_FAILURE);
    }
 
    for (uint32_t i = 0; i < num_columns * WIDTH; i++) {
        columns[i].b = ZERO_HEIGHT;
        columns[i].t = ZERO_HEIGHT;
    }
 
    for (uint16_t x = 0; x < WIDTH; x++){
        uint8_t current_column = 0;
        bool in_column = false;
 
        for (uint16_t y = 0; y < HEIGHT; y++){
            if (pixels[y][x] <= 127){
                if (!in_column){
                    in_column = true;
                    uint32_t base = (x * num_columns + current_column);
                    columns[base].b = y;
                    columns[base].t = y;
                }
                columns[x * num_columns + current_column].t = y;
            }else{
                if (in_column){
                    in_column = false;
                    current_column++;
                }
            }
        }
    }
    return columns;
}

void draw_dso_image(column_t* columns, uint8_t num_columns) {
    BeginDrawing();
    ClearBackground(WHITE);
    for (int x = 0; x < WIDTH; x++){
        for (int l = 0; l < num_columns; l++){
            column_t col = columns[x * num_columns + l];
            uint8_t b = col.b;
            uint8_t t = col.t;
 
            if (b != ZERO_HEIGHT && t != ZERO_HEIGHT){
                Color color = display_colors[l % 10];
                DrawLine(x, b, x, t, color);
            }
        }
    }
    EndDrawing();
}

SOCKET init_listen_sock(const short port) {
    SOCKET sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == SOCKET_ERROR) {
        printf("Socket failed: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (listen(sock, 1) == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

int main(void) {
    // Image processing
    int width, height, channels;
    unsigned char *pixels = stbi_load(IMAGE_PATH, &width, &height, &channels, 1);

    if (!pixels) {
        printf("Failed to load image");
        return 1;
    }

    image_edge_t edges = get_image_edges(pixels, width, height);

    unsigned char resized[HEIGHT][WIDTH];
    unsigned char *result = stbir_resize_uint8_linear(
        pixels + edges.top * width + edges.left,
        edges.right - edges.left + 1,
        edges.bottom - edges.top + 1,
        width,          
        &resized[0][0],
        WIDTH,
        HEIGHT,
        WIDTH,            
        STBIR_1CHANNEL
    );

    uint8_t layers = get_num_layers(resized);
    column_t *columns = get_columns(resized, layers);

    SetTraceLogLevel(LOG_ERROR);
    InitWindow(WIDTH, HEIGHT, "Preview");
    SetTargetFPS(10);
    while (!WindowShouldClose()) {
        draw_dso_image(columns, layers);
    }
    CloseWindow();
    stbi_image_free(pixels);

    // Data transmission
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) !=0) {
        printf("WSAStartup failed");
        return 1;
    }

    SOCKET sock_listen = init_listen_sock(PORT);
    if (sock_listen == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    SOCKET sock_client = accept(sock_listen, NULL, NULL);
    if (sock_client == INVALID_SOCKET) {
        printf("Accept failed: %d\n", WSAGetLastError());
        closesocket(sock_listen);
        closesocket(sock_client);
        WSACleanup();
        return 1;
    }

    printf("Pico connected\n\n");
    closesocket(sock_listen);

    char tx[5] = {0};
    for (uint8_t l = 0; l < layers; l++) {
        snprintf(tx, sizeof(tx), "l%u\n", l);
        send(sock_client, tx, strlen(tx), 0);
        for (uint8_t x = 0; x < WIDTH; x++) {
            snprintf(tx, sizeof(tx), "%u\n", columns[x * layers + l].b);
            send(sock_client, tx, strlen(tx), 0);
            snprintf(tx, sizeof(tx), "%u\n", columns[x * layers + l].t);
            send(sock_client, tx, strlen(tx), 0);
        }
        printf("Layer %d sent", l);
    }

    free(columns);
    closesocket(sock_client);
    WSACleanup();

    return 0;
}