// Folder-input cases in the existing ML/RDO suite; fixed regression tests stay unchanged.
#if GACL_INCLUDE_CLER
#define ORT_API_MANUAL_INIT
#include "../ContentInput.h"
#include <gacl.h>
#include <DirectXTex.h>
#include <RDO_ML/ML_RDO.h>
#include "../../Lib/RDO_ML/LossMetrics.h"
#include <fstream>
#include <limits>
#include <cmath>

namespace
{
    struct Com
    {
        HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Com() { if (FAILED(status) && status != RPC_E_CHANGED_MODE) throw std::runtime_error("COM initialization failed"); }
        ~Com() { if (SUCCEEDED(status)) CoUninitialize(); }
    };

    DirectX::ScratchImage LoadPng(const std::filesystem::path& path)
    {
        unsigned char signature[8]{};
        const unsigned char expected[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        std::ifstream stream(path, std::ios::binary);
        if (!stream.read(reinterpret_cast<char*>(signature), sizeof(signature)) ||
            !std::equal(std::begin(signature), std::end(signature), std::begin(expected)))
            throw std::runtime_error("Invalid PNG signature");
        DirectX::ScratchImage decoded, converted;
        if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, nullptr, decoded)))
            throw std::runtime_error("Cannot decode PNG");
        const auto* image = decoded.GetImage(0, 0, 0);
        if (!image || !image->pixels || !image->width || !image->height) throw std::runtime_error("PNG has no pixels");
        if (image->format == DXGI_FORMAT_R8G8B8A8_UNORM) return decoded;
        if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
            DirectX::TEX_THRESHOLD_DEFAULT, converted))) throw std::runtime_error("Cannot convert PNG to RGBA8");
        return converted;
    }

    void CheckLoss(const DirectX::Image& image)
    {
        // Bounded chunks avoid large float sums; exercise the actual loss API on every pixel.
        std::vector<float> values, zeros;
        values.reserve(4096);
        for (size_t row = 0; row < image.height; ++row)
        {
            const auto* pixels = image.pixels + row * image.rowPitch;
            for (size_t column = 0; column < image.width * 4;)
            {
                const size_t count = (std::min)(size_t(4096), image.width * 4 - column);
                values.resize(count); zeros.assign(count, 0.0f);
                double expected = 0;
                for (size_t i = 0; i < count; ++i) { values[i] = pixels[column + i] / 255.0f; expected += values[i] * values[i]; }
                expected /= count;
                EXPECT_FLOAT_EQ(LossMetrics::CalculateLoss(values.data(), values.data(), count, LossMetrics::Metric::MSE), 0.0f);
                EXPECT_NEAR(LossMetrics::CalculateLoss(values.data(), zeros.data(), count, LossMetrics::Metric::MSE), expected, expected * 0.001 + 1e-6);
                EXPECT_NEAR(LossMetrics::CalculateLoss(values.data(), zeros.data(), count, LossMetrics::Metric::RMSE), std::sqrt(expected), 0.001);
                column += count;
            }
        }
    }

    std::filesystem::path Partner(const std::filesystem::path& dds)
    {
        auto png = dds; png.replace_extension(L".png");
        if (std::filesystem::is_regular_file(png)) return png;
        for (const auto& entry : std::filesystem::directory_iterator(dds.parent_path()))
            if (entry.is_regular_file() && ContentInput::Extension(entry.path()) == L".png" &&
                CompareStringOrdinal(entry.path().stem().c_str(), -1, dds.stem().c_str(), -1, TRUE) == CSTR_EQUAL) return entry.path();
        return {}; // A reference is optional for basic k-means validation.
    }

    void CheckCler(const DirectX::Image& first, const DirectX::TexMetadata& metadata,
        const DirectX::Image* reference)
    {
        // Each mode starts from the original BC1 blocks, never the other mode's result.
        std::vector<uint8_t> reduced(first.pixels, first.pixels + first.slicePitch);
        RDOOptions options;
        options.metric = RDOLossMetric::MSE; // No perceptual-model dependency or fallback.
        options.minClusters = 1; options.maxClusters = 8; options.iterations = 1;
        options.numThreads = 4;
        options.useClusterRDO = reference != nullptr;
        options.isGammaFormat = metadata.format == DXGI_FORMAT_BC1_UNORM_SRGB;
        const auto result = GACL_RDO_ComponentLevelEntropyReduce(8, reduced.data(), reference ? reference->pixels : nullptr,
            static_cast<uint32_t>(metadata.width), static_cast<uint32_t>(metadata.height), metadata.format, options);
        const auto expected = reference ? RDO_ErrorCode::OK : RDO_ErrorCode::OK_NoAdvancedRDO;
        testing::Test::RecordProperty(reference ? "reference_rdo_result" : "basic_cler_result", static_cast<int>(result));
        ASSERT_EQ(result, expected) << (reference ? "Reference-based RDO failed or fell back to basic clustering" : "Basic CLER failed");
        DirectX::Image encoded = first; encoded.pixels = reduced.data();
        DirectX::ScratchImage decoded;
        EXPECT_TRUE(SUCCEEDED(DirectX::Decompress(encoded, DXGI_FORMAT_R8G8B8A8_UNORM, decoded)))
            << "CLER result must remain decodable BC1";
    }
}

