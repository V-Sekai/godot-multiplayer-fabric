#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_mas_precond_mas_per_domain_solve {


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


#line 91
void _mas_per_domain_solve(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 91
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 91
    KernelContext_0 kernelContext_0;

#line 91
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t domain_0 = _S2->groupID.x;
    uint32_t lane_0 = _S2->groupThreadID.x;
    uint32_t sigma_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->domain_size_0;
    uint32_t _S3 = domain_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->domain_size_0;

#line 95
    uint32_t vert_0 = _S3 + lane_0;
    if(vert_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->level_ni_0))
    {

#line 97
        return;
    }

    uint32_t domain_base_0 = (&kernelContext_0)->globalParams_0->domain_offsets_0.Load((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->level_domain_offset_0 + domain_0);
    uint32_t _S4 = domain_base_0 + ((lane_0 * (lane_0 + 1U)) >> 1U);

#line 101
    float acc_x_0 = 0.0f;

#line 101
    float acc_y_0 = 0.0f;

#line 101
    float acc_z_0 = 0.0f;

#line 101
    uint32_t j_0 = 0U;



    for(;;)
    {

#line 105
        if(j_0 < sigma_0)
        {
        }
        else
        {

#line 105
            break;
        }

#line 106
        uint32_t other_vert_0 = _S3 + j_0;
        if(other_vert_0 < ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->level_ni_0))
        {

#line 107
            uint32_t addr_0;
            if(j_0 <= lane_0)
            {

#line 108
                addr_0 = _S4 + j_0;

#line 108
            }
            else
            {

#line 108
                addr_0 = domain_base_0 + ((j_0 * (j_0 + 1U)) >> 1U) + lane_0;

#line 108
            }
            float * _S5 = (&((&kernelContext_0)->globalParams_0->m_inv_packed_0)[addr_0]);
            Vector<float, 3>  * _S6 = (&((&kernelContext_0)->globalParams_0->r_per_level_0)[(slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->level_r_offset_0 + other_vert_0]);

            float _S7 = (F32_fma((*_S5), ((*_S6).y), (acc_y_0)));
            float _S8 = (F32_fma((*_S5), ((*_S6).z), (acc_z_0)));

#line 113
            acc_x_0 = (F32_fma((*_S5), ((*_S6).x), (acc_x_0)));

#line 113
            acc_y_0 = _S7;

#line 113
            acc_z_0 = _S8;

#line 107
        }

#line 105
        j_0 = j_0 + 1U;

#line 105
    }

#line 116
    *(&((&kernelContext_0)->globalParams_0->z_per_level_0)[(slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->level_z_offset_0 + vert_0]) = Vector<float, 3> (acc_x_0, acc_y_0, acc_z_0);
    return;
}

// [numthreads(32, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_per_domain_solve_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _mas_per_domain_solve(varyingInput, entryPointParams, globalParams);
}
// [numthreads(32, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_per_domain_solve_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 32; ++x)
    {
        threadInput.groupThreadID.x = x;
        _mas_per_domain_solve(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(32, 1, 1)]
SLANG_PRELUDE_EXPORT
void mas_per_domain_solve(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                mas_per_domain_solve_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_mas_precond_mas_per_domain_solve
