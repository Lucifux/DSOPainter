#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"
#include "string.h"
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include "raylib.h"

#define IMAGE_PATH "C:\\03 - C Projekte\\0_example_files\\metrohm.ppm"
#define SERIAL_PORT "\\\\.\\COM4"
#define SERIAL_BAUD CBR_115200
#define COLUMN_EMPTY 255

typedef bool pixel_t;

typedef struct {
    uint8_t b;
    uint8_t t;
}column_t;

typedef struct {
    char type[3];
    uint16_t w;
    uint16_t h;
    uint8_t max_color_value;
} header_t;

Color column_display_colors[] = {BLACK, RED, BLUE, YELLOW, PURPLE, ORANGE, PINK, BROWN, GRAY};

HANDLE open_serial_port(const char* port_name) {
    HANDLE serial = CreateFileA(port_name,
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);

    if (serial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Warning: could not open serial port %s\n", port_name);
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial, &dcb)) {
        fprintf(stderr, "Warning: GetCommState failed\n");
        CloseHandle(serial);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = SERIAL_BAUD;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;

    if (!SetCommState(serial, &dcb)) {
        fprintf(stderr, "Warning: SetCommState failed\n");
        CloseHandle(serial);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(serial, &timeouts);

    return serial;
}

void transmit_columns(HANDLE serial, column_t* columns, header_t header, uint8_t num_columns) {
    if (serial == INVALID_HANDLE_VALUE) {
        return;
    }

    char line[64];
    DWORD written = 0;

    for (uint8_t c = 0; c < num_columns; c++){
        for (uint16_t x = 0; x < header.w; x++){
            column_t col = ((column_t*)columns)[(uint32_t)x * num_columns + c];
            uint8_t b = col.b;
            uint8_t t = col.t;

            if (b == COLUMN_EMPTY || t == COLUMN_EMPTY) {
                continue;
            }

            // TX format: X,<x>,<segment>,<bottom>,<top>\n
            int count = snprintf(line, sizeof(line), "X,%u,%u,%u,%u\n", x, c, b, t);
            if (count > 0) {
                WriteFile(serial, line, (DWORD)count, &written, NULL);
            }
        }
    }

    strcpy(line, "END\n");
    WriteFile(serial, line, (DWORD)strlen(line), &written, NULL);
}

FILE* open_image_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    return file;
}

header_t read_header(FILE* file) {
    char buffer[256];
    header_t header;

    fgets(header.type, sizeof(header.type), file);
    fgets(buffer, sizeof(buffer), file);  // NewLine of the type
    fgets(buffer, sizeof(buffer), file);  // Comment 
    fgets(buffer, sizeof(buffer), file);  // w and h 

    unsigned short w = 0, h = 0;
   
    sscanf(buffer, "%hu %hu", &w, &h);
    header.w = (uint16_t)w;
    header.h = (uint16_t)h;
    
    fgets(buffer, sizeof(buffer), file);
    header.max_color_value = (uint8_t)atoi(buffer);

    return header;  
}

pixel_t* get_pixels(FILE* file, header_t header) {
    char line_content[8];
    uint32_t current_line = 0;
    uint16_t color_accumulator = 0;
    uint32_t total_pixels = (uint32_t)header.w * header.h;
    pixel_t* pixels = (pixel_t*)malloc(sizeof(pixel_t) * total_pixels);

    if (pixels == NULL) {
        perror("Error allocating memory for image");
        exit(EXIT_FAILURE);
    }

    while (fgets(line_content, sizeof(line_content), file)) {
        uint8_t current_color = current_line % 3;
        uint32_t current_pixel = current_line / 3;
        if (current_pixel >= total_pixels) break;

        uint32_t x = current_pixel % header.w;
        uint32_t y = header.h - (current_pixel / header.w) - 1;

        if (current_color < 2){
            color_accumulator += (uint16_t)atoi(line_content);
        }else{
            color_accumulator += (uint16_t)atoi(line_content);
            pixels[y * header.w + x] = !(color_accumulator > (header.max_color_value * 3) / 2);
            color_accumulator = 0;
        }
         
        current_line++;
    }

    return pixels;
}



