#if defined(_WIN32)
#include "windows.h"
#else
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#endif

#include <dlfcn.h>

#include <inttypes.h>
#include <stdio.h>

#include <spngt_bench.h>

#include "test_png.h"
#include "test_spng.h"
#include "test_wuffs.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "lodepng.h"

struct spngt_times {
  uint64_t libpng;
  uint64_t spng;
  uint64_t stb;
  uint64_t lodepng;
  uint64_t wuffs;
  uint64_t external;
};

const char *external_name = NULL;
uint8_t *(*external_png_func)(const uint8_t *data, uint64_t size, uint32_t *w,
                              uint32_t *h);

static const int decode_runs = 5;
static const int encode_runs = 3;

static void print_times(struct spngt_times *times) {
  printf("libpng:    %" PRIu64 " usec\n", times->libpng / (uint64_t)1000);
  printf("spng:      %" PRIu64 " usec\n", times->spng / (uint64_t)1000);
  printf("stb_image: %" PRIu64 " usec\n", times->stb / (uint64_t)1000);
  printf("lodepng:   %" PRIu64 " usec\n", times->lodepng / (uint64_t)1000);
  printf("wuffs:     %" PRIu64 " usec\n", times->wuffs / (uint64_t)1000);
  if (external_name) {
    printf("%s:     %" PRIu64 " usec\n", external_name,
           times->external / (uint64_t)1000);
  }
}

static uint64_t spngt_time(void) {
#if defined(_WIN32)
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER time = {.LowPart = ft.dwLowDateTime,
                         .HighPart = ft.dwHighDateTime};
  return time.QuadPart * 100;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
#endif
}

static int decode_benchmark(void *pngbuf, size_t siz_pngbuf) {
  uint64_t a, b, elapsed = 0;

  struct spngt_times best = {.libpng = UINT64_MAX,
                             .spng = UINT64_MAX,
                             .stb = UINT64_MAX,
                             .lodepng = UINT64_MAX,
                             .wuffs = UINT64_MAX,
                             .external = UINT64_MAX};

  int i;
  for (i = 0; i < decode_runs; i++) {
    /* libpng */
    size_t img_png_size;

    a = spngt_time();
    unsigned char *img_png =
        getimage_libpng(pngbuf, siz_pngbuf, &img_png_size, SPNG_FMT_RGBA8, 0);
    b = spngt_time();

    elapsed = b - a;
    if (best.libpng > elapsed)
      best.libpng = elapsed;

    free(img_png);

    /* libspng */
    struct spng_ihdr ihdr;
    size_t img_spng_size;

    a = spngt_time();
    unsigned char *img_spng = getimage_libspng(
        pngbuf, siz_pngbuf, &img_spng_size, SPNG_FMT_RGBA8, 0, &ihdr);
    b = spngt_time();

    elapsed = b - a;
    if (best.spng > elapsed)
      best.spng = elapsed;

    free(img_spng);

    /* stb_image */
    int x, y, bpp;

    a = spngt_time();
    unsigned char *img_stb =
        stbi_load_from_memory(pngbuf, siz_pngbuf, &x, &y, &bpp, 4);
    b = spngt_time();

    elapsed = b - a;
    if (best.stb > elapsed)
      best.stb = elapsed;

    free(img_stb);

    /* lodepng */
    unsigned int width, height;

    a = spngt_time();
    int e = lodepng_decode32(&img_png, &width, &height, pngbuf, siz_pngbuf);
    b = spngt_time();

    if (e)
      printf("ERROR: lodepng decode failed\n");

    elapsed = b - a;
    if (best.lodepng > elapsed)
      best.lodepng = elapsed;

    free(img_png);

    /* wuffs */
    size_t img_wuffs_size = 0;

    a = spngt_time();
    unsigned char *img_wuffs =
        getimage_wuffs(pngbuf, siz_pngbuf, &img_wuffs_size);
    b = spngt_time();

    if (!img_wuffs)
      printf("ERROR: wuffs decode failed\n");

    elapsed = b - a;
    if (best.wuffs > elapsed)
      best.wuffs = elapsed;

    free(img_wuffs);

    /* external lib */
    if (external_name) {
      uint32_t w, h;

      a = spngt_time();
      uint8_t *img_external = external_png_func(pngbuf, siz_pngbuf, &w, &h);
      b = spngt_time();

      if (!img_external)
        printf("ERROR: external decode failed\n");

      elapsed = b - a;
      if (best.external > elapsed)
        best.external = elapsed;

      free(img_external);
    }
  }

  print_times(&best);

  return 0;
}

static int encode_benchmark(struct spngt_bench_params *params) { return 0; }

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("no input file\n");
    return 1;
  }

  const char *external_lib = getenv("EXTERNAL_LIB");
  void *lib = NULL;
  if (external_lib) {
    lib = dlopen(external_lib, RTLD_NOW);
    if (!lib) {
      fprintf(stderr, "%s\n", dlerror());
      return 1;
    }
    const char *(*name)() = dlsym(lib, "get_name");
    if (!name) {
      fprintf(stderr, "%s\n", dlerror());
      return 1;
    }
    external_name = name();
    external_png_func = dlsym(lib, "decode_png");
    if (!external_png_func) {
      fprintf(stderr, "%s\n", dlerror());
      return 1;
    }
  }

  int do_encode = 0;

  if (argc > 2) {
    if (!strcmp(argv[2], "enc")) {
      do_encode = 1;
    } else
      printf("unrecognized option: %s\n", argv[2]);
  }

  if (!strcmp(argv[1], "info")) {
    unsigned int png_ver = png_access_version_number();

    printf("png header version: %u.%u.%u, library version: %u.%u.%u\n",
           PNG_LIBPNG_VER_MAJOR, PNG_LIBPNG_VER_MINOR, PNG_LIBPNG_VER_RELEASE,
           png_ver / 10000, png_ver / 100 % 100, png_ver % 100);

    printf("spng header version: %u.%u.%u, library version: %s\n",
           SPNG_VERSION_MAJOR, SPNG_VERSION_MINOR, SPNG_VERSION_PATCH,
           spng_version_string());

    printf("stb_image 2.19 (2018-02-11)\n");

    printf("lodepng %s\n", LODEPNG_VERSION_STRING);

    printf("wuffs %s\n", WUFFS_VERSION_STRING);

    if (do_encode)
      printf("\nencode times are the best of %d runs\n", encode_runs);
    printf("\ndecode times are the best of %d runs\n", decode_runs);

    return 0;
  }

  FILE *png;
  unsigned char *pngbuf;
  png = fopen(argv[1], "rb");

  if (png == NULL) {
    printf("error opening input file %s\n", argv[1]);
    return 1;
  }

  fseek(png, 0, SEEK_END);
  long siz_pngbuf = ftell(png);
  rewind(png);

  if (siz_pngbuf < 1)
    return 1;

  pngbuf = malloc(siz_pngbuf);

  if (pngbuf == NULL) {
    printf("malloc() failed\n");
    return 1;
  }

  if (fread(pngbuf, siz_pngbuf, 1, png) != 1) {
    printf("fread() failed\n");
    return 1;
  }

  int ret = 0;

  if (do_encode) {
    struct spngt_bench_params params = {0};

    ret = encode_benchmark(&params);
  } else {
    ret = decode_benchmark(pngbuf, siz_pngbuf);
  }

  free(pngbuf);

  if (lib) {
    dlclose(lib);
  }

  return ret;
}
