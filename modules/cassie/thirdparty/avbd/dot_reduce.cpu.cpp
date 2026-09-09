#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_dot_reduce {


#line 1 "../thirdparty/avbd/dot_reduce.cpu.slang"
struct DotReduceSerialParams_0
{
    uint32_t n_0;
};


#line 64
struct GlobalParams_0
{
    DotReduceSerialParams_0* params_0;
    StructuredBuffer<float> a_0;
    StructuredBuffer<float> b_0;
    RWStructuredBuffer<float> dst_0;
};


#line 64
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 33
static void two_prod_0(float a_1, float b_1, float * hi_0, float * lo_0)
{

#line 34
    float h_0 = a_1 * b_1;
    *hi_0 = h_0;
    *lo_0 = (F32_fma((a_1), (b_1), (- h_0)));
    return;
}


#line 14
static void two_sum_0(float a_2, float b_2, float * hi_1, float * lo_1)
{

#line 15
    float h_1 = a_2 + b_2;
    float bb_0 = h_1 - a_2;

    float lo_a_0 = a_2 - (h_1 - bb_0);
    float lo_b_0 = b_2 - bb_0;
    *hi_1 = h_1;
    *lo_1 = lo_a_0 + lo_b_0;
    return;
}

static void quick_two_sum_0(float a_3, float b_3, float * hi_2, float * lo_2)
{

#line 26
    float h_2 = a_3 + b_3;
    float t_0 = h_2 - a_3;
    *hi_2 = h_2;
    *lo_2 = b_3 - t_0;
    return;
}


#line 40
static void df_add_0(float x_hi_0, float x_lo_0, float y_hi_0, float y_lo_0, float * z_hi_0, float * z_lo_0)
{

#line 41
    float sh_0;
    float sl_0;
    two_sum_0(x_hi_0, y_hi_0, &sh_0, &sl_0);


    quick_two_sum_0(sh_0, sl_0 + (x_lo_0 + y_lo_0), z_hi_0, z_lo_0);
    return;
}


void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 51
    KernelContext_0 kernelContext_0;

#line 51
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 51
    float acc_hi_0 = 0.0f;

#line 51
    float acc_lo_0 = 0.0f;

#line 51
    uint32_t i_0 = 0U;


    for(;;)
    {

#line 54
        if(i_0 < ((&kernelContext_0)->globalParams_0->params_0->n_0))
        {
        }
        else
        {

#line 54
            break;
        }

#line 55
        float p_hi_0;
        float p_lo_0;
        two_prod_0((&kernelContext_0)->globalParams_0->a_0.Load(i_0), (&kernelContext_0)->globalParams_0->b_0.Load(i_0), &p_hi_0, &p_lo_0);
        float new_hi_0;
        float new_lo_0;
        df_add_0(acc_hi_0, acc_lo_0, p_hi_0, p_lo_0, &new_hi_0, &new_lo_0);
        float _S2 = new_hi_0;
        float _S3 = new_lo_0;

#line 54
        uint32_t i_1 = i_0 + 1U;

#line 54
        acc_hi_0 = _S2;

#line 54
        acc_lo_0 = _S3;

#line 54
        i_0 = i_1;

#line 54
    }

#line 64
    *(&((&kernelContext_0)->globalParams_0->dst_0)[0U]) = acc_hi_0;
    *(&((&kernelContext_0)->globalParams_0->dst_0)[1U]) = acc_lo_0;
    return;
}

// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _main_0(varyingInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    _main_0(&threadInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
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
} // namespace cassie_slang_dot_reduce