void RunMLContentInput(const ContentInput::File& file)
{
    Com com;
    const auto extension = ContentInput::Extension(file.path);
    if (extension == L".png")
    {
        auto image = LoadPng(file.path);
        CheckLoss(*image.GetImage(0, 0, 0));
        testing::Test::RecordProperty("content_validation", "PNG MSE/RMSE; no perceptual-model claim");
        return;
    }
    if (extension != L".dds") throw ContentInput::Unsupported("ML content mode supports PNG loss metrics and BC1 DDS CLER with an optional PNG reference");
    DirectX::TexMetadata metadata;
    DirectX::ScratchImage dds;
    ASSERT_TRUE(SUCCEEDED(DirectX::LoadFromDDSFile(file.path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, dds))) << "Malformed DDS";
    if (metadata.format != DXGI_FORMAT_BC1_UNORM && metadata.format != DXGI_FORMAT_BC1_UNORM_SRGB)
        throw ContentInput::Unsupported("CLER currently supports typed BC1 only");
    if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.depth != 1 || metadata.arraySize != 1 || metadata.IsCubemap())
        throw ContentInput::Unsupported("Folder CLER supports one 2D texture, not arrays/cubes/volumes");
    if (metadata.width < 24 || metadata.height < 24 || metadata.width % 4 || metadata.height % 4 ||
        metadata.width > (std::numeric_limits<uint32_t>::max)() / 4 / metadata.height)
        throw ContentInput::Unsupported("CLER requires complete blocks, at least 24x24, with RGBA byte size representable as uint32");
    const auto* first = dds.GetImage(0, 0, 0);
    ASSERT_NE(first, nullptr);
    size_t rowPitch = 0, slicePitch = 0;
    ASSERT_TRUE(SUCCEEDED(DirectX::ComputePitch(metadata.format, metadata.width, metadata.height, rowPitch, slicePitch)));
    ASSERT_EQ(first->rowPitch, rowPitch);
    ASSERT_EQ(first->slicePitch, slicePitch);
    testing::Test::RecordProperty("content_validation", "Basic CLER on top mip; optional reference-based MSE RDO; no perceptual-model or quality-threshold claim");
    ASSERT_NO_FATAL_FAILURE(CheckCler(*first, metadata, nullptr));

    const auto referencePath = Partner(file.path);
    if (referencePath.empty())
    {
        testing::Test::RecordProperty("reference_rdo", "not requested: no same-stem PNG");
        return;
    }
    testing::Test::RecordProperty("reference_file", ContentInput::Utf8(referencePath.filename().wstring()));
    testing::Test::RecordProperty("reference_rdo", "requested: MSE reference-based clustering; no basic fallback accepted");
    auto reference = LoadPng(referencePath);
    const auto* pixels = reference.GetImage(0, 0, 0);
    ASSERT_NE(pixels, nullptr);
    ASSERT_EQ(pixels->width, metadata.width) << "PNG/DDS width mismatch";
    ASSERT_EQ(pixels->height, metadata.height) << "PNG/DDS height mismatch";
    ASSERT_EQ(pixels->rowPitch, metadata.width * 4);
    ASSERT_NO_FATAL_FAILURE(CheckCler(*first, metadata, pixels));
}
#endif
