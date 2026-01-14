#include <ImageStreamIO/ImageStreamIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Default FPS
#define DEFAULT_FPS 100

typedef enum {
    ROTATION_MODE_FIXED,
    ROTATION_MODE_OSCILLATING
} RotationMode;

void print_help(const char* prog_name) {
    printf("Usage: %s [options] [stream_name]\n", prog_name);
    printf("Options:\n");
    printf("  -h          Show this help message\n");
    printf("  -s <speed>  Rotation speed (radians per frame, default: 0.02)\n");
    printf("  -m <mode>   Rotation mode: 0 for fixed, 1 for oscillating (default: 1)\n");
    printf("  -a <angle>  Tilt angle in degrees for fixed mode (default: 0.0)\n");
    printf("  -w <width>  Image width (default: 256)\n");
    printf("  -e <height> Image height (default: 256)\n");
    printf("  -f <fps>    Framerate in Hz (default: 100)\n");
    printf("\n");
    printf("Default stream name: 'earth'\n");
}

int main(int argc, char **argv) {
    // Defaults
    const char *stream_name = "earth";
    int width = 256;
    int height = 256;
    float speed = 0.02f;
    RotationMode mode = ROTATION_MODE_OSCILLATING;
    float fixed_tilt_deg = 0.0f;
    int fps = DEFAULT_FPS;
    
    int opt;
    while ((opt = getopt(argc, argv, "hs:m:a:w:e:f:")) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                return 0;
            case 's':
                speed = strtof(optarg, NULL);
                break;
            case 'm':
                if (atoi(optarg) == 0) mode = ROTATION_MODE_FIXED;
                else mode = ROTATION_MODE_OSCILLATING;
                break;
            case 'a':
                fixed_tilt_deg = strtof(optarg, NULL);
                break;
            case 'w':
                width = atoi(optarg);
                break;
            case 'e':
                height = atoi(optarg);
                break;
            case 'f':
                fps = atoi(optarg);
                if (fps <= 0) fps = 1;
                break;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    // Check for positional arg (stream name)
    if (optind < argc) {
        stream_name = argv[optind];
    }
    
    // Dynamic geometry calculation
    float cx = width / 2.0f - 0.5f;
    float cy = height / 2.0f - 0.5f;
    // Radius is slightly smaller than half the min dimension
    float radius = (width < height ? width : height) / 2.0f * 0.78f; // ~100.0 for 256

    // Load Earth Map
    int map_w, map_h, map_ch;
    unsigned char *map_data = NULL;
    
    // Try to locate the map file in likely locations
    const char* paths[] = {
        "earth_map.jpg",
        "viewer/src/earth_map.jpg",
        "src/earth_map.jpg",
        "../viewer/src/earth_map.jpg",
        "../src/earth_map.jpg"
    };
    
    for (int i = 0; i < 5; i++) {
        map_data = stbi_load(paths[i], &map_w, &map_h, &map_ch, 3);
        if (map_data) {
            printf("Loaded Earth map from %s: %dx%d (%d channels)\n", paths[i], map_w, map_h, map_ch);
            break;
        }
    }

    if (!map_data) {
        fprintf(stderr, "Failed to load earth_map.jpg from any standard path.\n");
        return 1;
    }

    IMAGE image;
    uint32_t dims[2] = {(uint32_t)width, (uint32_t)height};

    // Create shared memory image
    // 2D, Float, Shared=1, NBkw=0, CBsize=0
    if (ImageStreamIO_createIm(&image, stream_name, 2, dims, _DATATYPE_FLOAT, 1, 0, 0) != 0) {
        fprintf(stderr, "Error creating stream %s\n", stream_name);
        stbi_image_free(map_data);
        return 1;
    }

    printf("Stream '%s' created (%dx%d) at %d FPS. Press Ctrl+C to stop.\n", stream_name, width, height, fps);

    float rotation = 0.0f;
    
    // Period = 50 rotations for full tilt cycle if oscillating
    float tilt_speed = (2.0f * M_PI) / (50.0f * (2.0f * M_PI / (speed > 0 ? speed : 0.02f)));
    float tilt_phase = 0.0f;

    // Light direction (from top left front)
    float lx = -0.5f;
    float ly = -0.5f;
    float lz = 1.0f;
    // Normalize light
    float l_len = sqrt(lx*lx + ly*ly + lz*lz);
    lx /= l_len; ly /= l_len; lz /= l_len;

    while(1) {
        float *data = (float*)image.array.raw;

        float tilt_deg;
        if (mode == ROTATION_MODE_OSCILLATING) {
            // Tilt between 0 and 90 degrees (0 to PI/2 radians)
            tilt_deg = 45.0f + 45.0f * sin(tilt_phase);
        } else {
            tilt_deg = fixed_tilt_deg;
        }
        
        float tilt_rad = tilt_deg * (M_PI / 180.0f);

        float cos_t = cos(tilt_rad);
        float sin_t = sin(tilt_rad);
        float cos_r = cos(rotation);
        float sin_r = sin(rotation);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float dx = x - cx;
                float dy = y - cy;
                float r2 = dx*dx + dy*dy;

                if (r2 > radius*radius) {
                    data[y * width + x] = 0.0f;
                } else {
                    float z = sqrt(radius*radius - r2);

                    // Normalize sphere normal
                    float nx = dx / radius;
                    float ny = dy / radius;
                    float nz = z / radius;

                    // Lighting (Diffuse)
                    float dot = nx * lx + ny * ly + nz * lz;
                    if (dot < 0) dot = 0;

                    // Texture coordinates

                    // 1. Rotate for Tilt (Around X axis)
                    // y' = y*cos(t) - z*sin(t)
                    // z' = y*sin(t) + z*cos(t)
                    float ny_t = ny * cos_t - nz * sin_t;
                    float nz_t = ny * sin_t + nz * cos_t;
                    float nx_t = nx;

                    // 2. Rotate for Spin (Around Y axis)
                    // x'' = x'*cos(r) - z'*sin(r)
                    // z'' = x'*sin(r) + z'*cos(r)
                    float px = nx_t * cos_r - nz_t * sin_r;
                    float py = ny_t;
                    float pz = nx_t * sin_r + nz_t * cos_r;

                    // Spherical Mapping
                    // u = 0.5 + atan2(pz, px) / (2PI)
                    // v = 0.5 - asin(py) / PI
                    
                    float u = 0.5f + atan2f(pz, px) / (2.0f * M_PI);
                    float v = 0.5f - asinf(py) / M_PI; 
                    
                    // Flip right/left coordinates
                    u = 1.0f - u;

                    // Clamp/Wrap (atan2 handles wrap, asin handles clamp implicitly)
                    if (u < 0) u += 1.0f;
                    if (u > 1) u -= 1.0f;
                    if (v < 0) v = 0.0f;
                    if (v > 1) v = 1.0f;

                    // Map to texture pixels
                    int tx = (int)(u * (map_w - 1));
                    int ty = (int)(v * (map_h - 1));

                    int idx = (ty * map_w + tx) * 3;
                    
                    // Grayscale conversion
                    float val = (0.299f * map_data[idx] + 0.587f * map_data[idx+1] + 0.114f * map_data[idx+2]) / 255.0f;

                    // Apply lighting
                    val *= (0.2f + 0.8f * dot);

                    data[y * width + x] = val;
                }
            }
        }

        image.md->cnt0++;
        ImageStreamIO_sempost(&image, -1);

        rotation += speed;
        tilt_phase += tilt_speed;
        usleep(1000000 / fps);
    }
    
    stbi_image_free(map_data);
    return 0;
}