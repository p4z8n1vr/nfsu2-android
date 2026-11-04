// Temporary Android native linking validation.
// This is not the final game entry point.

extern "C" __attribute__((visibility("default")))
int nfsu2_link_probe_version() {
    return 1;
}
