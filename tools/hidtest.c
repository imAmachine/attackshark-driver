#include <hidapi/hidapi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    hid_device *d = hid_open_path("/dev/hidraw6");
    if (!d) { printf("open failed\n"); return 1; }
    unsigned char out[65], in[65];
    int combos[4][2] = {{65,65},{64,64},{65,64},{64,65}};
    for (int c = 0; c < 4; c++) {
        int sl = combos[c][0], gl = combos[c][1];
        memset(out, 0, sizeof out);
        if (sl == 65) { out[0] = 0; out[1] = 0x8f; out[8] = 0xff - 0x8f; }
        else { out[0] = 0x8f; out[7] = 0xff - 0x8f; }
        int r = hid_send_feature_report(d, out, sl);
        printf("combo %d set(len=%d)=%d %s\n", c, sl, r, r < 0 ? "err" : "ok");
        usleep(150000);
        memset(in, 0, sizeof in);
        in[0] = 0;
        r = hid_get_feature_report(d, in, gl);
        printf("  get(len=%d)=%d %s", gl, r, r < 0 ? "err" : "ok");
        if (r > 0) { printf(" data="); for (int i = 0; i < 16 && i < r; i++) printf("%02x", in[i]); }
        printf("\n");
    }
    hid_close(d);
    return 0;
}
