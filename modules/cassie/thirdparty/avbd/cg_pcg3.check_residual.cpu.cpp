#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_pcg3_check_residual {


#line 1 "../thirdparty/avbd/cg_pcg3.cpu.slang"
struct CgPcg3Params_0
{
    uint32_t rows_0;
};


#line 284
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


#line 284
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 49
static void two_prod_0(float a_0, float b_1, float * hi_0, float * lo_0)
{

#line 50
    float h_0 = a_0 * b_1;
    *hi_0 = h_0;
    *lo_0 = (F32_fma((a_0), (b_1), (- h_0)));
    return;
}


#line 30
static void two_sum_0(float a_1, float b_2, float * hi_1, float * lo_1)
{

#line 31
    float h_1 = a_1 + b_2;
    float bb_0 = h_1 - a_1;

    float lo_a_0 = a_1 - (h_1 - bb_0);
    float lo_b_0 = b_2 - bb_0;
    *hi_1 = h_1;
    *lo_1 = lo_a_0 + lo_b_0;
    return;
}

static void quick_two_sum_0(float a_2, float b_3, float * hi_2, float * lo_2)
{

#line 42
    float h_2 = a_2 + b_3;
    float t_0 = h_2 - a_2;
    *hi_2 = h_2;
    *lo_2 = b_3 - t_0;
    return;
}


#line 56
static void df_add_0(float x_hi_0, float x_lo_0, float y_hi_0, float y_lo_0, float * z_hi_0, float * z_lo_0)
{

#line 57
    float sh_0;
    float sl_0;
    two_sum_0(x_hi_0, y_hi_0, &sh_0, &sl_0);


    quick_two_sum_0(sh_0, sl_0 + (x_lo_0 + y_lo_0), z_hi_0, z_lo_0);
    return;
}


#line 257
void _check_residual(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 257
    KernelContext_0 kernelContext_0;

#line 257
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 257
    float acc_hi_0 = 0.0f;

#line 257
    float acc_lo_0 = 0.0f;

#line 257
    uint32_t i_0 = 0U;


    for(;;)
    {

#line 260
        if(i_0 < ((&kernelContext_0)->globalParams_0->params_0->rows_0))
        {
        }
        else
        {

#line 260
            break;
        }

#line 261
        Vector<float, 3>  * _S2 = (&((&kernelContext_0)->globalParams_0->r_0)[i_0]);


        float _S3 = (*_S2).x;

#line 262
        float px_hi_0;
        float px_lo_0;
        two_prod_0(_S3, _S3, &px_hi_0, &px_lo_0);


        float _S4 = (*_S2).y;

#line 265
        float py_hi_0;
        float py_lo_0;
        two_prod_0(_S4, _S4, &py_hi_0, &py_lo_0);


        float _S5 = (*_S2).z;

#line 268
        float pz_hi_0;
        float pz_lo_0;
        two_prod_0(_S5, _S5, &pz_hi_0, &pz_lo_0);
        float t_hi_0;
        float t_lo_0;
        df_add_0(px_hi_0, px_lo_0, py_hi_0, py_lo_0, &t_hi_0, &t_lo_0);
        float p_hi_0;
        float p_lo_0;
        df_add_0(t_hi_0, t_lo_0, pz_hi_0, pz_lo_0, &p_hi_0, &p_lo_0);
        float new_hi_0;
        float new_lo_0;
        df_add_0(acc_hi_0, acc_lo_0, p_hi_0, p_lo_0, &new_hi_0, &new_lo_0);
        float _S6 = new_hi_0;
        float _S7 = new_lo_0;

#line 260
        uint32_t i_1 = i_0 + 1U;

#line 260
        acc_hi_0 = _S6;

#line 260
        acc_lo_0 = _S7;

#line 260
        i_0 = i_1;

#line 260
    }

#line 284
    *(&((&kernelContext_0)->globalParams_0->scalars_0)[9U]) = acc_hi_0 + acc_lo_0;
    return;
}

// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void check_residual_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _check_residual(varyingInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void check_residual_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    _check_residual(&threadInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void check_residual(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                check_residual_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
} // namespace cassie_slang_cg_pcg3_check_residual
