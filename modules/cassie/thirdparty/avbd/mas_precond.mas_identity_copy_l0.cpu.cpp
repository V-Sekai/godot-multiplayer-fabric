#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_mas_precond_mas_identity_copy_l0 {


#line 1 "../thirdparty/avbd/mas_precond.cpu.slang"
struct MasParams_0
{
    uint32_t ni_0;
    uint32_t num_levels_0;
    uint32_t domain_size_0;
    float aabb_min_x_0;
    float aabb_min_y_0;
    float aabb_min_z_0;
    float aabb_size_x_0;
    float aabb_size_y_0;
    float aabb_size_z_0;
    uint32_t level_0;
    uint32_t level_r_offset_0;
    uint32_t level_z_offset_0;
    uint32_t level_domain_offset_0;
    uint32_t level_ni_0;
    uint32_t total_coarse_supernodes_0;
};


#line 64
struct GlobalParams_0
{
    MasParams_0* params_0;
    StructuredBuffer<int32_t> rowPtr_0;
    StructuredBuffer<int32_t> colIdx_0;
    StructuredBuffer<float> values_0;
    RWStructuredBuffer<uint32_t> morton_0;
    StructuredBuffer<int32_t> sorted_idx_0;
    RWStructuredBuffer<int32_t> map_per_level_0;
    StructuredBuffer<uint32_t> domain_offsets_0;
    RWStructuredBuffer<float> m_inv_packed_0;
    RWStructuredBuffer<Vector<float, 3> > r_per_level_0;
    RWStructuredBuffer<Vector<float, 3> > z_per_level_0;
    StructuredBuffer<Vector<float, 3> > r_input_0;
    RWStructuredBuffer<Vector<float, 3> > z_output_0;
    StructuredBuffer<uint32_t> coarse_offsets_0;
    StructuredBuffer<int32_t> coarse_indices_0;
    StructuredBuffer<uint32_t> level_sizes_0;
    StructuredBuffer<Vector<float, 3> > positions_0;
    RWStructuredBuffer<uint32_t> connect_mask_0;
    RWStructuredBuffer<float> dense_workspace_0;
};


#line 64
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 59
void _mas_identity_copy_l0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 59
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 59
    KernelContext_0 kernelContext_0;

#line 59
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(i_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->ni_0))
    {

#line 62
        return;
    }
    *(&((&kernelContext_0)->globalParams_0->r_per_level_0)[i_0]) = (&kernelContext_0)->globalParams_0->r_input_0.Load(i_0);
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_identity_copy_l0_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _mas_identity_copy_l0(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_identity_copy_l0_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _mas_identity_copy_l0(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_identity_copy_l0(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeVaryingInput vi = *varyingInput;
    ComputeVaryingInput groupVaryingInput = {};
    for (uint32_t z = vi.startGroupID.z; z < vi.endGroupID.z; ++z)
    {
        groupVaryingInput.startGroupID.z = z;
        for (uint32_t y = vi.startGroupID.y; y < vi.endGroupID.y; ++y)
        {
            groupVaryingInput.startGroupID.y = y;
            for (uint32_t x = vi.startGroupID.x; x < vi.endGroupID.x; ++x)
            {
                groupVaryingInput.startGroupID.x = x;
                mas_identity_copy_l0_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_mas_precond_mas_identity_copy_l0
