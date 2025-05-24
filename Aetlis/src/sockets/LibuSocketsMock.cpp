#include "../../include/libusockets.h"
#include <cstdio>

// This file provides stub implementations for libusockets functions that might be called

extern "C" {
    // Just enough stubs to satisfy the linker for basic functionality
    
    struct us_loop_t *us_create_loop(void *hint, void (*wakeup_cb)(struct us_loop_t *loop), void (*pre_cb)(struct us_loop_t *loop), void (*post_cb)(struct us_loop_t *loop), unsigned int ext_size) {
        printf("us_create_loop called - using stub\n");
        return nullptr;
    }
    
    void us_loop_free(struct us_loop_t *loop) {
        printf("us_loop_free called - using stub\n");
    }
    
    struct us_socket_context_t *us_create_socket_context(struct us_loop_t *loop, int socket_context_ext_size) {
        printf("us_create_socket_context called - using stub\n");
        return nullptr;
    }
    
    struct us_listen_socket_t *us_socket_context_listen(struct us_socket_context_t *context, const char *host, int port, int options, int socket_ext_size) {
        printf("us_socket_context_listen called - using stub\n");
        return nullptr;
    }
    
    // Add more stubs as needed

} 