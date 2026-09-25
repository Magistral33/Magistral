#include    <compressor.h>

#include    <fstream>
#include    <iostream>
#include    <ktx.h>
#include    <thread>

#define     STB_IMAGE_IMPLEMENTATION
#include    <stb_image.h>
#define     STB_IMAGE_RESIZE_IMPLEMENTATION
#include    <stb_image_resize.h>

#include    <split-by-comma.h>

#include    <filesystem>
#include    <vulkan/vulkan.h>
#include    <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool compress_to_ktx2(const fs::path& src_img,
                      const fs::path& out_ktx,
                      const TextureSettings &cfg,
                      bool generate_mipmaps,
                      bool ignore)
{
    if (ignore)
    {
        if (fs::exists(out_ktx))
        {
            std::cout << "[INF] Texture " << out_ktx << " already exist. Skiped" << std::endl;
            return true;
        }
    }

    // Р—Р°РіСЂСѓР·РєР° РѕСЂРёРіРёРЅР°Р»СЊРЅРѕР№ С‚РµРєСЃС‚СѓСЂС‹
    int w = 0, h = 0, ch = 0;

    // Р’СЃРµРіРґР° Р±РµСЂРµРј 4 РєР°РЅР°Р»Р°, РµСЃР»Рё РЅРµС‚ Р°Р»СЊС„С‹, С‚Рѕ СЃРѕР·РґР°РµС‚СЃСЏ Р°Р»СЊС„Р° СЂР°РІРЅР°СЏ 255
    stbi_uc* data = stbi_load(src_img.string().c_str(), &w, &h, &ch, 4);

    if (!data)
    {
        std::cerr << "[ERR] stbi_load: " << stbi_failure_reason() << " | " << src_img << std::endl;
        return false;
    }

    // Р’С‹С‡РёСЃР»РµРЅРёРµ РєРѕР»РёС‡РµСЃС‚РІР° РјРёРї-СѓСЂРѕРІРЅРµР№
    auto calcMipLevels = [](uint32_t w, uint32_t h) -> uint32_t {
        return static_cast<uint32_t>(floor(log2(std::max(w, h))) + 1);
    };

    // Р—Р°РїРѕР»РЅСЏРµРј СЃС‚СЂСѓРєС‚СѓСЂСѓ, РЅРµСЃСѓС‰СѓСЋ РёРЅС„РѕСЂРјР°С†РёСЋ Рѕ Р±СѓРґСѓС‰РµР№ С‚РµРєСЃС‚СЂРµ
    ktxTextureCreateInfo ci = {};
    ci.vkFormat        = cfg.vkFormat; // С„РѕСЂРјР°С‚ РІС…РѕРґРЅС‹С… РґР°РЅРЅС‹С…
    ci.baseWidth       = static_cast<ktx_uint32_t>(w); // Р±Р°Р·РѕРІР°СЏ С€РёСЂРёРЅР°
    ci.baseHeight      = static_cast<ktx_uint32_t>(h); // Р±Р°Р·РѕРІР°СЏ РІС‹РѕСЃС‚Р°
    ci.baseDepth       = 1; // Р±Р°Р·РѕРІР°СЏ РіР»СѓР±РёРЅР°
    ci.numDimensions   = 2; // Р±Р°Р·РѕРІР°СЏ СЂР°Р·РјРµСЂРЅРѕСЃС‚СЊ (2D)
    ci.numLayers       = 1;
    ci.numFaces        = 1;
    ci.isArray         = KTX_FALSE;

    // Р§РёСЃР»Рѕ mipmap-СѓСЂРѕРІРЅРµР№ РѕРїСЂРµРґРµР»СЏРµРј РЇР’РќРћ, С‚Р°Рє РєР°Рє СЃР°РјРё РёС… СЃРіРµРЅРµСЂРёСЂСѓРµРј
    ci.numLevels       = generate_mipmaps ? calcMipLevels(w, h) : 1;
    // РќРµ РїРѕР»Р°РіР°РµРјСЃСЏ РЅР° Р°РїРїСЂР°С‚РЅСѓСЋ РіРµРЅРµСЂР°С†РёСЋ - СЌС‚Рѕ РґР»СЏ РЅРµСЃР¶Р°С‚С‹С… С‚РµРєСЃС‚СѓСЂ
    ci.generateMipmaps = KTX_FALSE;

    // РЎРѕР·РґР°РµРј С‚РµРєСЃС‚СѓСЂСѓ
    ktxTexture2* texture = nullptr;
    KTX_error_code result = ktxTexture2_Create(&ci,
                                               KTX_TEXTURE_CREATE_ALLOC_STORAGE,
                                               &texture);

    if (result != KTX_SUCCESS)
    {
        std::cerr << "[ERR] Failed to create ktxTexture2: " << result << std::endl;
        return false;
    }

    // Р—Р°РіСЂСѓР¶Р°РµРј Р±Р°Р·РѕРІС‹Р№ СѓСЂРѕРІРµРЅСЊ (0)
    auto* baseTex = ktxTexture(texture);
    ktxTexture_SetImageFromMemory(baseTex, 0, 0, 0, data, w * h * 4);

    // РњР°СЃСЃРёРІ РїРѕРґ СѓРєР°Р·Р°С‚РµР»Рё РЅР° РґР°РЅРЅС‹Рµ РІСЃРµС… СѓСЂРѕРІРЅРµР№, РЅРµ СѓРґР°Р»СЏРµРј СЌС‚Рё РґР°РЅРЅС‹Рµ РґРѕ СЃР¶Р°С‚РёСЏ
    // Рё СЃРѕС…СЂР°РЅРµРЅРёСЏ С‚РµРєСЃС‚СѓСЂС‹ РЅР° РґРёСЃРє!
    std::vector<stbi_uc *> levels_data;

    // Р•СЃР»Рё РЅСѓР¶РЅС‹ РјРёРїС‹ вЂ” РіРµРЅРµСЂРёСЂСѓРµРј РёС… РІСЂСѓС‡РЅСѓСЋ, РїРѕСЃР»РµРґРѕРІР°С‚РµР»СЊРЅРѕ СѓРјРµРЅР°С€Р°СЏ
    // СЂР·РјРµСЂС‹ С‚РµРєСЃС‚СЂСѓС‹ РІ РґРІР° СЂР°Р·Р° РЅР° РєР°Р¶РґРѕРј СѓСЂРѕРІРЅРµ
    if (generate_mipmaps && ci.numLevels > 1)
    {
        stbi_uc* prevLevelData = data;
        uint32_t prevW = w, prevH = h;

        for (uint32_t level = 1; level < ci.numLevels; ++level)
        {
            levels_data.push_back(prevLevelData);

            uint32_t curW = std::max(1u, prevW >> 1);
            uint32_t curH = std::max(1u, prevH >> 1);

            stbi_uc* resized = new stbi_uc[curW * curH * 4];

            // Р РµСЃРµРјРїР»РёРЅРі
            stbir_resize_uint8(
                prevLevelData, prevW, prevH, 0,
                resized, curW, curH, 0, 4
                );

            // Р—Р°РіСЂСѓР¶Р°РµРј СѓСЂРѕРІРµРЅСЊ РІ ktxTexture
            ktxTexture_SetImageFromMemory(
                baseTex, level, 0, 0,
                resized, curW * curH * 4
                );

            prevLevelData = resized;
            prevW = curW;
            prevH = curH;
        }
    }

    // РџРѕРґРіРѕС‚РѕРІРєР° РґР°РЅРЅС‹С… РґР»СЏ СЃР¶Р°С‚РёСЏ
    ktxBasisParams params = {};
    params.structSize = sizeof(ktxBasisParams);

    // РљР РРўРР§РќРћ: РћР±СЏР·Р°С‚РµР»СЊРЅРѕ СѓСЃС‚Р°РЅР°РІР»РёРІР°РµРј СѓСЂРѕРІРµРЅСЊ СЃР¶Р°С‚РёСЏ ETC1S
    // Р’Р°С€Рµ Р·РЅР°С‡РµРЅРёРµ 1 - РґРѕРїСѓСЃС‚РёРјРѕ, РЅРѕ РёСЃРїРѕР»СЊР·СѓР№С‚Рµ РєРѕРЅСЃС‚Р°РЅС‚Сѓ РґР»СЏ СЏСЃРЅРѕСЃС‚Рё
    params.compressionLevel = 2; // KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL = 2

    // --encode basis-lz

    // --qlevel (РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ 128)
    params.qualityLevel = cfg.qualityLevel;

    // РџРѕС‚РѕРєРё - РЇР’РќРћ СѓРєР°Р·С‹РІР°РµРј РєРѕР»РёС‡РµСЃС‚РІРѕ РЅР°С€РёС… СЏРґРµСЂ
    unsigned int numThreads = std::thread::hardware_concurrency();
    params.threadCount = (numThreads == 0) ? 1 : numThreads;

    // RDO-РїРѕСЂРѕРіРё - РІР°Р¶РЅС‹ РґР»СЏ normal maps!
    if (cfg.isNormalMap)
    {
        // Р”Р»СЏ normal map Р”РћР›Р–РќР« Р±С‹С‚СЊ РѕС‚РєР»СЋС‡РµРЅС‹ RDO РѕРїС‚РёРјРёР·Р°С†РёРё
        // РЎРѕРіР»Р°СЃРЅРѕ РґРѕРєСѓРјРµРЅС‚Р°С†РёРё: "Tunes codec parameters for better quality on normal maps
        // (no selector RDO, no endpoint RDO)"
        params.normalMap = KTX_TRUE;
        params.noEndpointRDO = KTX_TRUE;  // РћС‚РєР»СЋС‡Р°РµРј endpoint RDO
        params.noSelectorRDO = KTX_TRUE;  // РћС‚РєР»СЋС‡Р°РµРј selector RDO

        // Р”Р»СЏ normal maps РЅРµ РёСЃРїРѕР»СЊР·СѓРµРј preSwizzle - РѕРЅ РґР»СЏ РјРµС‚Р°РґР°РЅРЅС‹С… KTXswizzle
        // Р’ РІР°С€РµРј СЃР»СѓС‡Р°Рµ РЅРµС‚ С‚Р°РєРѕР№ РјРµС‚Р°РёРЅС„РѕСЂРјР°С†РёРё
        params.preSwizzle = KTX_FALSE;

        // РўР°РєР¶Рµ РЅРµ РёСЃРїРѕР»СЊР·СѓРµРј inputSwizzle, РµСЃР»Рё РЅРµ РЅСѓР¶РЅРѕ РјРµРЅСЏС‚СЊ РїРѕСЂСЏРґРѕРє РєР°РЅР°Р»РѕРІ
        params.inputSwizzle[0] = 'r'; // Р•СЃР»Рё РЅСѓР¶РЅРѕ РїРµСЂРµСЃС‚Р°РІРёС‚СЊ РєР°РЅР°Р»С‹
        params.inputSwizzle[1] = 'g';
        params.inputSwizzle[2] = 'b';
        params.inputSwizzle[3] = 'a';
    } else
    {
        params.normalMap = KTX_FALSE;
        params.endpointRDOThreshold = 1.25f;
        params.selectorRDOThreshold = 1.25f;
        params.noEndpointRDO = KTX_FALSE;
        params.noSelectorRDO = KTX_FALSE;
    }

    params.verbose = KTX_FALSE;
    params.noSSE = KTX_FALSE;

    result = ktxTexture2_CompressBasisEx(texture, &params);

    if (result != KTX_SUCCESS)
    {
        std::cerr << "[ERR] Failed to compress texture: " << result << std::endl;
        return false;
    }

    result = ktxTexture_WriteToNamedFile(ktxTexture(texture), out_ktx.string().c_str());

    if (result != KTX_SUCCESS)
    {
        std::cerr << "[ERR] Failed to write file: " << result << std::endl;
        return false;
    }

    // РЈРЅРёС‡С‚РѕР¶Р°РµРј С‚РµРєСЃС‚СѓСЂСѓ РІ РїР°РјСЏС‚Рё
    ktxTexture_Destroy(ktxTexture(texture));

    // РћСЃРІРѕР±РѕР¶РґР°РµРј РїР°РјСЏС‚СЊ РІСЃРµС… РјРёРїРјР°Рї-СѓСЂРѕРІРЅРµР№
    for (auto d : levels_data)
    {
        stbi_image_free(d);
    }

    // РЈС…РѕРґРёРј РґРѕРІРѕР»СЊРЅС‹Рµ, РєР°Рє СЃР»РѕРЅ, РїСЂРѕРґРµР»Р°РЅРЅРѕР№ СЂР°Р±РѕС‚РѕР№
    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
std::map<int, TextureSettings> parse_gltf_textures(const json &gltf)
{
    std::map<int, TextureSettings> image_settings;

    if (!gltf.contains("textures") || !gltf["textures"].is_array() ||
        !gltf.contains("materials") || !gltf["materials"].is_array())
    {
        return image_settings;
    }

    auto set_role = [&](int tex_idx, PBRTextureRole role, uint32_t quality, bool is_srgb, bool is_normal) {
        if (tex_idx < 0 || tex_idx >= gltf["textures"].size()) return;
        int img_idx = gltf["textures"][tex_idx].value("source", -1);
        if (img_idx < 0 || img_idx >= gltf["images"].size()) return;

        auto& settings = image_settings[img_idx];

        // РџСЂРёРѕСЂРёС‚РµС‚: Normal > BaseColor > РѕСЃС‚Р°Р»СЊРЅС‹Рµ. РќРµ РїРµСЂРµР·Р°РїРёСЃС‹РІР°РµРј, РµСЃР»Рё СЂРѕР»СЊ СѓР¶Рµ РѕРїСЂРµРґРµР»РµРЅР°.
        if (settings.role == PBRTextureRole::UNKNOWN || role == PBRTextureRole::NORMAL)
        {
            settings.role = role;
            settings.qualityLevel = quality;
            settings.isNormalMap = is_normal;
            settings.isSRGB = is_srgb;
            settings.vkFormat = is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            if (role == PBRTextureRole::OCCLUSION) settings.vkFormat = VK_FORMAT_R8_UNORM;
        }
    };

    for (const auto& mat : gltf["materials"])
    {
        if (mat.contains("pbrMetallicRoughness"))
        {
            auto& pbr = mat["pbrMetallicRoughness"];
            if (pbr.contains("baseColorTexture"))
                set_role(pbr["baseColorTexture"]["index"], PBRTextureRole::BASE_COLOR, 160, true, false);

            if (pbr.contains("metallicRoughnessTexture"))
                set_role(pbr["metallicRoughnessTexture"]["index"], PBRTextureRole::METALLIC_ROUGHNESS, 128, false, false);
        }

        if (mat.contains("normalTexture"))
            set_role(mat["normalTexture"]["index"], PBRTextureRole::NORMAL, 192, false, true);

        if (mat.contains("emissiveTexture"))
            set_role(mat["emissiveTexture"]["index"], PBRTextureRole::EMISSIVE, 128, true, false);

        if (mat.contains("occlusionTexture"))
            set_role(mat["occlusionTexture"]["index"], PBRTextureRole::OCCLUSION, 96, false, false);
    }

    return image_settings;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int compress_model_textures(const command_line_t &cmd_line)
{
    // РћС‚РєСЂС‹РІР°РµРј РјРѕРґРµР»СЊ РїРѕ СѓРєР°Р·Р°РЅРЅРѕРјСѓ РїСѓС‚Рё
    if (!cmd_line.model_path.is_present)
    {
        std::cerr << "[ERR] missing path to GLTF-file" << std::endl;
        return -1;
    }

    fs::path gltf_path = cmd_line.model_path.value;

    // Р’С‹С‚Р°РµРјСЃСЏ РѕС‚РєСЂС‹С‚СЊ РїРѕС‚РѕРє РІРІРѕРґР°
    std::ifstream ifs(gltf_path);

    if (!ifs.is_open())
    {
        std::cerr << "[ERR] can't open GLTF-file" << std::endl;
        return -1;
    }

    // РџС‹С‚Р°РµРјСЃСЏ СЂР°СЃРїР°СЂСЃРёС‚СЊ РјРѕРґРµР»СЊ
    json gltf;

    try
    {
        gltf = json::parse(ifs);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[ERR] JSON parse error: " << e.what() << std::endl;
        return -1;
    }

    fs::path base_dir = gltf_path.parent_path();

    int processed = 0;
    int failed = 0;

    auto img_settings = parse_gltf_textures(gltf);

    // Р¤РѕСЂРјРёСЂСѓРµРј СЃРїРёСЃРѕРє РїСЂРѕРїСѓСЃРєР°РµРјС‹С… С‚РµРєСЃС‚СЂСѓСЂ
    auto skiped_textures = split_by_comma_view(cmd_line.skip_textures.value);

    if (gltf.contains("images") && gltf["images"].is_array())
    {
        for (int img_idx = 0; img_idx < gltf["images"].size(); ++img_idx)
        {
            auto &img = gltf["images"][img_idx];

            if (!img.contains("uri"))
            {
                continue;
            }

            std::string uri = img["uri"];
            fs::path src_path = fs::weakly_canonical(base_dir / uri);

            if (!fs::exists(src_path))
            {
                std::cerr << "[ERR] Texture not found: " << src_path << std::endl;
                continue;
            }

            // РџСЂРѕРІРµСЂСЏРµРј, РЅРµ РЅР°РґРѕ Р»Рё РїСЂРѕРїСѓСЃС‚РёС‚СЊ СЌС‚Сѓ С‚РµРєСЃС‚СѓСЂСѓ
            bool is_skip_texture = false;

            for (auto skip_tex : skiped_textures)
            {
                if (skip_tex == src_path.filename().string())
                {
                    is_skip_texture = true;
                    break;
                }
            }

            // РџСЂРѕРїСѓСЃРєР°РµРј С‚РµРєСЃС‚СѓСЂСѓ РёР· СЃРїРёСЃРєР°
            if (is_skip_texture)
            {
                std::cout << "[INF] Skip texture " << src_path << std::endl;
                continue;
            }

            fs::path ktx_path = src_path;
            ktx_path.replace_extension(".ktx2");
            std::cout << "[INF] Converting: " << uri << " -> "
                      << fs::relative(ktx_path, base_dir).string()
                      << std::endl;

            auto it = img_settings.find(img_idx);
            const TextureSettings& cfg = (it != img_settings.end()) ? it->second : TextureSettings{};
            std::cout << " (Role: " << (cfg.role == PBRTextureRole::UNKNOWN ? "UNKNOWN" : "PBR")
                      << ", Format: " << (cfg.isSRGB ? "SRGB" : "UNORM") << ")\n";

            if (compress_to_ktx2(src_path, ktx_path, cfg, cmd_line.generate_mipmaps, cmd_line.ignore_existed))
            {
                img["uri"] = fs::relative(ktx_path, base_dir).string();
                img.erase("mimeType");

                if (img.contains("bufferView"))
                {
                    img.erase("bufferView");
                }

                // РЈРґР°Р»СЏРµРј РёСЃС…РѕРґРЅСѓСЋ С‚РµРєСЃС‚СѓСЂСѓ, РµСЃР»Рё СЌС‚Рѕ С‚СЂРµР±СѓРµС‚СЃСЏ
                if (cmd_line.delete_src)
                {
                    fs::remove(src_path);
                }

                processed++;
            }
            else
            {
                failed++;
            }
        }
    }

    fs::path out_gltf;

    if (cmd_line.overwrite_gltf)
        out_gltf = gltf_path;
    else
        out_gltf = gltf_path.parent_path() / (gltf_path.stem().string() + "_ktx.gltf");

    std::ofstream ofs(out_gltf);

    if (!ofs.is_open())
    {
        std::cerr << "[ERR] Cannot write output glTF\n";
        return -1;
    }

    ofs << gltf.dump(2);
    ofs.close();

    return failed > 0 ? -1 : 0;
}

