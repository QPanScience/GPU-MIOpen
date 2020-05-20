/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <miopen/conv/invokers/impl_gemm.hpp>
#include <miopen/solver.hpp>
#include <miopen/handle.hpp>
#include <miopen/generic_search.hpp>
#include "implicitgemm_util.hpp"

namespace miopen {
namespace solver {

static inline bool IsValidXdlopsGemm_v2(const ConvolutionContext& ctx,
                                        const int GemmMPerBlock,
                                        const int GemmNPerBlock,
                                        const int GemmMPerWave,
                                        const int GemmNPerWave,
                                        const int GemmKPack)
{
    if(ctx.IsFp16() && GemmKPack % 4 != 0)
        return false;

    if(ctx.IsBfp16() && GemmKPack % 2 != 0)
        return false;

    // unsupported xdlops-gemm
    if(GemmMPerWave == 16 && GemmNPerWave == 32)
        return false;
    if(GemmMPerWave == 32 && GemmNPerWave == 16)
        return false;
    if(GemmMPerWave == 8 && GemmNPerWave != 64)
        return false;
    if(GemmMPerWave == 4 && GemmNPerWave != 64)
        return false;
    if(GemmMPerWave == 32 && GemmNPerWave == 32 && GemmKPack % 2 != 0)
        return false;
    if(GemmMPerWave == 16 && GemmNPerWave == 16 && GemmKPack % 4 != 0)
        return false;

    const auto WaveSize = 64;
    const auto BlockSize =
        (GemmNPerBlock * GemmMPerBlock) / (GemmMPerWave * GemmNPerWave) * WaveSize;

    if(BlockSize < 64 || BlockSize > 256)
        return false;

    return (GemmMPerBlock % GemmMPerWave) == 0 && (GemmNPerBlock % GemmNPerWave) == 0;
}

PerformanceImplicitGemmForwardV4R4Xdlops::PerformanceImplicitGemmForwardV4R4Xdlops(bool spare)
{
    // always search full space, no matter if use_spare_set or not
    GemmMPerBlock = 32;
    GemmNPerBlock = 32;
    GemmKPerBlock = 4;

    GemmMPerWave = 16;
    GemmNPerWave = 16;

    GemmKSegment = 1;
    GemmKPack    = 1;

    use_spare_set = spare;
}

PerformanceImplicitGemmForwardV4R4Xdlops::PerformanceImplicitGemmForwardV4R4Xdlops(
    int GemmMPerBlock_,
    int GemmNPerBlock_,
    int GemmKPerBlock_,
    int GemmMPerWave_,
    int GemmNPerWave_,
    int GemmKSegment_,
    int GemmKPack_,
    bool use_spare_set_)
    : GemmMPerBlock(GemmMPerBlock_),
      GemmNPerBlock(GemmNPerBlock_),
      GemmKPerBlock(GemmKPerBlock_),
      GemmMPerWave(GemmMPerWave_),
      GemmNPerWave(GemmNPerWave_),
      GemmKSegment(GemmKSegment_),
      GemmKPack(GemmKPack_),
      use_spare_set(use_spare_set_)
{
}

bool PerformanceImplicitGemmForwardV4R4Xdlops::
operator==(const PerformanceImplicitGemmForwardV4R4Xdlops& other) const
{
    // clang-format off
    return GemmMPerBlock == other.GemmMPerBlock
        && GemmNPerBlock == other.GemmNPerBlock
        && GemmKPerBlock == other.GemmKPerBlock
        && GemmMPerWave == other.GemmMPerWave
        && GemmNPerWave == other.GemmNPerWave
        && GemmKSegment == other.GemmKSegment
        && GemmKPack == other.GemmKPack 
        && use_spare_set == other.use_spare_set;
    // clang-format on
}

bool PerformanceImplicitGemmForwardV4R4Xdlops::IsValidValue() const
{
    // clang-format off
    return IsTwoPower<32,128>(GemmMPerBlock)
        && IsTwoPower<32,128>(GemmNPerBlock)
        && IsTwoPower<4,32>(GemmKPerBlock)
        && IsTwoPower<16,64>(GemmMPerWave)
        && IsTwoPower<16,64>(GemmNPerWave)
        && IsTwoPower<1,1>(GemmKSegment)
        && IsTwoPower<1,8>(GemmKPack);
    // clang-format on
}

bool PerformanceImplicitGemmForwardV4R4Xdlops::SetNextValue()
{
    do
    {
        if(!NextTwoPower<32, 128>(GemmMPerBlock))
            break;
        if(!NextTwoPower<32, 128>(GemmNPerBlock))
            break;
        if(!NextTwoPower<4, 32>(GemmKPerBlock))
            break;
        if(!NextTwoPower<16, 64>(GemmMPerWave))
            break;
        if(!NextTwoPower<16, 64>(GemmNPerWave))
            break;
        if(!NextTwoPower<1, 1>(GemmKSegment))
            break;
        if(!NextTwoPower<1, 8>(GemmKPack))
            break;
        return false;
    } while(false);

    return true;
}

void PerformanceImplicitGemmForwardV4R4Xdlops::EuristicInit(const ConvolutionContext& ctx)
{
    PerformanceImplicitGemmForwardV4R4Xdlops tmp;
    if(ctx.IsFp32())
    {
        tmp = {128, 128, 4, 64, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {128, 128, 8, 64, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {32, 64, 4, 64, 32, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {32, 32, 4, 32, 32, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 16, 4, 16, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {16, 64, 4, 64, 16, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {16, 16, 4, 16, 16, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 4, 16, 4, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 8, 8, 8, 64, 1, 2};
    }
    else if(ctx.IsFp16())
    {
        tmp = {128, 128, 4, 64, 64, 1, 8};
        if(!tmp.IsValid(ctx))
            tmp = {128, 128, 8, 64, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {32, 64, 4, 64, 32, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {32, 32, 4, 32, 32, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {64, 16, 4, 16, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {16, 64, 4, 64, 16, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {16, 16, 4, 16, 16, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {64, 4, 16, 4, 64, 1, 4};
        if(!tmp.IsValid(ctx))
            tmp = {64, 8, 8, 8, 64, 1, 4};
    }
    else if(ctx.IsBfp16())
    {
        tmp = {128, 128, 16, 64, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 32, 4, 32, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {32, 64, 4, 64, 32, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {32, 32, 4, 32, 32, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 16, 4, 16, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {16, 64, 4, 64, 16, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {16, 16, 4, 16, 16, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 4, 16, 4, 64, 1, 2};
        if(!tmp.IsValid(ctx))
            tmp = {64, 8, 8, 8, 64, 1, 2};
    }
    else
    {
        MIOPEN_LOG_E("Only fp16, and bfp16 are supported");
        assert(false);
    }

    if(!tmp.IsValid(ctx))
    {
        MIOPEN_LOG_E("All attempts failed");
        assert(false);
    }
    *this = tmp;
    MIOPEN_LOG_I(ToString());
}

std::string PerformanceImplicitGemmForwardV4R4Xdlops::ToString() const
{
    std::ostringstream ss;
    Serialize(ss);
    return ss.str();
}

std::tuple<int, int, int>
PerformanceImplicitGemmForwardV4R4Xdlops::CalculateGemmSize(const ConvolutionContext& ctx) const
{
    const auto n  = ConvolutionContextInterpreter::GetBatchN(ctx);
    const auto k  = ConvolutionContextInterpreter::GetOutputChannelK(ctx);
    const auto c  = ConvolutionContextInterpreter::GetInputChannelC(ctx);
    const auto ho = ConvolutionContextInterpreter::GetOutputHeightHo(ctx);
    const auto wo = ConvolutionContextInterpreter::GetOutputWidthWo(ctx);
    const auto y  = ConvolutionContextInterpreter::GetFilterHeightY(ctx);
    const auto x  = ConvolutionContextInterpreter::GetFilterWidthX(ctx);

    const auto gemm_m       = k;
    const auto gemm_n       = n * ho * wo;
    const auto gemm_k_total = c * y * x;

    return std::make_tuple(gemm_m, gemm_n, gemm_k_total);
}

std::tuple<int, bool> PerformanceImplicitGemmForwardV4R4Xdlops::CalculateBlockSize() const
{
    int block_size = 0;

    try
    {
        if(!(GemmMPerBlock % GemmMPerWave == 0 && GemmNPerBlock % GemmNPerWave == 0))
            MIOPEN_THROW("invalid performance parameter");

        const auto WaveSize = 64;
        block_size = (GemmNPerBlock * GemmMPerBlock) / (GemmMPerWave * GemmNPerWave) * WaveSize;
    }
    catch(...)
    {
        return std::make_tuple(-1, false);
    }

    return std::make_tuple(block_size, true);
}

std::tuple<int, bool>
PerformanceImplicitGemmForwardV4R4Xdlops::CalculateGridSize(const ConvolutionContext& ctx) const
{
    int GridSize = 0;

    try
    {
        int gemm_m = 0;
        int gemm_n = 0;

        std::tie(gemm_m, gemm_n, std::ignore) = CalculateGemmSize(ctx);

        if(!(gemm_m % GemmMPerBlock == 0 && gemm_n % GemmNPerBlock == 0))
            MIOPEN_THROW("invalid performance parameter");

        GridSize = (gemm_m / GemmMPerBlock) * (gemm_n / GemmNPerBlock);
    }
    catch(...)
    {
        return std::make_tuple(-1, false);
    }

    return std::make_tuple(GridSize, true);
}

std::tuple<int, int, int, int, int, bool>
PerformanceImplicitGemmForwardV4R4Xdlops::CalculateGemmABlockCopyPerformanceParameters(
    const ConvolutionContext& ctx) const
{
    // A tensor shape [GemmKSeqment, GemmK, GemmM, GemmKPack]

    int ClusterLengths_GemmK     = 0;
    int ClusterLengths_GemmM     = 0;
    int ClusterLengths_GemmKPack = 0;
    int SrcDataPerRead_GemmKPack = ctx.IsFp32() ? amd_buffer_load_max_length<float>()
                                                : amd_buffer_load_max_length<half_float::half>();
    int DstDataPerWrite_GemmKPack = ctx.IsFp32() ? amd_lds_write_max_length<float>()
                                                 : amd_lds_write_max_length<half_float::half>();

    try
    {
        bool valid = false;

        int block_size = -1;

        std::tie(block_size, valid) = CalculateBlockSize();

        if(!valid)
            MIOPEN_THROW("invalid performance parameter");

        if(!((GemmKPerBlock * GemmMPerBlock * GemmKPack) % block_size == 0))
            MIOPEN_THROW("invalid performance parameter");

        // GemmKPack is src vector read dimension
        SrcDataPerRead_GemmKPack = gcd(SrcDataPerRead_GemmKPack, GemmKPack);

        // calculate threadwise copy size
        const auto a_data_per_thread_copy =
            (GemmKPerBlock * GemmMPerBlock * GemmKPack) / block_size;

        // SrcDataPerRead bounded by size of threadwise copy
        SrcDataPerRead_GemmKPack = gcd(SrcDataPerRead_GemmKPack, a_data_per_thread_copy);

        const auto a_data_per_thread_copy_gemmkpack = SrcDataPerRead_GemmKPack;
        const auto tmp = a_data_per_thread_copy / a_data_per_thread_copy_gemmkpack;
        const auto a_data_per_thread_copy_gemmk = gcd(GemmKPerBlock, tmp);
        const auto a_data_per_thread_copy_gemmm = tmp / a_data_per_thread_copy_gemmk;

        // vector write into LDS
        DstDataPerWrite_GemmKPack =
            gcd(DstDataPerWrite_GemmKPack, a_data_per_thread_copy_gemmkpack);

        if(!(GemmKPerBlock % a_data_per_thread_copy_gemmk == 0 &&
             GemmMPerBlock % a_data_per_thread_copy_gemmm == 0 &&
             GemmKPack % a_data_per_thread_copy_gemmkpack == 0))
            MIOPEN_THROW("invalid performance parameter");

        ClusterLengths_GemmK     = GemmKPerBlock / a_data_per_thread_copy_gemmk;
        ClusterLengths_GemmM     = GemmMPerBlock / a_data_per_thread_copy_gemmm;
        ClusterLengths_GemmKPack = GemmKPack / a_data_per_thread_copy_gemmkpack;
    }
    catch(...)
    {
        return std::make_tuple(-1, -1, -1, -1, -1, false);
    }

    return std::make_tuple(ClusterLengths_GemmK,
                           ClusterLengths_GemmM,
                           ClusterLengths_GemmKPack,
                           SrcDataPerRead_GemmKPack,
                           DstDataPerWrite_GemmKPack,
                           true);
}

std::tuple<int, int, int, int, int, bool>
PerformanceImplicitGemmForwardV4R4Xdlops::CalculateGemmBBlockCopyPerformanceParameters(
    const ConvolutionContext& ctx) const
{
    // B tensor shape [GemmKSeqment, GemmK, GemmN, GemmKPack]

    int ClusterLengths_GemmK     = 0;
    int ClusterLengths_GemmN     = 0;
    int ClusterLengths_GemmKPack = 0;
    int SrcDataPerRead_GemmN     = ctx.IsFp32() ? amd_buffer_load_max_length<float>()
                                            : amd_buffer_load_max_length<half_float::half>();
    int DstDataPerWrite_GemmKPack = ctx.IsFp32() ? amd_lds_write_max_length<float>()
                                                 : amd_lds_write_max_length<half_float::half>();

    try
    {
        bool valid = false;

        int block_size = -1;

        std::tie(block_size, valid) = CalculateBlockSize();

        if(!valid)
            MIOPEN_THROW("invalid performance parameter");

        if(!((GemmKPerBlock * GemmNPerBlock * GemmKPack) % block_size == 0))
            MIOPEN_THROW("invalid performance parameter");

        // GemmN is src vector read dimension
        // calculate vector length on gemmn dimension based on global tensor layout
        const auto y  = ConvolutionContextInterpreter::GetFilterHeightY(ctx);
        const auto x  = ConvolutionContextInterpreter::GetFilterWidthX(ctx);
        const auto hi = ConvolutionContextInterpreter::GetInputHeightHi(ctx);
        const auto wi = ConvolutionContextInterpreter::GetInputWidthWi(ctx);
        const auto conv_stride_h =
            ConvolutionContextInterpreter::GetAdjustedConvolutionStrideH(ctx);
        const auto conv_stride_w =
            ConvolutionContextInterpreter::GetAdjustedConvolutionStrideW(ctx);
        const auto conv_dilation_w =
            ConvolutionContextInterpreter::GetAdjustedConvolutionDilationW(ctx);
        const auto in_left_pad_h  = ConvolutionContextInterpreter::GetInputLeftPadH(ctx);
        const auto in_left_pad_w  = ConvolutionContextInterpreter::GetInputLeftPadW(ctx);
        const auto in_right_pad_h = ConvolutionContextInterpreter::GetAdjustedInputRightPadH(ctx);
        const auto in_right_pad_w = ConvolutionContextInterpreter::GetAdjustedInputRightPadW(ctx);

        if(y == 1 && x == 1 && conv_stride_h == 1 && conv_stride_w == 1 && in_left_pad_h == 0 &&
           in_left_pad_w == 0 && in_right_pad_h == 0 && in_right_pad_w == 0)
        {
            // \todo there are more configs that can go through this if branch
            SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, hi * wi);
        }
        else if(conv_stride_w == 1)
        {
            SrcDataPerRead_GemmN =
                gcd(SrcDataPerRead_GemmN, in_left_pad_w, wi, in_right_pad_w, conv_dilation_w);
        }
        else
        {
            SrcDataPerRead_GemmN = 1;
        }

        // calculate threadwise copy size
        const auto a_data_per_thread_copy =
            (GemmKPerBlock * GemmNPerBlock * GemmKPack) / block_size;

        // SrcDataPerRead_GemmN bounded by size of threadwise copy
        SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, a_data_per_thread_copy);

        // SrcDataPerRead also bounded by GemmKPack;
        SrcDataPerRead_GemmN = gcd(SrcDataPerRead_GemmN, GemmNPerBlock);

        const auto a_data_per_thread_copy_gemmn = SrcDataPerRead_GemmN;
        const auto tmp = a_data_per_thread_copy / a_data_per_thread_copy_gemmn;
        const auto a_data_per_thread_copy_gemmkpack = gcd(GemmKPack, tmp);
        const auto a_data_per_thread_copy_gemmk     = tmp / a_data_per_thread_copy_gemmkpack;

        // vector write into LDS
        DstDataPerWrite_GemmKPack =
            gcd(DstDataPerWrite_GemmKPack, a_data_per_thread_copy_gemmkpack);

        if(!(GemmKPerBlock % a_data_per_thread_copy_gemmk == 0 &&
             GemmNPerBlock % a_data_per_thread_copy_gemmn == 0 &&
             GemmKPack % a_data_per_thread_copy_gemmkpack == 0))
            MIOPEN_THROW("invalid performance parameter");

        ClusterLengths_GemmK     = GemmKPerBlock / a_data_per_thread_copy_gemmk;
        ClusterLengths_GemmN     = GemmNPerBlock / a_data_per_thread_copy_gemmn;
        ClusterLengths_GemmKPack = GemmKPack / a_data_per_thread_copy_gemmkpack;
    }
    catch(...)
    {
        return std::make_tuple(-1, -1, -1, -1, -1, false);
    }

    return std::make_tuple(ClusterLengths_GemmK,
                           ClusterLengths_GemmN,
                           ClusterLengths_GemmKPack,
                           SrcDataPerRead_GemmN,
                           DstDataPerWrite_GemmKPack,
                           true);
}

std::tuple<std::size_t, bool> PerformanceImplicitGemmForwardV4R4Xdlops::CalculateLdsNumberOfByte(
    const ConvolutionContext& ctx) const
{
    const auto a_block_space = GemmKPerBlock * GemmMPerBlock * GemmKPack;
    const auto b_block_space = GemmKPerBlock * GemmMPerBlock * GemmKPack;

    std::size_t lds_size = 2 * (a_block_space + b_block_space) *
                           (ctx.IsFp32() ? sizeof(float) : sizeof(half_float::half));

    return std::make_tuple(lds_size, true);
}

bool PerformanceImplicitGemmForwardV4R4Xdlops::IsValid(const ConvolutionContext& ctx) const
{
    if(!IsValidValue())
        return false;

    if(!IsValidXdlopsGemm_v2(
           ctx, GemmMPerBlock, GemmNPerBlock, GemmMPerWave, GemmNPerWave, GemmKPack))
        return false;

    bool valid = false;

    // check blockwise GEMM size
    const auto n  = ConvolutionContextInterpreter::GetBatchN(ctx);
    const auto k  = ConvolutionContextInterpreter::GetOutputChannelK(ctx);
    const auto c  = ConvolutionContextInterpreter::GetInputChannelC(ctx);
    const auto ho = ConvolutionContextInterpreter::GetOutputHeightHo(ctx);
    const auto wo = ConvolutionContextInterpreter::GetOutputWidthWo(ctx);
    const auto y  = ConvolutionContextInterpreter::GetFilterHeightY(ctx);
    const auto x  = ConvolutionContextInterpreter::GetFilterWidthX(ctx);

    const auto gemm_m       = k;
    const auto gemm_n       = static_cast<std::size_t>(n) * ho * wo;
    const auto gemm_k_total = static_cast<std::size_t>(c) * y * x;

    if(gemm_k_total % (GemmKSegment * GemmKPack) != 0)
        return false;

    const auto gemm_k = gemm_k_total / (GemmKSegment * GemmKPack);

    if(!(gemm_m % GemmMPerBlock == 0 && gemm_n % GemmNPerBlock == 0 && gemm_k % GemmKPerBlock == 0))
        return false;

    // check blockwise copy of A matrix
    std::tie(std::ignore, std::ignore, std::ignore, std::ignore, std::ignore, valid) =
        CalculateGemmABlockCopyPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check blockwise copy of B matrix
    std::tie(std::ignore, std::ignore, std::ignore, std::ignore, std::ignore, valid) =
        CalculateGemmBBlockCopyPerformanceParameters(ctx);

    if(!valid)
        return false;

    // check LDS allocation
    std::size_t lds_size = 0;
    std::tie(lds_size, valid) = CalculateLdsNumberOfByte(ctx);

    return (valid and lds_size <= get_lds_max_number_of_byte());
}

PerformanceImplicitGemmForwardV4R4Xdlops
ConvHipImplicitGemmForwardV4R4Xdlops::GetPerformanceConfig(const ConvolutionContext& ctx) const
{
    return GetPerformanceConfigBase<PerformanceImplicitGemmForwardV4R4Xdlops>(ctx);
}

ConvSolution ConvHipImplicitGemmForwardV4R4Xdlops::GetSolution(
    const ConvolutionContext& ctx,
    const PerformanceImplicitGemmForwardV4R4Xdlops& config,
    bool) const
{
    ConvSolution result;
    KernelInfo construction_parameters;

    assert(config.IsValid(ctx));

    construction_parameters.kernel_file =
        "gridwise_convolution_forward_implicit_gemm_v4r4_xdlops_nchw_kcyx_nkhw.cpp";

    construction_parameters.kernel_name =
        "gridwise_convolution_forward_implicit_gemm_v4r4_xdlops_nchw_kcyx_nkhw";

    int grid_size  = 0;
    int block_size = 0;

    std::tie(grid_size, std::ignore)  = config.CalculateGridSize(ctx);
    std::tie(block_size, std::ignore) = config.CalculateBlockSize();

    construction_parameters.l_wk.push_back(block_size);
    construction_parameters.l_wk.push_back(1);
    construction_parameters.l_wk.push_back(1);

    construction_parameters.g_wk.push_back(block_size * grid_size);
    construction_parameters.g_wk.push_back(1);
    construction_parameters.g_wk.push_back(1);

    int GemmABlockCopyClusterLengths_GemmK      = -1;
    int GemmABlockCopyClusterLengths_GemmM      = -1;
    int GemmABlockCopyClusterLengths_GemmKPack  = -1;
    int GemmABlockCopySrcDataPerRead_GemmKPack  = -1;
    int GemmABlockCopyDstDataPerWrite_GemmKPack = -1;

    int GemmBBlockCopyClusterLengths_GemmK      = -1;
    int GemmBBlockCopyClusterLengths_GemmN      = -1;
    int GemmBBlockCopyClusterLengths_GemmKPack  = -1;
    int GemmBBlockCopySrcDataPerRead_GemmN      = -1;
    int GemmBBlockCopyDstDataPerWrite_GemmKPack = -1;

    std::tie(GemmABlockCopyClusterLengths_GemmK,
             GemmABlockCopyClusterLengths_GemmM,
             GemmABlockCopyClusterLengths_GemmKPack,
             GemmABlockCopySrcDataPerRead_GemmKPack,
             GemmABlockCopyDstDataPerWrite_GemmKPack,
             std::ignore) = config.CalculateGemmABlockCopyPerformanceParameters(ctx);

    std::tie(GemmBBlockCopyClusterLengths_GemmK,
             GemmBBlockCopyClusterLengths_GemmN,
             GemmBBlockCopyClusterLengths_GemmKPack,
             GemmBBlockCopySrcDataPerRead_GemmN,
             GemmBBlockCopyDstDataPerWrite_GemmKPack,
             std::ignore) = config.CalculateGemmBBlockCopyPerformanceParameters(ctx);

    // clang-format off
    construction_parameters.comp_options =
        std::string(" -std=c++14 ") +
        std::string(" -DCK_PARAM_PROBLEM_N=") + std::to_string(ConvolutionContextInterpreter::GetBatchN(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_K=") + std::to_string(ConvolutionContextInterpreter::GetOutputChannelK(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_C=") + std::to_string(ConvolutionContextInterpreter::GetInputChannelC(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_HI=") + std::to_string(ConvolutionContextInterpreter::GetInputHeightHi(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_WI=") + std::to_string(ConvolutionContextInterpreter::GetInputWidthWi(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_HO=") + std::to_string(ConvolutionContextInterpreter::GetOutputHeightHo(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_WO=") + std::to_string(ConvolutionContextInterpreter::GetOutputWidthWo(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_Y=") + std::to_string(ConvolutionContextInterpreter::GetFilterHeightY(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_X=") + std::to_string(ConvolutionContextInterpreter::GetFilterWidthX(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_STRIDE_H=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedConvolutionStrideH(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_STRIDE_W=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedConvolutionStrideW(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DILATION_H=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedConvolutionDilationH(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DILATION_W=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedConvolutionDilationW(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_LEFT_PAD_H=") + std::to_string(ConvolutionContextInterpreter::GetInputLeftPadH(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_LEFT_PAD_W=") + std::to_string(ConvolutionContextInterpreter::GetInputLeftPadW(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_RIGHT_PAD_H=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedInputRightPadH(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_IN_RIGHT_PAD_W=") + std::to_string(ConvolutionContextInterpreter::GetAdjustedInputRightPadW(ctx)) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_FORWARD=") + std::to_string(1) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_BACKWARD_DATA=") + std::to_string(0) +
        std::string(" -DCK_PARAM_PROBLEM_CONV_DIRECTION_BACKWARD_WEIGHT=") + std::to_string(0) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_PER_BLOCK=") + std::to_string(config.GemmNPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_PER_BLOCK=") + std::to_string(config.GemmMPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_K_PER_BLOCK=") + std::to_string(config.GemmKPerBlock) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_M_PER_WAVE=") + std::to_string(config.GemmMPerWave) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_N_PER_WAVE=") + std::to_string(config.GemmNPerWave) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_KSEGMENT=") + std::to_string(config.GemmKSegment) +
        std::string(" -DCK_PARAM_TUNABLE_GEMM_KPACK=") + std::to_string(config.GemmKPack) +
        std::string(" -DCK_PARAM_DEPENDENT_BLOCK_SIZE=") + std::to_string(block_size) +
        std::string(" -DCK_PARAM_DEPENDENT_GRID_SIZE=") + std::to_string(grid_size) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_A_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_K=") + std::to_string(GemmABlockCopyClusterLengths_GemmK) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_A_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_M=") + std::to_string(GemmABlockCopyClusterLengths_GemmM) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_A_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_KPACK=") + std::to_string(GemmABlockCopyClusterLengths_GemmKPack) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_A_BLOCK_COPY_SRC_DATA_PER_READ_GEMM_KPACK=") + std::to_string(GemmABlockCopySrcDataPerRead_GemmKPack) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_A_BLOCK_COPY_DST_DATA_PER_WRITE_GEMM_KPACK=") + std::to_string(GemmABlockCopyDstDataPerWrite_GemmKPack) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_B_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_K=") + std::to_string(GemmBBlockCopyClusterLengths_GemmK) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_B_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_N=") + std::to_string(GemmBBlockCopyClusterLengths_GemmN) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_B_BLOCK_COPY_CLUSTER_LENGTHS_GEMM_KPACK=") + std::to_string(GemmBBlockCopyClusterLengths_GemmKPack) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_B_BLOCK_COPY_SRC_DATA_PER_READ_GEMM_N=") + std::to_string(GemmBBlockCopySrcDataPerRead_GemmN) +
        std::string(" -DCK_PARAM_DEPENDENT_GEMM_B_BLOCK_COPY_DST_DATA_PER_WRITE_GEMM_KPACK=") + std::to_string(GemmBBlockCopyDstDataPerWrite_GemmKPack) +
        std::string(" -DCK_USE_AMD_XDLOPS=") + std::to_string(IsXdlopsSupport(ctx) ? 1 : 0) +
        std::string(" -DCK_USE_AMD_XDLOPS_INLINE_ASM=") + std::to_string(miopen::IsEnabled(MIOPEN_DEBUG_IMPLICIT_GEMM_XDLOPS_INLINE_ASM{}) ? 1 : 0) +
        std::string(" -DCK_USE_AMD_XDLOPS_EMULATE=") + (miopen::IsEnabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_XDLOPS_EMULATE{}) ? '1' : '0') +
        std::string(" -DCK_USE_AMD_BUFFER_ADDRESSING=") + (miopen::IsEnabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_USE_AMD_BUFFER_ADDRESSING{}) ? '1' : '0') +
        std::string(" -DCK_USE_AMD_BUFFER_ADDRESSING_INTRINSIC=") + (miopen::IsEnabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_USE_AMD_BUFFER_ADDRESSING_INTRINSIC{}) ? '1' : '0') +
        ctx.general_compile_options;
    // clang-format on

    result.invoker_factory = conv::MakeImplGemmDataInvokerFactory(ctx);

    result.construction_params.push_back(construction_parameters);
    return result;
}

int ConvHipImplicitGemmForwardV4R4Xdlops::RunAndMeasureSolution(miopen::Handle& profile_h,
                                                                ConstData_t bot_buf,
                                                                Data_t top_buf,
                                                                ConstData_t wei_buf,
                                                                ConstData_t bias_buf,
                                                                const ConvolutionContext& ctx,
                                                                const ConvSolution& solution,
                                                                float& elapsed_time) const
{
    assert(bias_buf == nullptr);
    (void)bias_buf;

    return RunAndMeasureSolutionBase(
        profile_h, bot_buf, top_buf, wei_buf, ctx, solution, elapsed_time);
}

bool ConvHipImplicitGemmForwardV4R4Xdlops::IsApplicable(const ConvolutionContext& ctx) const
{
    if(!(ctx.IsFp32() || ctx.IsFp16() || ctx.IsBfp16()))
        return false;

    if(!ctx.direction.IsForward())
        return false;

    if(!ctx.Is2d())
        return false;

    if(ctx.group_counts > 1)
        return false;

    return IsApplicableXdlops(ctx);
}

bool ConvHipImplicitGemmForwardV4R4Xdlops::IsValidPerformanceConfig(
    const ConvolutionContext& ctx, const PerformanceImplicitGemmForwardV4R4Xdlops& c) const
{
    MIOPEN_LOG_I("");
    return c.IsValidValue() && c.IsValid(ctx);
}

PerformanceImplicitGemmForwardV4R4Xdlops
ConvHipImplicitGemmForwardV4R4Xdlops::Search(const ConvolutionContext& ctx) const
{
    return GenericSearchFwd(*this, ctx);
}

} // namespace solver
} // namespace miopen