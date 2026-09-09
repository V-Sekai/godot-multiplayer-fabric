#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_pcg3_init {


#line 1 "../thirdparty/avbd/cg_pcg3.cpu.slang"
struct CgPcg3Params_0
{
    uint32_t rows_0;
};


#line 28
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


#line 28
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 67
void _init(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 67
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 67
    KernelContext_0 kernelContext_0;

#line 67
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(i_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->rows_0))
    {

#line 70
        return;
    }
    uint32_t rs_0 = uint32_t((&kernelContext_0)->globalParams_0->rowPtr_0.Load(i_0));
    uint32_t _S3 = uint32_t((&kernelContext_0)->globalParams_0->rowPtr_0.Load(i_0 + 1U));

#line 73
    float s_x_0 = 0.0f;

#line 73
    float s_y_0 = 0.0f;

#line 73
    float s_z_0 = 0.0f;

#line 73
    uint32_t k_0 = rs_0;



    for(;;)
    {

#line 77
        if(k_0 < _S3)
        {
        }
        else
        {

#line 77
            break;
        }

#line 78
        float v_0 = (&kernelContext_0)->globalParams_0->values_0.Load(k_0);
        Vector<float, 3>  * _S4 = (&((&kernelContext_0)->globalParams_0->x_0)[uint32_t((&kernelContext_0)->globalParams_0->colIdx_0.Load(k_0))]);
        float _S5 = (F32_fma((v_0), ((*_S4).x), (s_x_0)));
        float _S6 = (F32_fma((v_0), ((*_S4).y), (s_y_0)));
        float _S7 = (F32_fma((v_0), ((*_S4).z), (s_z_0)));

#line 77
        uint32_t k_1 = k_0 + 1U;

#line 77
        s_x_0 = _S5;

#line 77
        s_y_0 = _S6;

#line 77
        s_z_0 = _S7;

#line 77
        k_0 = k_1;

#line 77
    }

#line 84
    Vector<float, 3>  bi_0 = (&kernelContext_0)->globalParams_0->b_0.Load(i_0);
    float _S8 = bi_0.x - s_x_0;

#line 85
    float _S9 = bi_0.y - s_y_0;

#line 85
    float _S10 = bi_0.z - s_z_0;
    float d_0 = (&kernelContext_0)->globalParams_0->diag_inv_0.Load(i_0);
    Vector<float, 3>  zi_0 = Vector<float, 3> (d_0 * _S8, d_0 * _S9, d_0 * _S10);
    *(&((&kernelContext_0)->globalParams_0->r_0)[i_0]) = Vector<float, 3> (_S8, _S9, _S10);
    *(&((&kernelContext_0)->globalParams_0->z_0)[i_0]) = zi_0;
    *(&((&kernelContext_0)->globalParams_0->p_0)[i_0]) = zi_0;
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void init_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _init(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void init_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _init(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void init(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                init_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_cg_pcg3_init
