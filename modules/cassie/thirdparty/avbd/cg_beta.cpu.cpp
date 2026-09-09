#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_cg_beta {


#line 11 "../thirdparty/avbd/cg_beta.cpu.slang"
struct GlobalParams_0
{
    StructuredBuffer<float> dotNew_0;
    RWStructuredBuffer<float> dotOld_0;
    RWStructuredBuffer<float> betaOut_0;
};


#line 11
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 9
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_1)
{

#line 9
    KernelContext_0 kernelContext_0;

#line 9
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    float dnew_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotNew_0.Load(0U) + (slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotNew_0.Load(1U);
    float dold_0 = *(&((slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotOld_0)[0U]) + *(&((slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotOld_0)[1U]);

#line 11
    float b_0;
    if(dold_0 > 0.0f)
    {

#line 12
        b_0 = dnew_0 / dold_0;

#line 12
    }
    else
    {

#line 12
        b_0 = 0.0f;

#line 12
    }
    *(&((&kernelContext_0)->globalParams_0->betaOut_0)[0U]) = b_0;
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotOld_0)[0U]) = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotNew_0.Load(0U);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotOld_0)[1U]) = (slang_bit_cast<GlobalParams_0*>(globalParams_1))->dotNew_0.Load(1U);
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
} // namespace cassie_slang_cg_beta
