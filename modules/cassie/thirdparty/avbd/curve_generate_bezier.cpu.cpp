#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_curve_generate_bezier {


#line 1 "../thirdparty/avbd/curve_generate_bezier.cpu.slang"
struct GbParams_0
{
    Vector<float, 3>  tangent_a_0;
    Vector<float, 3>  tangent_b_0;
    uint32_t count_0;
};


#line 69
struct GlobalParams_0
{
    GbParams_0* params_0;
    StructuredBuffer<Vector<float, 3> > in_points_0;
    StructuredBuffer<float> in_u_0;
    RWStructuredBuffer<Vector<float, 3> > out_ctrl_0;
};


#line 69
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


#line 12120
static float length_0(Vector<float, 3>  x_1)
{

#line 12132
    return (F32_sqrt((dot_0(x_1, x_1))));
}


#line 17 "../thirdparty/avbd/curve_generate_bezier.cpu.slang"
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 17
    KernelContext_0 kernelContext_0;

#line 17
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    Vector<float, 3>  p0_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->in_points_0.Load(0U);
    Vector<float, 3>  p3_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->in_points_0.Load((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->count_0 - 1U);

#line 19
    float c00_0 = 0.0f;

#line 19
    float c01_0 = 0.0f;

#line 19
    float c11_0 = 0.0f;

#line 19
    float x0_0 = 0.0f;

#line 19
    float x1_0 = 0.0f;

#line 19
    uint32_t i_1 = 0U;

#line 25
    for(;;)
    {

#line 25
        if(i_1 < ((slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->count_0))
        {
        }
        else
        {

#line 25
            break;
        }

#line 26
        float ui_0 = (&kernelContext_0)->globalParams_0->in_u_0.Load(i_1);
        float omu_0 = 1.0f - ui_0;
        float _S2 = omu_0 * omu_0;

#line 28
        float ka_0 = 3.0f * (_S2 * ui_0);
        float _S3 = ui_0 * ui_0;

#line 29
        float kb_0 = 3.0f * (_S3 * omu_0);
        Vector<float, 3>  a1_0 = Vector<float, 3> (ka_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.x, ka_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.y, ka_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.z);
        Vector<float, 3>  a2_0 = Vector<float, 3> (kb_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.x, kb_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.y, kb_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.z);
        float _S4 = c00_0 + dot_0(a1_0, a1_0);
        float _S5 = c01_0 + dot_0(a1_0, a2_0);
        float _S6 = c11_0 + dot_0(a2_0, a2_0);
        float _S7 = 2.0f * ui_0;

#line 35
        float wA_0 = _S2 * (1.0f + _S7);
        float wB_0 = _S3 * (3.0f - _S7);

        Vector<float, 3>  pi_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->in_points_0.Load(i_1);
        Vector<float, 3>  tmp_0 = Vector<float, 3> (pi_0.x - (wA_0 * p0_0.x + wB_0 * p3_0.x), pi_0.y - (wA_0 * p0_0.y + wB_0 * p3_0.y), pi_0.z - (wA_0 * p0_0.z + wB_0 * p3_0.z));
        float _S8 = x0_0 + dot_0(a1_0, tmp_0);
        float _S9 = x1_0 + dot_0(a2_0, tmp_0);

#line 25
        uint32_t i_2 = i_1 + 1U;

#line 25
        c00_0 = _S4;

#line 25
        c01_0 = _S5;

#line 25
        c11_0 = _S6;

#line 25
        x0_0 = _S8;

#line 25
        x1_0 = _S9;

#line 25
        i_1 = i_2;

#line 25
    }

#line 43
    float det_0 = c00_0 * c11_0 - c01_0 * c01_0;
    float det_x_0 = c00_0 * x1_0 - c01_0 * x0_0;
    float det_y_0 = x0_0 * c11_0 - x1_0 * c01_0;
    float _S10 = p3_0.x;

#line 46
    float _S11 = p0_0.x;

#line 46
    float _S12 = p3_0.y;

#line 46
    float _S13 = p0_0.y;

#line 46
    float _S14 = p3_0.z;

#line 46
    float _S15 = p0_0.z;
    float seg_length_0 = length_0(Vector<float, 3> (_S10 - _S11, _S12 - _S13, _S14 - _S15));
    float epsilon_0 = 0.00000999999974738f * seg_length_0;

#line 48
    uint32_t fallback_0;


    if((F32_abs((det_0))) < 0.0f)
    {

#line 51
        fallback_0 = 1U;

#line 51
    }
    else
    {

#line 51
        fallback_0 = 0U;

#line 51
    }

#line 51
    float alpha_a_0;

#line 51
    float alpha_b_0;
    if(fallback_0 == 0U)
    {

#line 53
        float alpha_a_1 = det_y_0 / det_0;
        float alpha_b_1 = det_x_0 / det_0;

#line 54
        bool _S16;
        if(alpha_a_1 < epsilon_0)
        {

#line 55
            _S16 = true;

#line 55
        }
        else
        {

#line 55
            _S16 = alpha_b_1 < epsilon_0;

#line 55
        }

#line 55
        if(_S16)
        {

#line 55
            fallback_0 = 1U;

#line 55
        }

#line 55
        alpha_a_0 = alpha_a_1;

#line 55
        alpha_b_0 = alpha_b_1;

#line 52
    }
    else
    {

#line 52
        alpha_a_0 = 0.0f;

#line 52
        alpha_b_0 = 0.0f;

#line 52
    }

#line 52
    Vector<float, 3>  p1_0;

#line 52
    Vector<float, 3>  p2_0;

#line 61
    if(fallback_0 == 1U)
    {

#line 62
        float third_0 = seg_length_0 / 3.0f;

        Vector<float, 3>  _S17 = Vector<float, 3> (_S10 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.x, _S12 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.y, _S14 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.z);

#line 64
        p1_0 = Vector<float, 3> (_S11 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.x, _S13 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.y, _S15 + third_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.z);

#line 64
        p2_0 = _S17;

#line 61
    }
    else
    {



        Vector<float, 3>  _S18 = Vector<float, 3> (_S10 + alpha_b_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.x, _S12 + alpha_b_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.y, _S14 + alpha_b_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_b_0.z);

#line 67
        p1_0 = Vector<float, 3> (_S11 + alpha_a_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.x, _S13 + alpha_a_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.y, _S15 + alpha_a_0 * (slang_bit_cast<GlobalParams_0*>(globalParams_1))->params_0->tangent_a_0.z);

#line 67
        p2_0 = _S18;

#line 61
    }

#line 69
    *(&((&kernelContext_0)->globalParams_0->out_ctrl_0)[0U]) = p0_0;
    *(&((&kernelContext_0)->globalParams_0->out_ctrl_0)[1U]) = p1_0;
    *(&((&kernelContext_0)->globalParams_0->out_ctrl_0)[2U]) = p2_0;
    *(&((&kernelContext_0)->globalParams_0->out_ctrl_0)[3U]) = p3_0;
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
} // namespace cassie_slang_curve_generate_bezier
