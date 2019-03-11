#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pmix.h>

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

#define TEST_PACK_INT_FN_TEMPLATE(_type, _pmix_type, _print_fmt, _min, _max) \
int integer_pack_int_test_ ##_type##_fn(pmix_bfrops_module_t *bfrop)    \
{                                                                       \
    _type i;                                                            \
    _type min = _min;                                                   \
    _type max = _max;                                                   \
    int ret = PMIX_SUCCESS;                                             \
    if (test_verbose) {                                                 \
        printf("%s: min "_print_fmt", max = "_print_fmt"\n",            \
               __func__, min, max);                                     \
    }                                                                   \
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
            fprintf(stderr, "ERR values are not match, "_print_fmt"\n", \
                    dst);                                               \
            return PMIX_ERROR;                                          \
        }                                                               \
        if (test_verbose) {                                             \
            fprintf(stderr, "OK\n");                                    \
        }                                                               \
        PMIX_RELEASE(buffer);                                           \
    }                                                                   \
    if (test_verbose) {                                                 \
        printf("%s: test OK\n", __func__);                              \
    }                                                                   \
    return PMIX_SUCCESS;                                                \
}

typedef struct {
    char *name;
    char *string;
    pmix_data_type_t type;
    char **description;
} pmix_regattr_input_t;

