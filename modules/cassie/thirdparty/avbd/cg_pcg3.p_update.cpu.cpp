#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_pcg3_p_update {


#line 1 "../thirdparty/avbd/cg_pcg3.cpu.slang"
struct CgPcg3Params_0
{
    uint32_t rows_0;
};


#line 249
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


#line 249
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 244
void _p_update(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 244
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 244
    KernelContext_0 kernelContext_0;

#line 244
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(i_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->rows_0))
    {

#line 247
        return;
    }
    float * _S3 = (&((&kernelContext_0)->globalParams_0->scalars_0)[6U]);
    Vector<float, 3>  * _S4 = (&((&kernelContext_0)->globalParams_0->z_0)[i_0]);
    Vector<float, 3>  * _S5 = (&((&kernelContext_0)->globalParams_0->p_0)[i_0]);
    *(&((&kernelContext_0)->globalParams_0->p_0)[i_0]) = Vector<float, 3> ((F32_fma((*_S3), ((*_S5).x), ((*_S4).x))), (F32_fma((*_S3), ((*_S5).y), ((*_S4).y))), (F32_fma((*_S3), ((*_S5).z), ((*_S4).z))));
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void p_update_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _p_update(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void p_update_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _p_update(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void p_update(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                p_update_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_cg_pcg3_p_update
