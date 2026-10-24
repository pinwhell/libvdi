#include "libvdi.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== libvdi Example: C Pure RAM Disk ===\n");

    // Create 32 MiB dynamic in-memory disk (io = NULL)
    vdi_image_t *img = NULL;
    vdi_status_t rc = vdi_create(&img, NULL, 32ULL * 1024 * 1024, VDI_TYPE_DYNAMIC, 0);
    if (rc != VDI_OK) {
        fprintf(stderr, "Failed to create RAM disk: %s\n", vdi_strerror(rc));
        return 1;
    }

    const char *data = "Hello from pure RAM disk!";
    size_t written = 0;
    vdi_write(img, 0, data, strlen(data), &written);
    printf("Wrote %zu bytes to cluster 0 in memory.\n", written);

    char buf[64] = {0};
    size_t bytes_read = 0;
    vdi_read(img, 0, buf, strlen(data), &bytes_read);
    printf("Read back: \"%s\"\n", buf);

    vdi_close(img);
    printf("RAM disk cleanly closed.\n");
    return 0;
}