pixel_t* crop_image(pixel_t* pixels, header_t* header) {
    uint16_t min_x = header->w, max_x = 0;
    uint16_t min_y = header->h, max_y = 0;

    for (uint16_t x = 0; x < header->w; x++) {
        for (uint16_t y = 0; y < header->h; y++) {
            if (pixels[y * header->w + x]) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y - 10;
                if (y > max_y) max_y = y + 10;
            }
        }
    }

    uint16_t new_w = max_x - min_x + 1;
    uint16_t new_h = max_y - min_y + 1;
    pixel_t* cropped = (pixel_t*)malloc(sizeof(pixel_t) * new_w * new_h);
    if (cropped == NULL) {
        perror("Error allocating memory for cropped image");
        exit(EXIT_FAILURE);
    }

    for (uint16_t y = 0; y < new_h; y++) {
        for (uint16_t x = 0; x < new_w; x++) {
            cropped[y * new_w + x] = pixels[(y + min_y) * header->w + (x + min_x)];
        }
    }

    free(pixels);
    header->w = new_w;
    header->h = new_h;
    return cropped;
}

pixel_t* resize_image(pixel_t* pixels, header_t* header, uint16_t new_w, uint16_t new_h) {
    pixel_t* resized = (pixel_t*)malloc(sizeof(pixel_t) * new_w * new_h);
    if (resized == NULL) {
        perror("Error allocating memory for resized image");
        exit(EXIT_FAILURE);
    }

    for (uint16_t y = 0; y < new_h; y++) {
        for (uint16_t x = 0; x < new_w; x++) {
            uint16_t orig_x = x * header->w / new_w;
            uint16_t orig_y = y * header->h / new_h;
            resized[y * new_w + x] = pixels[orig_y * header->w + orig_x];
        }
    }

    free(pixels);
    header->w = new_w;
    header->h = new_h;
    return resized;
}

uint8_t get_num_columns(pixel_t* pixels, header_t header){
    uint8_t max = 0;
    uint8_t current = 0;

    for (uint16_t x = 0; x < header.w; x++){
        bool in_collum = false;

        for (uint16_t y = 0; y < header.h; y++){
            if (pixels[y * header.w + x]){
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

column_t* get_columns(pixel_t* pixels, header_t header, uint8_t num_columns) {
    column_t* columns = (column_t*)malloc(sizeof(column_t) * num_columns * header.w);
    if (columns == NULL) {
        perror("Error allocating memory for columns");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < (uint32_t)num_columns * header.w; i++) {
        columns[i].b = COLUMN_EMPTY;
        columns[i].t = COLUMN_EMPTY;
    }

    for (uint16_t x = 0; x < header.w; x++){
        uint8_t current_column = 0;
        bool in_column = false;

        for (uint16_t y = 0; y < header.h; y++){
            if (pixels[y * header.w + x]){
                if (!in_column){
                    in_column = true;
                    uint32_t base = ((uint32_t)x * num_columns + current_column);
                    columns[base].b = (uint8_t)y;
                    columns[base].t = (uint8_t)y;
                }
                columns[((uint32_t)x * num_columns + current_column)].t = (uint8_t)y;
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



void draw_dso_image(column_t* columns, header_t header, uint8_t num_columns) {
    BeginDrawing();
    ClearBackground(WHITE);
    for (int x = 0; x < header.w * 2; x++){
        for (int c = 0; c < num_columns; c++){
            column_t col = columns[(uint32_t)(x / 2) * num_columns + c];
            uint8_t b = col.b;
            uint8_t t = col.t;

            if (b != COLUMN_EMPTY && t != COLUMN_EMPTY){
                DrawLine(x, 2 * header.h - 1 - b * 2, x, 2 * header.h - 1 - t * 2, column_display_colors[c]);
            }
        }
    }
    EndDrawing();
}

int main() {
    FILE* image_file = open_image_file(IMAGE_PATH);
    header_t header = read_header(image_file);
    pixel_t* pixels = get_pixels(image_file, header);
    fclose(image_file);
    
    pixels = crop_image(pixels, &header);
    pixels = resize_image(pixels, &header, 150, 127);

    uint8_t num_columns = get_num_columns(pixels, header);
    column_t* columns = get_columns(pixels, header, num_columns);

    HANDLE serial = open_serial_port(SERIAL_PORT);
    transmit_columns(serial, columns, header, num_columns);

    SetTraceLogLevel(LOG_ERROR);
    InitWindow(header.w * 2, header.h * 2, "Preview");
    while (!WindowShouldClose()) {
        draw_dso_image(columns, header, num_columns);
    }
    CloseWindow();

    free(pixels);
    free(columns);

    if (serial != INVALID_HANDLE_VALUE) {
        CloseHandle(serial);
    }

    return 0;
}