// udaf_c.h - C 接口 SDK（通用）
//
// 设计要点：
//   - 13 个函数 + 4 个结构体
//   - ABI 稳定（v1）
//   - 句柄不透明（void*）
//   - 不抛异常（返回 ErrorCode）

#ifndef UDAF_C_H
#define UDAF_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- 错误码（与 udaf::core::ErrorCode 镜像） ----------
typedef enum {
    UDAF_OK                  = 0,
    UDAF_ERR_INVALID_ARG     = 1,
    UDAF_ERR_INTERNAL        = 2,
    UDAF_ERR_NOT_FOUND       = 3,
    UDAF_ERR_AUTH_UNTRUSTED  = 4,
    UDAF_ERR_RESOURCE_BUSY   = 5,
    UDAF_ERR_NET_TIMEOUT     = 6,
    UDAF_ERR_WHITELIST       = 7,
    UDAF_ERR_CONFIG          = 8,
    UDAF_ERR_NOT_IMPLEMENTED = 9,
} udaf_error_t;

// ---------- 结构体 ----------
typedef struct {
    const char* node_id;
    const char* bind_address;
    uint16_t    bind_port;
    const char* audit_path;
} udaf_client_config_t;

typedef struct {
    const char* node_id;
    const char* hostname;
    const char* bind_address;
    uint16_t    bind_port;
    uint32_t    service_count;
    const char* const* service_names;
} udaf_node_entry_t;

typedef struct {
    const char* node_id;
    const char* fingerprint_hex;     // 64-hex SHA-256
    uint32_t    capability_count;
    const char* const* capabilities;
} udaf_trust_entry_t;

typedef struct {
    uint64_t    sequence;
    const char* json_payload;        // 一次性，调用方须拷贝
} udaf_audit_result_t;

// ---------- 生命周期 ----------
udaf_error_t udaf_client_create(const udaf_client_config_t* cfg,
                                 void** out_client);

udaf_error_t udaf_client_start(void* client);

udaf_error_t udaf_client_stop(void* client);

void         udaf_client_destroy(void* client);

// ---------- 发现 / 拓扑 ----------
udaf_error_t udaf_client_discover(void* client,
                                   const char* capability_filter,
                                   udaf_node_entry_t** out_entries,
                                   uint32_t* out_count);

void         udaf_client_free_entries(udaf_node_entry_t* entries,
                                       uint32_t count);

/// 注册一个本地节点到发现视图（不触发审计；仅 registry 视图填充）
udaf_error_t udaf_client_register_node(void* client,
                                        const char* node_id,
                                        const char* hostname,
                                        const char* bind_address,
                                        uint16_t    bind_port);

udaf_error_t udaf_client_unregister_node(void* client, const char* node_id);

uint32_t     udaf_client_topology_node_count(void* client);

// ---------- 白名单 ----------
udaf_error_t udaf_client_trust_add(void* client,
                                    const char* node_id,
                                    const char* fingerprint_hex,
                                    const char* const* capabilities,
                                    uint32_t capability_count);

udaf_error_t udaf_client_trust_remove(void* client, const char* node_id);

udaf_error_t udaf_client_trust_list(void* client,
                                      udaf_trust_entry_t** out_entries,
                                      uint32_t* out_count);

void         udaf_client_free_trust_entries(udaf_trust_entry_t* entries,
                                              uint32_t count);

// ---------- 调度 / 文件 / 命令 ----------
udaf_error_t udaf_client_run_remote(void* client,
                                      const char* node_id,
                                      const char* command,
                                      const char* const* args,
                                      uint32_t arg_count,
                                      udaf_audit_result_t* out_result);

udaf_error_t udaf_client_push_file(void* client,
                                    const char* src_path,
                                    const char* dst_node,
                                    const char* dst_path,
                                    udaf_audit_result_t* out_result);

udaf_error_t udaf_client_pull_file(void* client,
                                    const char* src_node,
                                    const char* src_path,
                                    const char* dst_path,
                                    udaf_audit_result_t* out_result);

// ---------- 版本 ----------
const char*  udaf_version_string(void);

uint32_t     udaf_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif  // UDAF_C_H