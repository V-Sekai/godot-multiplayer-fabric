#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_curve_rdp {


#line 1 "../thirdparty/avbd/curve_rdp.cpu.slang"
struct RdpParams_0
{
    uint32_t in_count_0;
    float tolerance_0;
};


#line 21
struct GlobalParams_0
{
    RdpParams_0* params_0;
    StructuredBuffer<Vector<float, 3> > in_points_0;
    RWStructuredBuffer<uint32_t> out_keep_0;
    RWStructuredBuffer<uint32_t> out_count_0;
};


#line 21
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 9310 "hlsl.meta.slang"
static Vector<float, 3>  cross_0(Vector<float, 3>  left_0, Vector<float, 3>  right_0)
{

#line 9324
    float _S1 = left_0.y;

#line 9324
    float _S2 = right_0.z;

#line 9324
    float _S3 = left_0.z;

#line 9324
    float _S4 = right_0.y;
    float _S5 = right_0.x;

#line 9325
    float _S6 = left_0.x;

#line 9323
    return Vector<float, 3> (_S1 * _S2 - _S3 * _S4, _S3 * _S5 - _S6 * _S2, _S6 * _S4 - _S1 * _S5);
}


#line 9865
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


#line 16 "../thirdparty/avbd/curve_rdp.cpu.slang"
void _main_0(void* _S7, void* entryPointParams_0, void* globalParams_1)
{

#line 16
    uint32_t split_0;

#line 16
    GlobalParams_0* _S8;

#line 16
    KernelContext_0 kernelContext_0;

#line 16
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    FixedArray<uint32_t, 512>  stack_0;

#line 17
    uint32_t i_1 = 0U;


    for(;;)
    {

#line 20
        _S8 = (&kernelContext_0)->globalParams_0;

#line 20
        if(i_1 < ((&kernelContext_0)->globalParams_0->params_0->in_count_0))
        {
        }
        else
        {

#line 20
            break;
        }

#line 21
        *(&((&kernelContext_0)->globalParams_0->out_keep_0)[i_1]) = 0U;

#line 20
        i_1 = i_1 + 1U;

#line 20
    }


    if((_S8->params_0->in_count_0) >= 1U)
    {

#line 24
        *(&((&kernelContext_0)->globalParams_0->out_keep_0)[0U]) = 1U;

#line 23
    }


    if((_S8->params_0->in_count_0) >= 2U)
    {

#line 27
        *(&((&kernelContext_0)->globalParams_0->out_keep_0)[_S8->params_0->in_count_0 - 1U]) = 1U;

#line 26
    }

#line 26
    uint32_t stack_top_0;


    if((_S8->params_0->in_count_0) >= 3U)
    {

#line 30
        stack_0[0U] = 0U;
        stack_0[1U] = _S8->params_0->in_count_0 - 1U;

#line 31
        stack_top_0 = 1U;

#line 29
    }
    else
    {

#line 29
        stack_top_0 = 0U;

#line 29
    }

#line 34
    for(;;)
    {

#line 34
        if(stack_top_0 > 0U)
        {
        }
        else
        {

#line 34
            break;
        }

#line 35
        uint32_t stack_top_1 = stack_top_0 - 1U;
        uint32_t _S9 = stack_top_1 * 2U;

#line 36
        uint32_t first_0 = stack_0[_S9];
        uint32_t last_0 = stack_0[_S9 + 1U];
        if((stack_0[_S9 + 1U] - stack_0[_S9]) > 1U)
        {

#line 39
            Vector<float, 3>  a_0 = (&kernelContext_0)->globalParams_0->in_points_0.Load(first_0);
            Vector<float, 3>  b_0 = (&kernelContext_0)->globalParams_0->in_points_0.Load(last_0);
            float _S10 = b_0.x;

#line 41
            float _S11 = a_0.x;

#line 41
            float _S12 = b_0.y;

#line 41
            float _S13 = a_0.y;

#line 41
            float _S14 = b_0.z;

#line 41
            float _S15 = a_0.z;

#line 41
            Vector<float, 3>  ab_0 = Vector<float, 3> (_S10 - _S11, _S12 - _S13, _S14 - _S15);
            float _S16 = dot_0(ab_0, ab_0);


            uint32_t _S17 = first_0 + 1U;

#line 45
            float max_dist_0 = _S8->params_0->tolerance_0;

#line 45
            split_0 = 0U;

#line 45
            uint32_t j_0 = _S17;

#line 45
            for(;;)
            {

#line 45
                if(j_0 < last_0)
                {
                }
                else
                {

#line 45
                    break;
                }

#line 46
                Vector<float, 3>  p_0 = (&kernelContext_0)->globalParams_0->in_points_0.Load(j_0);
                float _S18 = p_0.x;

#line 47
                float _S19 = p_0.y;

#line 47
                float _S20 = p_0.z;

#line 47
                Vector<float, 3>  ap_0 = Vector<float, 3> (_S18 - _S11, _S19 - _S13, _S20 - _S15);
                Vector<float, 3>  bp_0 = Vector<float, 3> (_S18 - _S10, _S19 - _S12, _S20 - _S14);

#line 48
                float d_0;
                if(_S16 <= 0.0f)
                {

#line 49
                    d_0 = length_0(ap_0);

#line 49
                }
                else
                {

#line 49
                    Vector<float, 3>  _S21 = cross_0(ap_0, bp_0);

#line 49
                    d_0 = (F32_sqrt((dot_0(_S21, _S21) / _S16)));

#line 49
                }
                if(d_0 > max_dist_0)
                {

#line 50
                    max_dist_0 = d_0;

#line 50
                    split_0 = j_0;

#line 50
                }

#line 45
                j_0 = j_0 + 1U;

#line 45
            }

#line 45
            uint32_t stack_top_2;

#line 55
            if(split_0 != 0U)
            {

#line 56
                *(&((&kernelContext_0)->globalParams_0->out_keep_0)[split_0]) = 1U;
                if(stack_top_1 < 255U)
                {

#line 58
                    stack_0[_S9] = first_0;
                    stack_0[_S9 + 1U] = split_0;

#line 59
                    stack_top_2 = stack_top_1 + 1U;

#line 57
                }
                else
                {

#line 57
                    stack_top_2 = stack_top_1;

#line 57
                }

#line 57
                uint32_t stack_top_3;

#line 62
                if(stack_top_2 < 255U)
                {

#line 63
                    uint32_t _S22 = stack_top_2 * 2U;

#line 63
                    stack_0[_S22] = split_0;
                    stack_0[_S22 + 1U] = last_0;

#line 64
                    stack_top_3 = stack_top_2 + 1U;

#line 62
                }
                else
                {

#line 62
                    stack_top_3 = stack_top_2;

#line 62
                }

#line 62
                stack_top_2 = stack_top_3;

#line 55
            }
            else
            {

#line 55
                stack_top_2 = stack_top_1;

#line 55
            }

#line 55
            stack_top_0 = stack_top_2;

#line 38
        }
        else
        {

#line 38
            stack_top_0 = stack_top_1;

#line 38
        }

#line 34
    }

#line 34
    split_0 = 0U;

#line 34
    i_1 = 0U;

#line 70
    for(;;)
    {

#line 70
        if(i_1 < (_S8->params_0->in_count_0))
        {
        }
        else
        {

#line 70
            break;
        }

#line 71
        if((*(&((&kernelContext_0)->globalParams_0->out_keep_0)[i_1])) == 1U)
        {

#line 71
            split_0 = split_0 + 1U;

#line 71
        }

#line 70
        i_1 = i_1 + 1U;

#line 70
    }

#line 75
    *(&((&kernelContext_0)->globalParams_0->out_count_0)[0U]) = split_0;
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
} // namespace cassie_slang_curve_rdp
