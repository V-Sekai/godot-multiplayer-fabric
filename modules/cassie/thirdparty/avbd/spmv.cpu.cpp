#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_spmv {


#line 1 "../thirdparty/avbd/spmv.cpu.slang"
struct SpmvDf32Params_0
{
    uint32_t rows_0;
};


#line 76
struct GlobalParams_0
{
    SpmvDf32Params_0* params_0;
    StructuredBuffer<int32_t> rowPtr_0;
    StructuredBuffer<int32_t> colIdx_0;
    StructuredBuffer<float> values_0;
    StructuredBuffer<float> x_0;
    RWStructuredBuffer<float> y_0;
};


#line 76
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 37
static void two_prod_0(float a_0, float b_0, float * hi_0, float * lo_0)
{

#line 38
    float h_0 = a_0 * b_0;
    *hi_0 = h_0;
    *lo_0 = (F32_fma((a_0), (b_0), (- h_0)));
    return;
}


#line 18
static void two_sum_0(float a_1, float b_1, float * hi_1, float * lo_1)
{

#line 19
    float h_1 = a_1 + b_1;
    float bb_0 = h_1 - a_1;

    float lo_a_0 = a_1 - (h_1 - bb_0);
    float lo_b_0 = b_1 - bb_0;
    *hi_1 = h_1;
    *lo_1 = lo_a_0 + lo_b_0;
    return;
}

static void quick_two_sum_0(float a_2, float b_2, float * hi_2, float * lo_2)
{

#line 30
    float h_2 = a_2 + b_2;
    float t_0 = h_2 - a_2;
    *hi_2 = h_2;
    *lo_2 = b_2 - t_0;
    return;
}


#line 44
static void df_add_0(float x_hi_0, float x_lo_0, float y_hi_0, float y_lo_0, float * z_hi_0, float * z_lo_0)
{

#line 45
    float sh_0;
    float sl_0;
    two_sum_0(x_hi_0, y_hi_0, &sh_0, &sl_0);


    quick_two_sum_0(sh_0, sl_0 + (x_lo_0 + y_lo_0), z_hi_0, z_lo_0);
    return;
}


void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 55
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));

#line 55
    KernelContext_0 kernelContext_0;

#line 55
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (256U, 1U, 1U) + _S2->groupThreadID).x;
    if(i_0 >= ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->rows_0))
    {

#line 58
        return;
    }
    uint32_t rs_0 = uint32_t((&kernelContext_0)->globalParams_0->rowPtr_0.Load(i_0));
    uint32_t _S3 = uint32_t((&kernelContext_0)->globalParams_0->rowPtr_0.Load(i_0 + 1U));

#line 61
    float s_hi_0 = 0.0f;

#line 61
    float s_lo_0 = 0.0f;

#line 61
    uint32_t p_0 = rs_0;


    for(;;)
    {

#line 64
        if(p_0 < _S3)
        {
        }
        else
        {

#line 64
            break;
        }

        float t_hi_0;
        float t_lo_0;
        two_prod_0((&kernelContext_0)->globalParams_0->values_0.Load(p_0), (&kernelContext_0)->globalParams_0->x_0.Load(uint32_t((&kernelContext_0)->globalParams_0->colIdx_0.Load(p_0))), &t_hi_0, &t_lo_0);
        float n_hi_0;
        float n_lo_0;
        df_add_0(s_hi_0, s_lo_0, t_hi_0, t_lo_0, &n_hi_0, &n_lo_0);
        float _S4 = n_hi_0;
        float _S5 = n_lo_0;

#line 64
        uint32_t p_1 = p_0 + 1U;

#line 64
        s_hi_0 = _S4;

#line 64
        s_lo_0 = _S5;

#line 64
        p_0 = p_1;

#line 64
    }

#line 76
    *(&((&kernelContext_0)->globalParams_0->y_0)[i_0]) = s_hi_0 + s_lo_0;
    return;
}

// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _main_0(varyingInput, entryPointParams, globalParams);
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 256; ++x)
    {
        threadInput.groupThreadID.x = x;
        _main_0(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(256, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                main_0_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_spmv
