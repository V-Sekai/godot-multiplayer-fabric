#include "../slang-prelude/slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#undef SLANG_PRELUDE_EXPORT
#define SLANG_PRELUDE_EXPORT

namespace cassie_slang_attachment_dual_update {


#line 19 "../thirdparty/avbd/attachment_dual_update.cpu.slang"
struct GlobalParams_0
{
    StructuredBuffer<Vector<float, 3> > positions_0;
    StructuredBuffer<uint32_t> vertIdx_0;
    StructuredBuffer<Vector<float, 3> > fixedPos_0;
    StructuredBuffer<float> gamma_0;
    RWStructuredBuffer<Vector<float, 3> > lambda_0;
};


#line 13
void _main_0(void* _S1, void* entryPointParams_0, void* globalParams_0)
{

#line 13
    ComputeThreadVaryingInput * _S2 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S1));
    uint32_t c_0 = (_S2->groupID * Vector<uint32_t, 3> (64U, 1U, 1U) + _S2->groupThreadID).x;

    Vector<float, 3>  p_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->positions_0.Load((slang_bit_cast<GlobalParams_0*>(globalParams_0))->vertIdx_0.Load(c_0));
    Vector<float, 3>  fp_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->fixedPos_0.Load(c_0);
    float g_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_0))->gamma_0.Load(c_0);
    Vector<float, 3>  * _S3 = (&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->lambda_0)[c_0]);
    *(&((slang_bit_cast<GlobalParams_0*>(globalParams_0))->lambda_0)[c_0]) = Vector<float, 3> ((*_S3).x + g_0 * (p_0.x - fp_0.x), (*_S3).y + g_0 * (p_0.y - fp_0.y), (*_S3).z + g_0 * (p_0.z - fp_0.z));
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
} // namespace cassie_slang_attachment_dual_update