static pmix_regattr_input_t server_attributes[] = {
    // init
        {.name = "PMIX_GDS_MODULE", .string = PMIX_GDS_MODULE, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", NULL}},
        {.name = "PMIX_EVENT_BASE", .string = PMIX_EVENT_BASE, .type = PMIX_POINTER, .description = (char *[]){"VALID MEMORY REFERENCE", NULL}},
        {.name = "PMIX_HOSTNAME", .string = PMIX_HOSTNAME, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", NULL}},
        {.name = "PMIX_NODEID", .string = PMIX_NODEID, .type = PMIX_UINT32, .description = (char *[]){"POSITIVE INTEGERS", NULL}},
        {.name = "PMIX_SINGLE_LISTENER", .string = PMIX_SINGLE_LISTENER, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Only use one messaging interface", NULL}},
        {.name = "PMIX_USOCK_DISABLE", .string = PMIX_USOCK_DISABLE, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Disable usock messaging interface", NULL}},
        {.name = "PMIX_SOCKET_MODE", .string = PMIX_SOCKET_MODE, .type = PMIX_UINT32, .description = (char *[]){"Valid POSIX mode_t value", NULL}},
        {.name = "PMIX_TCP_REPORT_URI", .string = PMIX_TCP_REPORT_URI, .type = PMIX_STRING, .description = (char *[]){"-, +, or filename", NULL}},
        {.name = "PMIX_TCP_IF_INCLUDE", .string = PMIX_TCP_IF_INCLUDE, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", "Comma-separated list of", "TCP interfaces to include", NULL}},
        {.name = "PMIX_TCP_IF_EXCLUDE", .string = PMIX_TCP_IF_EXCLUDE, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", "Comma-separated list of", "TCP interfaces to exclude", NULL}},
        {.name = "PMIX_TCP_IPV4_PORT", .string = PMIX_TCP_IPV4_PORT, .type = PMIX_INT, .description = (char *[]){"POSITIVE INTEGERS", "IPv4 port to be used", NULL}},
        {.name = "PMIX_TCP_IPV6_PORT", .string = PMIX_TCP_IPV6_PORT, .type = PMIX_INT, .description = (char *[]){"POSITIVE INTEGERS", "IPv6 port to be used", NULL}},
        {.name = "PMIX_TCP_DISABLE_IPV4", .string = PMIX_TCP_DISABLE_IPV4, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Disable IPv4 messaging interface", NULL}},
        {.name = "PMIX_TCP_DISABLE_IPV6", .string = PMIX_TCP_DISABLE_IPV6, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Disable IPv6 messaging interface", NULL}},
        {.name = "PMIX_SERVER_REMOTE_CONNECTIONS", .string = PMIX_SERVER_REMOTE_CONNECTIONS, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Allow connections from", "remote tools", NULL}},
        {.name = "PMIX_SERVER_NSPACE", .string = PMIX_SERVER_NSPACE, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", "Namespace assigned to server", NULL}},
        {.name = "PMIX_SERVER_RANK", .string = PMIX_SERVER_RANK, .type = PMIX_PROC_RANK, .description = (char *[]){"POSITIVE INTEGERS", "Rank assigned to server", NULL}},
        {.name = "PMIX_SERVER_TMPDIR", .string = PMIX_SERVER_TMPDIR, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", "Path to temp directory", "assigned to server", NULL}},
        {.name = "PMIX_SYSTEM_TMPDIR", .string = PMIX_SYSTEM_TMPDIR, .type = PMIX_STRING, .description = (char *[]){"UNRESTRICTED", "Path to system temp directory", NULL}},
        {.name = "PMIX_SERVER_TOOL_SUPPORT", .string = PMIX_SERVER_TOOL_SUPPORT, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Allow tool connections", NULL}},
        {.name = "PMIX_SERVER_SYSTEM_SUPPORT", .string = PMIX_SERVER_SYSTEM_SUPPORT, .type = PMIX_BOOL, .description = (char *[]){"True,False", "Declare server as being the", "local system server for PMIx", "connection requests", NULL}},
};

int init(pmix_bfrops_module_t **bfrop)
{
    int ret;

    /* initialize the output system */
    if (!pmix_output_init()) {
        return PMIX_ERROR;
    }

    /* initialize install dirs code */
    if (PMIX_SUCCESS != (ret = pmix_mca_base_framework_open(&pmix_pinstalldirs_base_framework, 0))) {
        fprintf(stderr, "pmix_pinstalldirs_base_open() failed -- process will likely abort (%s:%d: returned %d instead of PMIX_SUCCESS)\n",
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
        fprintf(stderr, "%s:%d: pmix_bfrops_base_assign_module() cannot assign module, %d\n",
                __FILE__, __LINE__, ret);
        return -1;
    }
    return ret;
}

#if 0
int integer_pack_int_test_int64(pmix_bfrops_module_t *bfrop)
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

TEST_PACK_INT_FN_TEMPLATE(int64_t, PMIX_INT64, "%ld", -10000, 10000)
TEST_PACK_INT_FN_TEMPLATE(uint64_t, PMIX_UINT64, "%lu", 0, 30000)
TEST_PACK_INT_FN_TEMPLATE(int32_t, PMIX_INT32, "%d", -10000, 40000)
TEST_PACK_INT_FN_TEMPLATE(uint32_t, PMIX_UINT32, "%u", 0, 50000)
TEST_PACK_INT_FN_TEMPLATE(int16_t, PMIX_INT16, "%d", SHRT_MIN, SHRT_MAX)
TEST_PACK_INT_FN_TEMPLATE(uint16_t, PMIX_UINT16, "%u", 0, USHRT_MAX)

static int test_int_pack(pmix_bfrops_module_t *bfrop)
{
    int i, ret;
    integer_pack_test_fn test_fn_array[] = { integer_pack_int_test_int64_t_fn,
                                             integer_pack_int_test_uint64_t_fn,
                                             integer_pack_int_test_int32_t_fn,
                                             integer_pack_int_test_uint32_t_fn,
                                             integer_pack_int_test_int16_t_fn,
                                             integer_pack_int_test_uint16_t_fn,
                                             NULL };

    for (i = 0; test_fn_array[i] != NULL; i++) {
        ret = test_fn_array[i](bfrop);
        if (ret != PMIX_SUCCESS) {
            fprintf(stderr, "Test ERROR\n");
            return ret;
        }
    }
    return 0;
}

static int test_pinfo_pack(pmix_bfrops_module_t *bfrop)
{
    int i, ret = 0;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    pmix_kval_t *kv = PMIX_NEW(pmix_kval_t), *kv2 = PMIX_NEW(pmix_kval_t);
    pmix_proc_info_t *pinfo;

    kv->key = strdup("proc_info");
    kv->value = (pmix_value_t*)malloc(sizeof(pmix_value_t));
    kv->value->type = PMIX_PROC_INFO;
    PMIX_PROC_INFO_CREATE(pinfo, 1);
    kv->value->data.pinfo = pinfo;

    pinfo->executable_name = strdup("executable_name");
    pinfo->exit_code = -1;
    pinfo->hostname = strdup("hostname");
    pinfo->pid = 444;
    PMIX_PROC_CONSTRUCT(&pinfo->proc);
    PMIX_PROC_LOAD(&pinfo->proc, "myproc.nspace", 2);
    pinfo->state = PMIX_ERR_NOT_IMPLEMENTED;

    ret = bfrop->pack(buffer, kv, 1, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_INFO ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    PMIX_RELEASE(kv);
    PMIX_RELEASE(kv2);
    PMIX_RELEASE(buffer);

    return ret;
}

static int test_proc_pack(pmix_bfrops_module_t *bfrop)
{
    int i, ret = 0;
    pmix_info_t *info, *info2;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    pmix_kval_t *kv = PMIX_NEW(pmix_kval_t), *kv2 = PMIX_NEW(pmix_kval_t);
    pmix_data_array_t *darray;

    PMIX_DATA_ARRAY_CREATE(darray, 10, PMIX_INFO);
    info = (pmix_info_t*)darray->array;

    for (i = 0; i < 10; i++) {
        sprintf(info[i].key, "key-%d", i);
        info[i].value.type = PMIX_PROC;
        PMIX_PROC_CREATE(info[i].value.data.proc, 1);
        info[i].value.data.proc->rank = i;
        sprintf(info[i].value.data.proc->nspace, "nspace-%d", i);
    }

    kv->key = strdup("darray");
    kv->value = (pmix_value_t*)malloc(sizeof(pmix_value_t));
    kv->value->type = PMIX_DATA_ARRAY;
    kv->value->data.darray = darray;

    ret = bfrop->pack(buffer, kv, 1, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_INFO ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }
    uint32_t cnt = 1;
    ret = bfrop->unpack(buffer, kv2, &cnt, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: unpack PMIX_INFO ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }


    info2 = (pmix_info_t*)kv2->value->data.darray->array;
    for (i = 0; i < 10; i++) {
        if (!PMIX_CHECK_KEY(&info[i], info2[i].key)) {
            fprintf(stderr, "%s:%d: unpack %s name mismatch: %s\n",
                    __FILE__, __LINE__, info[i].key, info2[i].key);
            return -1;
        }
        if (!PMIX_CHECK_PROCID(info[i].value.data.proc,
                              info2[i].value.data.proc)) {
            fprintf(stderr, "%s:%d: unpack proc %d name mismatch: %s:%d, expected %s:%d\n",
                    __FILE__, __LINE__, i, info2[i].value.data.proc->nspace,
                    info2[i].value.data.proc->rank,
                    info[i].value.data.proc->nspace,
                    info[i].value.data.proc->rank);
            return -1;
        }
    }

    PMIX_RELEASE(kv);
    PMIX_RELEASE(kv2);
    PMIX_RELEASE(buffer);

    return ret;
}

static int test_darray_pack(pmix_bfrops_module_t *bfrop)
{
    int i, ret;
    pmix_kval_t *kv = PMIX_NEW(pmix_kval_t), *kv2 = NULL;
    pmix_data_type_t start_type = PMIX_SIZE;
    pmix_data_array_t *darray;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    size_t size = 12, size2;

    PMIX_DATA_ARRAY_CREATE(darray, (int32_t)size, PMIX_INFO);
    ret = bfrop->pack(buffer, &size, 1, PMIX_SIZE);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_SIZE ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }
    for (i = 0; i < 12; i ++) {
        pmix_info_t *info = (pmix_info_t*)darray->array;
        sprintf(info[i].key, "key-%d", i);
        info[i].value.type = start_type + i;
        info[i].value.data.int8 = (int8_t)i;
    }

    kv->key = strdup("darray");
    kv->value = (pmix_value_t*)malloc(sizeof(pmix_value_t));
    kv->value->type = PMIX_DATA_ARRAY;
    kv->value->data.darray = darray;

    ret = bfrop->pack(buffer, kv, 1, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_KVAL ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    /* unpacking */
    int32_t cnt = 1;
    ret = bfrop->unpack(buffer, &size2, &cnt, PMIX_SIZE);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: unpack PMIX_SIZE ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    if (size != size2) {
        fprintf(stderr, "%s:%d: unpacked size mismatch: %lu, expected %lu",
                __FILE__, __LINE__, size2, size);
        return -1;
    }
    cnt = 1;
    kv2 = PMIX_NEW(pmix_kval_t);
    ret = bfrop->unpack(buffer, kv2, &cnt, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: unpack PMIX_KVAL ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }
    if ((kv2->value->type != PMIX_DATA_ARRAY) ||
            (0 != strcmp(kv2->key, "darray"))) {
        fprintf(stderr, "%s:%d: unpack type PMIX_DATA_ARRAY ERR\n",
                __FILE__, __LINE__);
        return -1;
    }
    for (i = 0; i < size2; i++) {
        pmix_info_t *info2 = (pmix_info_t*)kv2->value->data.darray->array;
        pmix_info_t *info = (pmix_info_t*)kv->value->data.darray->array;
        if (0 != strcmp(info[i].key, info[i].key)) {
            fprintf(stderr, "%s:%d: unpack key-%d name mismatch\n",
                    __FILE__, __LINE__, i);
            return -1;
        }
        if (info[i].value.data.int8 != (int8_t)i) {
            fprintf(stderr, "%s:%d: unpack key val mismatch\n",
                    __FILE__, __LINE__);
            return -1;
        }
    }
    PMIX_RELEASE(kv);
    PMIX_RELEASE(kv2);
    PMIX_RELEASE(buffer);

    return 0;
}

static int test_darray_regattr_pack(pmix_bfrops_module_t *bfrop)
{
    int i, ret;
    pmix_kval_t *kv = PMIX_NEW(pmix_kval_t), *kv2 = NULL;
    pmix_data_type_t start_type = PMIX_SIZE;
    pmix_data_array_t *darray;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    size_t size = sizeof(server_attributes )/sizeof(server_attributes[0]), size2, m;
    pmix_regattr_t *regarray;

    PMIX_DATA_ARRAY_CREATE(darray, (int32_t)size, PMIX_REGATTR);
    ret = bfrop->pack(buffer, &size, 1, PMIX_SIZE);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_SIZE ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    kv->key = strdup("darray");
    kv->value = (pmix_value_t*)malloc(sizeof(pmix_value_t));
    kv->value->type = PMIX_DATA_ARRAY;
    kv->value->data.darray = darray;
    regarray = (pmix_regattr_t*)darray->array;

    for (m=0; m < size; m++) {
        if (0 == strlen(server_attributes[m].name)) {
            continue;
        }
        regarray[m].name = strdup(server_attributes[m].name);
        PMIX_LOAD_KEY(regarray[m].string, server_attributes[m].string);
        regarray[m].type = server_attributes[m].type;
        PMIX_ARGV_COPY(regarray[m].description, server_attributes[m].description);
        regarray[m].ninfo = 0;
    }

    ret = bfrop->pack(buffer, kv, 1, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: pack PMIX_KVAL ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    /* unpacking */
    int32_t cnt = 1;
    ret = bfrop->unpack(buffer, &size2, &cnt, PMIX_SIZE);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: unpack PMIX_SIZE ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }

    if (size != size2) {
        fprintf(stderr, "%s:%d: unpacked size mismatch: %lu, expected %lu",
                __FILE__, __LINE__, size2, size);
        return -1;
    }
    cnt = 1;
    kv2 = PMIX_NEW(pmix_kval_t);
    ret = bfrop->unpack(buffer, kv2, &cnt, PMIX_KVAL);
    if (ret != PMIX_SUCCESS) {
        fprintf(stderr, "%s:%d: unpack PMIX_KVAL ERR %d\n",
                __FILE__, __LINE__, ret);
        return ret;
    }
    if ((kv2->value->type != PMIX_DATA_ARRAY) ||
            (0 != strcmp(kv2->key, "darray"))) {
        fprintf(stderr, "%s:%d: unpack type PMIX_DATA_ARRAY ERR\n",
                __FILE__, __LINE__);
        return -1;
    }

    regarray = (pmix_regattr_t*)kv2->value->data.darray->array;
    for (i = 0; i < size2; i++) {
        //fprintf(stderr, "unpack %s\n", regarray[i].name);
        if (server_attributes[i].type != regarray[i].type) {
            ret = PMIX_ERROR;
            break;
        }
        if (0 != strcmp(server_attributes[i].string, regarray[i].string)) {
            ret = PMIX_ERROR;
            break;
        }
        if (0 != strcmp(server_attributes[i].name, regarray[i].name)) {
            ret = PMIX_ERROR;
            break;
        }
    }
    PMIX_RELEASE(kv);
    PMIX_RELEASE(kv2);
    PMIX_RELEASE(buffer);

    return 0;
}

int main(int argc, char **argv)
{
    pmix_bfrops_module_t *bfrop;
    pmix_buffer_t *buffer = PMIX_NEW(pmix_buffer_t);
    int64_t src = LONG_MIN;
    int64_t dst;
    int32_t cnt;
    int ret;
    int64_t status = 33024L;//ULONG_MAX >> 1;// >> 6;
    int64_t status_unpack = 0;

    if (PMIX_SUCCESS != (ret = init(&bfrop))) {
        fprintf(stderr, "init error %d\n", ret);
        return ret;
    }

    fprintf(stderr, "Bfrops version: %s\n", bfrop->version);
/*
    ret = bfrop->pack(buffer, &status, 1, PMIX_UINT64);
    fprintf(stderr, "pack ret %d\n", ret);

    fprintf(stderr, "allocated %lu, used %lu\n", buffer->bytes_allocated, buffer->bytes_used);

    cnt = 1;
    ret = bfrop->unpack(buffer, &status_unpack, &cnt, PMIX_UINT64);
    fprintf(stderr, "unpack ret %d, value %lu\n", ret, status_unpack);
*/
    ret = test_darray_regattr_pack(bfrop);
    fprintf(stderr, "darray regattr test: %s\n", !ret ? "OK" : "Error");

    ret = test_int_pack(bfrop);
    fprintf(stderr, "integer 16/32/64 packing test: %s\n", !ret ? "OK" : "Error");

    ret = test_darray_pack(bfrop);
    fprintf(stderr, "darray key-val packing test: %s\n", !ret ? "OK" : "Error");

    ret = test_proc_pack(bfrop);
    fprintf(stderr, "proc packing test: %s\n", !ret ? "OK" : "Error");

    ret = test_pinfo_pack(bfrop);
    fprintf(stderr, "proc_info packing test: %s\n", !ret ? "OK" : "Error");

    pmix_query_t *q1, *q2;
    char *myallocation = "MYALLOCATION";

    PMIX_QUERY_CREATE(q1, 1);
    PMIX_ARGV_APPEND(ret, q1[0].keys, PMIX_QUERY_ALLOC_STATUS);
    PMIX_INFO_CREATE(q1[0].qualifiers, 1);
    PMIX_INFO_LOAD(&q1[0].qualifiers[0], PMIX_ALLOC_ID, myallocation, PMIX_STRING);

    ret = bfrop->pack(buffer, q1, 1, PMIX_QUERY);
    fprintf(stderr, "pack ret %d\n", ret);

    PMIX_QUERY_CREATE(q2, 1);
    cnt = 1;
    ret = bfrop->unpack(buffer, &q2, &cnt, PMIX_QUERY);
    fprintf(stderr, "unpack ret %d\n", ret);

    return 0;
}
