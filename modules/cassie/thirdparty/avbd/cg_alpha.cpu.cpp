#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_alpha {


#line 13 "../thirdparty/avbd/cg_alpha.cpu.slang"
struct GlobalParams_0
{
    StructuredBuffer<float> dotPQ_0;
    StructuredBuffer<float> dotDelta_0;
    RWStructuredBuffer<float> alphaOut_0;
};


#line 9
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_0)
{

    float a_0 = ((slang_bit_cast<GlobalParams_0*>(globalParams_0))->dotDelta_0.Load(0U) + (slang_bit_cast<GlobalParams_0*>(globalParams_0))->dotDelta_0.Load(1U)) / ((slang_bit_cast<GlobalParams_0*>(globalParams_0))->dotPQ_0.Load(0U) + (slang_bit_cast<GlobalParams_0*>(globalParams_0))->dotPQ_0.Load(1U));
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->alphaOut_0)[0U]) = a_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->alphaOut_0)[1U]) = 0.0f - a_0;
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
} // namespace cassie_slang_cg_alpha
