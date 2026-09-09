#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_mas_precond_mas_sum_levels {


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


#line 42
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


#line 42
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 121
void _mas_sum_levels(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 121
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 121
    KernelContext_0 kernelContext_0;

#line 121
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(i_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->ni_0))
    {

#line 124
        return;
    }
    Vector<float, 3>  * _S3 = (&((&kernelContext_0)->globalParams_0->z_per_level_0)[i_0]);

    uint32_t _S4 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->num_levels_0;

#line 128
    uint32_t off_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->ni_0;

#line 128
    Vector<float, 3>  acc_0 = *_S3;

#line 128
    uint32_t l_0 = 1U;
    for(;;)
    {

#line 129
        if(l_0 < _S4)
        {
        }
        else
        {

#line 129
            break;
        }

        Vector<float, 3>  * _S5 = (&((&kernelContext_0)->globalParams_0->z_per_level_0)[off_0 + uint32_t(*(&((&kernelContext_0)->globalParams_0->map_per_level_0)[(l_0 - 1U) * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->ni_0 + i_0]))]);
        Vector<float, 3>  _S6 = Vector<float, 3> (acc_0.x + (*_S5).x, acc_0.y + (*_S5).y, acc_0.z + (*_S5).z);
        uint32_t _S7 = off_0 + (&kernelContext_0)->globalParams_0->level_sizes_0.Load(l_0);

#line 129
        uint32_t l_1 = l_0 + 1U;

#line 129
        off_0 = _S7;

#line 129
        acc_0 = _S6;

#line 129
        l_0 = l_1;

#line 129
    }

#line 136
    *(&((&kernelContext_0)->globalParams_0->z_output_0)[i_0]) = acc_0;
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_sum_levels_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _mas_sum_levels(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_sum_levels_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _mas_sum_levels(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_sum_levels(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                mas_sum_levels_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_mas_precond_mas_sum_levels
