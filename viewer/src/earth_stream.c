#include <ImageStreamIO/ImageStreamIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define WIDTH 256
#define HEIGHT 256
#define RADIUS 100.0
#define CX 127.5
#define CY 127.5
#define FPS 100

// Simple hash for noise
unsigned int hash(unsigned int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

float noise(float x, float y, float z) {
    int xi = (int)floor(x);
    int yi = (int)floor(y);
    int zi = (int)floor(z);

    float xf = x - xi;
    float yf = y - yi;
    float zf = z - zi;

    // Smoothstep
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float w = zf * zf * (3.0f - 2.0f * zf);

    // Hash coords
    #define H(a,b,c) (hash((unsigned int)(a) + hash((unsigned int)(b) + hash((unsigned int)(c)))))

    unsigned int aaa = H(xi,   yi,   zi);
    unsigned int baa = H(xi+1, yi,   zi);
    unsigned int aba = H(xi,   yi+1, zi);
    unsigned int bba = H(xi+1, yi+1, zi);
    unsigned int aab = H(xi,   yi,   zi+1);
    unsigned int bab = H(xi+1, yi,   zi+1);
    unsigned int abb = H(xi,   yi+1, zi+1);
    unsigned int bbb = H(xi+1, yi+1, zi+1);

    // Mix
    float x1 = (1.0f - u) * (aaa & 255) + u * (baa & 255);
    float x2 = (1.0f - u) * (aba & 255) + u * (bba & 255);
    float y1 = (1.0f - v) * x1 + v * x2;

    float x3 = (1.0f - u) * (aab & 255) + u * (bab & 255);
    float x4 = (1.0f - u) * (abb & 255) + u * (bbb & 255);
    float y2 = (1.0f - v) * x3 + v * x4;

    return ((1.0f - w) * y1 + w * y2) / 255.0f;
}

float fbm(float x, float y, float z) {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 0.5f;
    float maxVal = 0.0f;

    for (int i = 0; i < 4; i++) {
        total += noise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / maxVal;
}

int main(int argc, char **argv) {
    const char *stream_name = "earth";
    if (argc > 1) stream_name = argv[1];

    IMAGE image;
    uint32_t dims[2] = {WIDTH, HEIGHT};

    // Create shared memory image
    // 2D, Float, Shared=1, NBkw=0, CBsize=0
    if (ImageStreamIO_createIm(&image, stream_name, 2, dims, _DATATYPE_FLOAT, 1, 0, 0) != 0) {
        fprintf(stderr, "Error creating stream %s\n", stream_name);
        return 1;
    }

    printf("Stream %s created. Press Ctrl+C to stop.\n", stream_name);

    float rotation = 0.0f;
    float speed = 0.02f; // Rad per frame

    // Period = 50 rotations
    // 1 rotation = 2*PI
    // Frames per rotation = 2*PI / speed
    // Period in frames = 50 * (2*PI / speed)
    float tilt_speed = (2.0f * M_PI) / (50.0f * (2.0f * M_PI / speed));
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

        // Tilt between 0 and 90 degrees (0 to PI/2 radians)
        // Sine wave: 45 + 45 * sin(phase) -> 0..90 deg
        float tilt_deg = 45.0f + 45.0f * sin(tilt_phase);
        float tilt_rad = tilt_deg * (M_PI / 180.0f);

        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                float dx = x - CX;
                float dy = y - CY;
                float r2 = dx*dx + dy*dy;

                if (r2 > RADIUS*RADIUS) {
                    data[y * WIDTH + x] = 0.0f;
                } else {
                    float z = sqrt(RADIUS*RADIUS - r2);

                    // Normalize sphere normal
                    float nx = dx / RADIUS;
                    float ny = dy / RADIUS;
                    float nz = z / RADIUS;

                    // Lighting (Diffuse)
                    float dot = nx * lx + ny * ly + nz * lz;
                    if (dot < 0) dot = 0;

                    // Texture coordinates

                    // 1. Rotate for Tilt (Around X axis)
                    // y' = y*cos(t) - z*sin(t)
                    // z' = y*sin(t) + z*cos(t)
                    float ny_t = ny * cos(tilt_rad) - nz * sin(tilt_rad);
                    float nz_t = ny * sin(tilt_rad) + nz * cos(tilt_rad);
                    float nx_t = nx;

                    // 2. Rotate for Spin (Around Y axis)
                    float px = nx_t * cos(rotation) - nz_t * sin(rotation);
                    float py = ny_t;
                    float pz = nx_t * sin(rotation) + nz_t * cos(rotation);

                    // Sample noise
                    float freq = 3.0f;
                    float val = fbm(px * freq + 10.0f, py * freq + 10.0f, pz * freq + 10.0f);

                    // Continent threshold
                    if (val < 0.5f) val = 0.1f; // Ocean
                    else val = val * 1.5f; // Land

                    // Apply lighting
                    val *= (0.2f + 0.8f * dot);

                    data[y * WIDTH + x] = val;
                }
            }
        }

        image.md->cnt0++;
        ImageStreamIO_sempost(&image, -1);

        rotation += speed;
        tilt_phase += tilt_speed;
        usleep(1000000 / FPS);
    }

    return 0;
}
