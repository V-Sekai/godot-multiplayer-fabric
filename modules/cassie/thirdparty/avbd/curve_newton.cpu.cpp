#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_curve_newton {


#line 1 "../thirdparty/avbd/curve_newton.cpu.slang"
struct NewtonParams_0
{
    Vector<float, 3>  a_0;
    Vector<float, 3>  b_0;
    Vector<float, 3>  c_0;
    Vector<float, 3>  d_0;
    uint32_t count_0;
};


#line 45
struct GlobalParams_0
{
    NewtonParams_0* params_0;
    StructuredBuffer<Vector<float, 3> > in_points_0;
    StructuredBuffer<float> in_u_0;
    RWStructuredBuffer<float> out_u_0;
};


#line 45
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 9865 "hlsl.meta.slang"
static float dot_0(Vector<float, 3>  x_0, Vector<float, 3>  y_0)
{

#line 9865
    int32_t i_0 = int(0);

#line 9865
    float result_0 = 0.0f;

#line 9884
    for(;;)
    {

#line 9884
        if(i_0 < int(3))
        {
        }
        else
        {

#line 9884
            break;
        }

#line 9885
        float result_1 = result_0 + _slang_vector_get_element(x_0, i_0) * _slang_vector_get_element(y_0, i_0);

#line 9884
        i_0 = i_0 + int(1);

#line 9884
        result_0 = result_1;

#line 9884
    }

    return result_0;
}


#line 12178
static Vector<float, 3>  lerp_0(Vector<float, 3>  x_1, Vector<float, 3>  y_1, Vector<float, 3>  s_0)
{

#line 12190
    return x_1 + (y_1 - x_1) * s_0;
}


#line 19 "../thirdparty/avbd/curve_newton.cpu.slang"
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 19
    KernelContext_0 kernelContext_0;

#line 19
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 19
    uint32_t i_1 = 0U;
    for(;;)
    {

#line 20
        if(i_1 < ((&kernelContext_0)->globalParams_0->params_0->count_0))
        {
        }
        else
        {

#line 20
            break;
        }

#line 21
        float u_0 = (&kernelContext_0)->globalParams_0->in_u_0.Load(i_1);
        Vector<float, 3>  pt_0 = (&kernelContext_0)->globalParams_0->in_points_0.Load(i_1);
        float omu_0 = 1.0f - u_0;
        float _S2 = (&kernelContext_0)->globalParams_0->params_0->b_0.x - (&kernelContext_0)->globalParams_0->params_0->a_0.x;

#line 24
        float _S3 = (&kernelContext_0)->globalParams_0->params_0->b_0.y - (&kernelContext_0)->globalParams_0->params_0->a_0.y;

#line 24
        float _S4 = (&kernelContext_0)->globalParams_0->params_0->b_0.z - (&kernelContext_0)->globalParams_0->params_0->a_0.z;
        float _S5 = (&kernelContext_0)->globalParams_0->params_0->c_0.x - (&kernelContext_0)->globalParams_0->params_0->b_0.x;

#line 25
        float _S6 = (&kernelContext_0)->globalParams_0->params_0->c_0.y - (&kernelContext_0)->globalParams_0->params_0->b_0.y;

#line 25
        float _S7 = (&kernelContext_0)->globalParams_0->params_0->c_0.z - (&kernelContext_0)->globalParams_0->params_0->b_0.z;
        float _S8 = (&kernelContext_0)->globalParams_0->params_0->d_0.x - (&kernelContext_0)->globalParams_0->params_0->c_0.x;

#line 26
        float _S9 = (&kernelContext_0)->globalParams_0->params_0->d_0.y - (&kernelContext_0)->globalParams_0->params_0->c_0.y;

#line 26
        float _S10 = (&kernelContext_0)->globalParams_0->params_0->d_0.z - (&kernelContext_0)->globalParams_0->params_0->c_0.z;
        float omu2_0 = omu_0 * omu_0;
        float u2_0 = u_0 * u_0;
        float twoOmU_0 = 2.0f * (omu_0 * u_0);
        Vector<float, 3>  q1_0 = Vector<float, 3> (3.0f * (omu2_0 * _S2 + twoOmU_0 * _S5 + u2_0 * _S8), 3.0f * (omu2_0 * _S3 + twoOmU_0 * _S6 + u2_0 * _S9), 3.0f * (omu2_0 * _S4 + twoOmU_0 * _S7 + u2_0 * _S10));



        Vector<float, 3>  _S11 = (Vector<float, 3> )u_0;
        Vector<float, 3>  qbc_0 = lerp_0((&kernelContext_0)->globalParams_0->params_0->b_0, (&kernelContext_0)->globalParams_0->params_0->c_0, _S11);



        Vector<float, 3>  qval_0 = lerp_0(lerp_0(lerp_0((&kernelContext_0)->globalParams_0->params_0->a_0, (&kernelContext_0)->globalParams_0->params_0->b_0, _S11), qbc_0, _S11), lerp_0(qbc_0, lerp_0((&kernelContext_0)->globalParams_0->params_0->c_0, (&kernelContext_0)->globalParams_0->params_0->d_0, _S11), _S11), _S11);
        Vector<float, 3>  e_0 = Vector<float, 3> (qval_0.x - pt_0.x, qval_0.y - pt_0.y, qval_0.z - pt_0.z);
        float num_0 = dot_0(e_0, q1_0);
        float den_0 = dot_0(q1_0, q1_0) + dot_0(e_0, Vector<float, 3> (6.0f * (omu_0 * (_S5 - _S2) + u_0 * (_S8 - _S5)), 6.0f * (omu_0 * (_S6 - _S3) + u_0 * (_S9 - _S6)), 6.0f * (omu_0 * (_S7 - _S4) + u_0 * (_S10 - _S7))));

#line 42
        float u_new_0;

        if((F32_abs((den_0))) < 0.0f)
        {

#line 44
            u_new_0 = u_0;

#line 44
        }
        else
        {

#line 44
            u_new_0 = u_0 - num_0 / den_0;

#line 44
        }
        *(&((&kernelContext_0)->globalParams_0->out_u_0)[i_1]) = u_new_0;

#line 20
        i_1 = i_1 + 1U;

#line 20
    }

#line 47
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
} // namespace cassie_slang_curve_newton
