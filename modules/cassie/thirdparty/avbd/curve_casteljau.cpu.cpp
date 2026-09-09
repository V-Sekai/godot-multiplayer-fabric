#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_curve_casteljau {


#line 1 "../thirdparty/avbd/curve_casteljau.cpu.slang"
struct CasteljauParams_0
{
    Vector<float, 3>  a_0;
    Vector<float, 3>  b_0;
    Vector<float, 3>  c_0;
    Vector<float, 3>  d_0;
    float u_0;
};


#line 22
struct GlobalParams_0
{
    CasteljauParams_0* params_0;
    RWStructuredBuffer<Vector<float, 3> > out_0;
};


#line 12178 "hlsl.meta.slang"
static Vector<float, 3>  lerp_0(Vector<float, 3>  x_0, Vector<float, 3>  y_0, Vector<float, 3>  s_0)
{

#line 12190
    return x_0 + (y_0 - x_0) * s_0;
}


#line 15 "../thirdparty/avbd/curve_casteljau.cpu.slang"
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_0)
{

#line 16
    Vector<float, 3>  ab_0 = lerp_0((slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->a_0, (slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->b_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    Vector<float, 3>  bc_0 = lerp_0((slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->b_0, (slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->c_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    Vector<float, 3>  cd_0 = lerp_0((slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->c_0, (slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->d_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    Vector<float, 3>  abc_0 = lerp_0(ab_0, bc_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    Vector<float, 3>  bcd_0 = lerp_0(bc_0, cd_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    Vector<float, 3>  split_0 = lerp_0(abc_0, bcd_0, (Vector<float, 3> )(slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->u_0);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[0U]) = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->a_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[1U]) = ab_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[2U]) = abc_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[3U]) = split_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[4U]) = split_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[5U]) = bcd_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[6U]) = cd_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->out_0)[7U]) = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->params_0->d_0;
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
} // namespace cassie_slang_curve_casteljau
