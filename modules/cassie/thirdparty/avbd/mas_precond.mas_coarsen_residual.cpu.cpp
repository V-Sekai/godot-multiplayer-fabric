#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_mas_precond_mas_coarsen_residual {


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


#line 69
void _mas_coarsen_residual(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 69
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 69
    KernelContext_0 kernelContext_0;

#line 69
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t s_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(s_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->total_coarse_supernodes_0))
    {

#line 72
        return;
    }
    uint32_t start_0 = (&kernelContext_0)->globalParams_0->coarse_offsets_0.Load(s_0);
    uint32_t _S3 = (&kernelContext_0)->globalParams_0->coarse_offsets_0.Load(s_0 + 1U);

#line 75
    float acc_x_0 = 0.0f;

#line 75
    float acc_y_0 = 0.0f;

#line 75
    float acc_z_0 = 0.0f;

#line 75
    uint32_t k_0 = start_0;



    for(;;)
    {

#line 79
        if(k_0 < _S3)
        {
        }
        else
        {

#line 79
            break;
        }
        Vector<float, 3>  * _S4 = (&((&kernelContext_0)->globalParams_0->r_per_level_0)[uint32_t((&kernelContext_0)->globalParams_0->coarse_indices_0.Load(k_0))]);
        float _S5 = acc_x_0 + (*_S4).x;
        float _S6 = acc_y_0 + (*_S4).y;
        float _S7 = acc_z_0 + (*_S4).z;

#line 79
        uint32_t k_1 = k_0 + 1U;

#line 79
        acc_x_0 = _S5;

#line 79
        acc_y_0 = _S6;

#line 79
        acc_z_0 = _S7;

#line 79
        k_0 = k_1;

#line 79
    }

#line 86
    *(&((&kernelContext_0)->globalParams_0->r_per_level_0)[(slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->ni_0 + s_0]) = Vector<float, 3> (acc_x_0, acc_y_0, acc_z_0);
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_coarsen_residual_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _mas_coarsen_residual(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_coarsen_residual_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _mas_coarsen_residual(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_coarsen_residual(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                mas_coarsen_residual_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_mas_precond_mas_coarsen_residual
