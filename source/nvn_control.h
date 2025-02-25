#include <stddef.h>
#include "nv_attributes.h"
#include "nv_shader_header.h"

typedef unsigned long long uint64_t;
typedef signed long long int64_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;

enum NVNshaderStage {
    NVN_SHADER_STAGE_VERTEX = 0,
    NVN_SHADER_STAGE_FRAGMENT = 1,
    NVN_SHADER_STAGE_GEOMETRY = 2,
    NVN_SHADER_STAGE_TESS_CONTROL = 3,
    NVN_SHADER_STAGE_TESS_EVALUATION = 4,
    NVN_SHADER_STAGE_COMPUTE = 5
};

// all paddings should be memset to 0s
struct NVNshaderControl {
    uint32_t magic; // = 0x98761234

    uint32_t mMajorVer; // = 1
    uint32_t mMinorVer; // = 5, lowest supported version for maximum compatibility as nvn is backwards compatible

    uint32_t unk0; // = 120
    uint32_t unk1; // = 0xb

    uint32_t mGlasmOffset; // = sizeof(NVNshaderControl) - 8
    uint32_t mGlasmSize; // = 0
    uint32_t mGlasmUnk0; // = 0
    uint32_t mGlasmUnk1; // = sizeof(NVNshaderControl) - 7
    uint8_t padding1[0x6f0 - 0x24]; 

    uint32_t unk2; // = sizeof(NVNshaderControl) - 7
    uint32_t unk3; // = 0

    uint32_t mProgramSize; // (header + code, not including 0x30 first bytes)

    uint32_t mConstBufSize; // = constant size
    uint32_t mConstBufOffset; // = 0x30 + 0x50 + N + padding to 256 byte boundary

    uint32_t mShaderSize; // = 0x30 + 0x50 + mProgramSize + align 256 + mConstBufSize + align 256

    uint32_t mProgramOffset; // = 0x30
    uint32_t mProgramRegNum; // reg num
    uint32_t mPerWarpScratchSize; // idk some thing

    NVNshaderStage mShaderStage; // stage

    uint8_t early_fragment_tests; // for frag
    uint8_t post_depth_coverage; // for frag

    uint8_t padding2[0x2];

    uint8_t writesDepth; // for frag

    uint8_t padding3[0x15];

    uint32_t numColourResults; // for frag

    uint8_t padding4[0x10]; 

    union {
        struct {
            uint8_t   paddingFragUnk[2]; // 0s
            uint8_t   per_sample_invocation;
        } frag;
        struct
		{
			uint32_t block_dims[3];
			uint32_t shared_mem_sz;
			uint32_t local_pos_mem_sz;
			uint32_t local_neg_mem_sz;
			uint32_t crs_sz;
			uint32_t num_barriers;
		} comp; // same as dksh
    };


    uint8_t endPadding[0x60]; // 0x7C0 + 8 zeroed bytes just in case
};

struct GPUProgramHeader {
    uint32_t magic; // = 0x12345678
    uint8_t padding[0x30 - 0x4]; // zeroes
    NvShaderHeader nvsh;
    // program goes here, offset = 0x30 + 0x50, then align 256, const if present, align 256;
};