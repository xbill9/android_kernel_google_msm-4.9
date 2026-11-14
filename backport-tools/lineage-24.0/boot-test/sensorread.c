/*
 * sensorread: read one sensor from the adb shell, so a test does not depend on
 * an app having used it. Opens the default sensor of the given type, samples it
 * every 100 ms and prints the first event's values, or exits 1 on timeout.
 * Usage: sensorread <type> [timeout_s]   e.g. sensorread 6 (pressure, hPa)
 * Build: build-test.sh sensorread.
 */
#include <android/looper.h>
#include <android/sensor.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <type> [timeout_s]\n", argv[0]); return 2; }
  int type = atoi(argv[1]);
  int timeout_ms = (argc > 2 ? atoi(argv[2]) : 10) * 1000;
  ASensorManager *sm = ASensorManager_getInstanceForPackage("com.android.shell");
  const ASensor *s = sm ? ASensorManager_getDefaultSensor(sm, type) : NULL;
  if (!s) { printf("no sensor of type %d\n", type); return 1; }
  ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  ASensorEventQueue *q = ASensorManager_createEventQueue(sm, looper, 1, NULL, NULL);
  if (!q || ASensorEventQueue_enableSensor(q, s) < 0) { printf("%s: cannot enable\n", ASensor_getName(s)); return 1; }
  ASensorEventQueue_setEventRate(q, s, 100000);
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    ALooper_pollOnce(200, NULL, NULL, NULL);
    ASensorEvent e;
    if (ASensorEventQueue_getEvents(q, &e, 1) > 0) {
      clock_gettime(CLOCK_MONOTONIC, &t);
      long ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
      printf("%s: %.2f %.2f %.2f after %ld ms\n", ASensor_getName(s), e.data[0], e.data[1], e.data[2], ms);
      ASensorEventQueue_disableSensor(q, s);
      return 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &t);
    if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 > timeout_ms) break;
  }
  printf("%s: no event in %d s\n", ASensor_getName(s), timeout_ms / 1000);
  ASensorEventQueue_disableSensor(q, s);
  return 1;
}
