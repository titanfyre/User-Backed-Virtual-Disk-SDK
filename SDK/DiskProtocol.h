#pragma once
#ifdef _KERNEL_MODE
#include <ntddk.h>
typedef ULONG UD_U32; typedef ULONGLONG UD_U64; typedef UCHAR UD_U8;
#else
#include <cstdint>
typedef uint32_t UD_U32; typedef uint64_t UD_U64; typedef uint8_t UD_U8;
#endif
#define UD_MAGIC 0x31444255u
#define UD_VERSION 1u
#define UD_TRANSFER 65536u
#define UD_PENDING 32u
#define UD_MIN_CAPACITY (64ull*1024*1024)
#define UD_MAX_CAPACITY (1024ull*1024*1024)
#define UD_SECTOR 512u
#define UD_STATUS 0u
#define UD_CHALLENGE 1u
#define UD_AUTHORIZE 2u
#define UD_START 3u
#define UD_POLL 4u
#define UD_COMPLETE 5u
#define UD_STOP 6u
#define UD_READ 1u
#define UD_WRITE 2u
#define UD_FLUSH 3u
typedef struct UD_MESSAGE {
 UD_U32 magic,version,command,reserved;
 UD_U64 generation,id,offset;
 UD_U32 length,result;
 UD_U64 capacity;
 UD_U32 readonly,operation;
 UD_U8 data[UD_TRANSFER];
} UD_MESSAGE;
typedef struct UD_HEADER {
 UD_U32 magic,version,command,reserved;
 UD_U64 generation,id,offset;
 UD_U32 length,result;
 UD_U64 capacity;
 UD_U32 readonly,operation;
} UD_HEADER;
typedef struct UD_STATISTICS {
 UD_U64 reads,writes,flushes,completed,failed,timeouts,rejected,remaining_100ns,queue_overflow;
 UD_U32 pending,active,version,size;
} UD_STATISTICS;
static __inline int UdValidCapacity(UD_U64 capacity){return capacity>=UD_MIN_CAPACITY&&capacity<=UD_MAX_CAPACITY&&capacity%UD_SECTOR==0;}
static __inline int UdValidRange(UD_U64 capacity,UD_U64 offset,UD_U32 length){return UdValidCapacity(capacity)&&length&&length<=UD_TRANSFER&&length%UD_SECTOR==0&&offset%UD_SECTOR==0&&offset<capacity&&length<=capacity-offset;}
#ifdef __cplusplus
static_assert(sizeof(UD_MESSAGE)==65600);
static_assert(sizeof(UD_HEADER)==64);
static_assert(sizeof(UD_STATISTICS)==88);
#else
C_ASSERT(sizeof(UD_MESSAGE)==65600);
C_ASSERT(sizeof(UD_HEADER)==64);
C_ASSERT(sizeof(UD_STATISTICS)==88);
#endif
