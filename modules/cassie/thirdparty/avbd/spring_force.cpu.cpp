#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_spring_force {


#line 35 "../thirdparty/avbd/spring_force.cpu.slang"
struct GlobalParams_0
{
    StructuredBuffer<Vector<float, 3> > positions_0;
    StructuredBuffer<uint32_t> p1Idx_0;
    StructuredBuffer<uint32_t> p2Idx_0;
    StructuredBuffer<float> restLen_0;
    StructuredBuffer<float> stiffness_0;
    RWStructuredBuffer<Vector<float, 3> > gradA_0;
    RWStructuredBuffer<float> hess_0;
};


#line 17
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_0)
{

#line 17
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));
    uint32_t i_0 = (_S2->groupID * Vector<uint32_t, 3> (64U, 1U, 1U) + _S2->groupThreadID).x;


    Vector<float, 3>  p1_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->positions_0.Load((slang_bit_cast<GlobalParams_0*>(globalParams_0))->p1Idx_0.Load(i_0));
    Vector<float, 3>  p2_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->positions_0.Load((slang_bit_cast<GlobalParams_0*>(globalParams_0))->p2Idx_0.Load(i_0));
    float dx_0 = p1_0.x - p2_0.x;
    float dy_0 = p1_0.y - p2_0.y;
    float dz_0 = p1_0.z - p2_0.z;
    float _S3 = dx_0 * dx_0;

#line 26
    float _S4 = dy_0 * dy_0;

#line 26
    float _S5 = dz_0 * dz_0;

#line 26
    float len2_0 = _S3 + _S4 + _S5;
    float len_0 = (F32_sqrt((len2_0)));
    float k_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->stiffness_0.Load(i_0);


    float gScale_0 = k_0 * (len_0 - (slang_bit_cast<GlobalParams_0*>(globalParams_0))->restLen_0.Load(i_0)) / len_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->gradA_0)[i_0]) = Vector<float, 3> (gScale_0 * dx_0, gScale_0 * dy_0, gScale_0 * dz_0);
    float h_0 = k_0 / len2_0;
    uint32_t h0_0 = 6U * i_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0]) = h_0 * _S3;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0 + 1U]) = h_0 * (dx_0 * dy_0);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0 + 2U]) = h_0 * (dx_0 * dz_0);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0 + 3U]) = h_0 * _S4;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0 + 4U]) = h_0 * (dy_0 * dz_0);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->hess_0)[h0_0 + 5U]) = h_0 * _S5;
    return;
}

// [numthreads(64, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _main_0(varyingInput, entryPointParams, globalParams);
}
// [numthreads(64, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t x = 0; x < 64; ++x)
    {
        threadInput.groupThreadID.x = x;
        _main_0(&threadInput, entryPointParams, globalParams);
    }
}
// [numthreads(64, 1, 1)]
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
} // namespace cassie_slang_spring_force
