/* Purpose: verify the stopped M6 core checkpoint without boot integration.
 * Dependencies: pure memory-service and kernel-heap over guarded host RAM.
 * Invariants: one exclusive caller; independent snapshots check rejected writes.
 * Reference: ADR-persistent-memory-service; no physical memory or host changes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "memory-service.h"

struct fixture {
    uint8_t before[16];
    _Alignas(16) uint8_t arena[4096];
    uint8_t after[16];
};

/* Confirm sentinels independently of ASan; no state mutation or ownership. */
static void guards(const struct fixture *storage)
{
    for (size_t i = 0; i < 16; ++i) {
        assert(storage->before[i] == 0xa7);
        assert(storage->after[i] == 0xa7);
    }
}

/* Exercise identity, ordinary allocations and resets through public methods.
 * Storage is local, disjoint and stable for the complete service lifetime. */
static void test_lifetimes(void)
{
    struct fixture storage;
    memset(&storage, 0xa7, sizeof storage);
    struct memory_service service = {0};
    assert(memory_service_initialize(&service, storage.arena, sizeof storage.arena) == MEMORY_SERVICE_OK);
    const struct memory_service_session *session = NULL;
    assert(memory_service_get_session(&service, &session) == MEMORY_SERVICE_OK);
    assert(session->workspace_count == 9 && session->active_workspace_id == 1);
    assert(service.heap.live_allocations == 10);
    for (size_t id = 1; id <= 9; ++id) {
        const struct memory_service_workspace *workspace = NULL;
        assert(memory_service_get_workspace(&service, id, &workspace) == MEMORY_SERVICE_OK);
        assert(workspace->workspace_id == id && workspace->reset_generation == 1);
        assert(memory_service_release(&service, KERNEL_HEAP_OWNER_KERNEL, (void *)workspace) == MEMORY_SERVICE_ROOT_PROTECTED);
    }
    void *keep = NULL, *discard = NULL;
    assert(memory_service_allocate(&service, KERNEL_HEAP_OWNER_FIRST_WORKSPACE + 1, 32, &keep) == MEMORY_SERVICE_OK);
    assert(memory_service_allocate(&service, KERNEL_HEAP_OWNER_FIRST_WORKSPACE, 32, &discard) == MEMORY_SERVICE_OK);
    for (size_t i = 0; i < 32; ++i) assert(((uint8_t *)keep)[i] == 0);
    memset(keep, 0x6b, 32);
    memset(discard, 0x42, 32);
    struct memory_service saved = service;
    uint8_t bytes[4096];
    memcpy(bytes, storage.arena, sizeof bytes);
    void *output = (void *)(uintptr_t)0x1234;
    assert(memory_service_allocate(&service, KERNEL_HEAP_OWNER_KERNEL, 4096, &output) == MEMORY_SERVICE_OOM);
    assert(output == (void *)(uintptr_t)0x1234);
    assert(memcmp(&saved, &service, sizeof service) == 0);
    assert(memcmp(bytes, storage.arena, sizeof bytes) == 0);
    assert(memory_service_allocate(&service, KERNEL_HEAP_OWNER_KERNEL, 16, (void **)&service.session_root) == MEMORY_SERVICE_INVALID);
    assert(memory_service_reset_workspace(&service, 1) == MEMORY_SERVICE_OK);
    const struct memory_service_workspace *workspace = NULL;
    assert(memory_service_get_workspace(&service, 1, &workspace) == MEMORY_SERVICE_OK);
    assert(workspace->reset_generation == 2 && workspace->workspace_id == 1);
    assert(service.session_root == session);
    for (size_t i = 0; i < 32; ++i) assert(((uint8_t *)keep)[i] == 0x6b);
    assert(memory_service_release(&service, KERNEL_HEAP_OWNER_FIRST_WORKSPACE + 1, keep) == MEMORY_SERVICE_OK);
    for (size_t id = 1; id <= 9; ++id) assert(memory_service_reset_workspace(&service, id) == MEMORY_SERVICE_OK);
    assert(memory_service_check(&service) == MEMORY_SERVICE_OK);
    assert(service.heap.live_allocations == 10);
    guards(&storage);
}

/* Exhaust external records, then prove every root can be reconstructed. Also
 * reject overflow and a forged root before dereference, preserving snapshots. */
static void test_pressure_and_corruption(void)
{
    struct fixture storage;
    memset(&storage, 0xa7, sizeof storage);
    struct memory_service service = {0};
    assert(memory_service_initialize(&service, storage.arena, sizeof storage.arena) == MEMORY_SERVICE_OK);
    void *output = NULL;
    size_t allocated = 0;
    enum memory_service_result result;
    while ((result = memory_service_allocate(&service, KERNEL_HEAP_OWNER_KERNEL, 16, &output)) == MEMORY_SERVICE_OK) {
        assert(++allocated < 64);
    }
    assert(result == MEMORY_SERVICE_METADATA_EXHAUSTED && allocated > 0);
    for (size_t id = 1; id <= 9; ++id) assert(memory_service_reset_workspace(&service, id) == MEMORY_SERVICE_OK);
    service.workspace_roots[0]->reset_generation = SIZE_MAX;
    struct memory_service saved = service;
    uint8_t bytes[4096];
    memcpy(bytes, storage.arena, sizeof bytes);
    assert(memory_service_reset_workspace(&service, 1) == MEMORY_SERVICE_GENERATION_OVERFLOW);
    assert(memcmp(&saved, &service, sizeof service) == 0);
    assert(memcmp(bytes, storage.arena, sizeof bytes) == 0);
    service.workspace_roots[0] = (void *)(uintptr_t)1;
    assert(memory_service_check(&service) == MEMORY_SERVICE_CORRUPT);
    guards(&storage);
}

/* Run this core-only checkpoint's meaningful host checks. */
int main(void)
{
    test_lifetimes();
    test_pressure_and_corruption();
    puts("PASS memory service core: roots, zeroing, OOM, isolated reset, metadata pressure and corruption");
}
