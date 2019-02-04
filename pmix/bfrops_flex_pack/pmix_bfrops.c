#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pmix_config.h"
#include "pmix_globals.h"

#include "src/mca/mca.h"
#include "src/mca/base/base.h"
#include "src/mca/base/pmix_mca_base_var.h"
#include "src/mca/base/pmix_mca_base_framework.h"
#include "src/mca/bfrops/base/base.h"
#include "src/mca/pinstalldirs/base/base.h"
#include "src/runtime/pmix_rte.h"

#include <limits.h>

typedef int (*integer_pack_test_fn)(pmix_bfrops_module_t *bfrop);

bool test_verbose = 0;

#define TEST_SIGN_FMT_unsigned lu
#define TEST_SIGN_FMT_signed ld

#define TEST_PACK_FN_TEMPLATE(_type, _pmix_type, _print_fmt, _min, _max) \
int integer_pack_test_ ##_type##_fn(pmix_bfrops_module_t *bfrop)        \
{                                                                       \
    _type i;                                                            \
    _type min = _min;                                                   \
    _type max = _max;                                                   \
    int ret = PMIX_SUCCESS;                                             \
    printf("%s: min "_print_fmt", max = "_print_fmt"\n", __func__, min, max);             \
    for (i = min; i < max; i++) {                                       \
        pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);                \
        _type src = i;                                                  \
        _type dst;                                                      \
        int cnt = 1;                                                    \
        if (test_verbose) {                                             \
            fprintf(stderr, "packing "_print_fmt, src);                 \
        }                                                               \
        ret = bfrop->pack(buffer, &src, 1, _pmix_type);                 \
        if (ret != PMIX_SUCCESS) {                                      \
            fprintf(stderr, "pack ERR %d\n", ret);                      \
            return ret;                                                 \
        }                                                               \
        ret = bfrop->unpack(buffer, &dst, &cnt, _pmix_type);            \
        if (ret != PMIX_SUCCESS) {                                      \
            fprintf(stderr, "unpack ERR %d\n", ret);                    \
            return ret;                                                 \
        }                                                               \
        if (src != dst) {                                               \
            fprintf(stderr, "ERR values are not match, "_print_fmt"\n", dst);    \
            return PMIX_ERROR;                                          \
        }                                                               \
        if (test_verbose) {                                             \
            fprintf(stderr, "OK\n");                                    \
        }                                                               \
        PMIX_RELEASE(buffer);                                           \
    }                                                                   \
    printf("%s: test OK\n", __func__);                                  \
    return PMIX_SUCCESS;                                                \
}

int init(pmix_bfrops_module_t **bfrop)
{
    int ret;

    /* initialize the output system */
    if (!pmix_output_init()) {
        return PMIX_ERROR;
    }

    /* initialize install dirs code */
    if (PMIX_SUCCESS != (ret = pmix_mca_base_framework_open(&pmix_pinstalldirs_base_framework, 0))) {
        fprintf(stderr, "pmix_pinstalldirs_base_open() failed -- process will likely abort (%s:%d, returned %d instead of PMIX_SUCCESS)\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    /* Setup the parameter system */
    if (PMIX_SUCCESS != (ret = pmix_mca_base_var_init())) {
        return ret;
    }

    /* register params for pmix */
    if (PMIX_SUCCESS != (ret = pmix_register_params())) {
        return ret;
    }

    /* initialize the mca */
    if (PMIX_SUCCESS != (ret = pmix_mca_base_open())) {
        return ret;
    }

    /* open the bfrops and select the active plugins */
    if (PMIX_SUCCESS != (ret = pmix_mca_base_framework_open(&pmix_bfrops_base_framework, 0)) ) {
        return ret;
    }
    if (PMIX_SUCCESS != (ret = pmix_bfrop_base_select()) ) {
        return ret;
    }

    *bfrop = pmix_bfrops_base_assign_module(NULL);

    if (NULL == *bfrop) {
        fprintf(stderr, "%s:%d, pmix_bfrops_base_assign_module() cannot assign module, %d\n",
                __FILE__, __LINE__, ret);
        return -1;
    }
    return ret;
}

#if 0
int integer_pack_test_int64(pmix_bfrops_module_t *bfrop)
{
    uint64_t i;
    uint64_t min = 0;
    uint64_t max = 0xFFFFFFFFFFFFFFFF;
    int ret = PMIX_SUCCESS;

    for (i = min; i <= max; i++) {
        pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
        uint64_t src = i;
        uint64_t dst;
        int cnt = 1;
        fprintf(stderr, "packing %ld ", src);
        ret = bfrop->pack(buffer, &src, 1, PMIX_UINT64);
        if (ret != PMIX_SUCCESS) {
            fprintf(stderr, "pack ERR %d\n", ret);
            return ret;
        }
        ret = bfrop->unpack(buffer, &dst, &cnt, PMIX_UINT64);
        if (ret != PMIX_SUCCESS) {
            fprintf(stderr, "unpack ERR %d\n", ret);
            return ret;
        }
        if (src != dst) {
            fprintf(stderr, "ERR values are not match\n");
            return PMIX_ERROR;
        }
        fprintf(stderr, "OK\n");
        PMIX_RELEASE(buffer);
    }
    return PMIX_SUCCESS;
}
#endif


TEST_PACK_FN_TEMPLATE(int64_t, PMIX_INT64, "%ld", LONG_MIN, LONG_MAX)
TEST_PACK_FN_TEMPLATE(uint64_t, PMIX_UINT64, "%lu", 0, ULONG_MAX)
TEST_PACK_FN_TEMPLATE(int32_t, PMIX_INT32, "%d", INT_MIN, INT_MAX)
TEST_PACK_FN_TEMPLATE(uint32_t, PMIX_UINT32, "%u", 0, UINT_MAX)
TEST_PACK_FN_TEMPLATE(int16_t, PMIX_INT16, "%d", SHRT_MIN, SHRT_MAX)
TEST_PACK_FN_TEMPLATE(uint16_t, PMIX_UINT16, "%u", 0, USHRT_MAX)

int main(int argc, char **argv)
{
    pmix_bfrops_module_t *bfrop;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    int64_t src = LONG_MIN;
    int64_t dst;
    int32_t cnt;
    int ret;
    int i;
    integer_pack_test_fn test_fn_array[] = { integer_pack_test_int64_t_fn,
                                             integer_pack_test_uint64_t_fn,
                                             integer_pack_test_int32_t_fn,
                                             integer_pack_test_uint32_t_fn,
                                             integer_pack_test_int16_t_fn,
                                             integer_pack_test_uint16_t_fn,
                                             NULL };

    if (PMIX_SUCCESS != (ret = init(&bfrop))) {
        fprintf(stderr, "init error %d\n", ret);
        return ret;
    }

    fprintf(stderr, "Bfrops version: %s\n", bfrop->version);


    printf("%ld\n", src);
    ret = bfrop->pack(buffer, &src, 1, PMIX_INT64);
    fprintf(stderr, "pack ret %d\n", ret);

    fprintf(stderr, "allocated %lu, used %lu\n", buffer->bytes_allocated, buffer->bytes_used);

    cnt = 1;
    ret = bfrop->unpack(buffer, &dst, &cnt, PMIX_INT64);
    fprintf(stderr, "unpack ret %d, value %ld\n", ret, (int64_t)dst);

    for (i = 0; test_fn_array[i] != NULL; i++) {
        ret = test_fn_array[i](bfrop);
        if (ret != PMIX_SUCCESS) {
            fprintf(stderr, "Test ERROR\n");
            return ret;
        }
    }

    return 0;
}
