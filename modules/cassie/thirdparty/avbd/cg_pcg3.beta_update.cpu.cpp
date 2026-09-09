#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_pcg3_beta_update {


#line 1 "../thirdparty/avbd/cg_pcg3.cpu.slang"
struct CgPcg3Params_0
{
    uint32_t rows_0;
};


#line 232
struct GlobalParams_0
{
    CgPcg3Params_0* params_0;
    StructuredBuffer<int32_t> rowPtr_0;
    StructuredBuffer<int32_t> colIdx_0;
    StructuredBuffer<float> values_0;
    StructuredBuffer<float> diag_inv_0;
    StructuredBuffer<Vector<float, 3> > b_0;
    RWStructuredBuffer<Vector<float, 3> > x_0;
    RWStructuredBuffer<Vector<float, 3> > r_0;
    RWStructuredBuffer<Vector<float, 3> > z_0;
    RWStructuredBuffer<Vector<float, 3> > p_0;
    RWStructuredBuffer<Vector<float, 3> > Ap_0;
    RWStructuredBuffer<float> scalars_0;
};


#line 231
void _beta_update(void* _S1, void* entryPointParams_0, void* globalParams_0)
{

#line 232
    float rz_old_0 = *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[0U]) + *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[1U]);
    float * _S2 = (&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[6U]);

#line 233
    float rz_new_hi_0 = *_S2;
    float * _S3 = (&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[7U]);

#line 234
    float rz_new_lo_0 = *_S3;
    float rz_new_0 = *_S2 + *_S3;

#line 235
    float beta_0;
    if(rz_old_0 > 0.0f)
    {

#line 236
        beta_0 = rz_new_0 / rz_old_0;

#line 236
    }
    else
    {

#line 236
        beta_0 = 0.0f;

#line 236
    }
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[6U]) = beta_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[0U]) = rz_new_hi_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->scalars_0)[1U]) = rz_new_lo_0;
    return;
}

// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void beta_update_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _beta_update(varyingInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void beta_update_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    _beta_update(&threadInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void beta_update(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                beta_update_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_cg_pcg3_beta_update
