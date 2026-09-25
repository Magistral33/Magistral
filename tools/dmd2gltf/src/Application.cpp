#include    <Application.h>
#include    <Geometry.h>
#include    <filesystem-utils.h>
#include    <Logger.h>

#include    <algorithm>
#include    <chrono>
#include    <cmath>
#include    <cstdint>
#include    <filesystem>
#include    <fstream>
#include    <iostream>
#include    <map>
#include    <mutex>
#include    <set>
#include    <string>
#include    <thread>
#include    <utility>
#include    <vector>

#include    <vulkan/vulkan.h>
#include    <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include    <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include    <stb_image_resize.h>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static bool is_slash(char ch)
{
    return ch == '/' || ch == '\\';
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::parse_args(int argc, char* argv[])
{
    cli::Parser parser(argc, argv);

    configure_parser(parser);

    parse_command_line(parser, cmd_line);

    return set_convert_mode(cmd_line, convert_mode);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::convert()
{
    if (convert_mode == CONVERT_ROUTE)
    {
        return convert_route(cmd_line.input_route_path.value,
                             cmd_line.output_route_path.value,
                             cmd_line.input_only_used_at_map.value,
                             cmd_line.input_lights_at_map.value,
                             cmd_line.input_compress_textures.value,
                             cmd_line.num_threads.value);
    }

    if (convert_mode == CONVERT_MODEL)
    {
        return convert_model(cmd_line.input_model_path.value,
                             cmd_line.input_texture_path.value,
                             cmd_line.output_model_path.value,
                             cmd_line.input_compress_textures.value,
                             cmd_line.smooth.value);
    }

    return false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::convert_route(std::string &in_dmd_route_path,
                                std::string &out_gltf_route_path,
                                bool only_used_at_map,
                                bool lights_at_map,
                                bool compress_texture,
                                int num_threads)
{
    // РџСЂРµРѕР±СЂР°Р·СѓРµРј РїСѓС‚Рё Рє РїР»Р°С‚С„РѕСЂРјРѕСЃРїРµС†РёС„РёС‡РЅРѕРјСѓ РІРёРґСѓ
    path_to_native_separator(in_dmd_route_path);
    path_to_native_separator(out_gltf_route_path);

    using Label = std::string;
    using RelativeModelPath = std::string;
    using RelativeTexturePath = std::string;
    using LabelTextureSmooth = std::tuple<Label, RelativeTexturePath, RelativeTexturePath, bool>;
    std::string line_buffer;

    // РЎРїРёСЃРѕРє РѕР±СЉРµРєС‚РѕРІ, РёСЃРїРѕР»СЊР·СѓРµРјС‹С… РІ .map РјР°СЂС€СЂСѓС‚Р°
    std::set<Label> map_objects;

    if (only_used_at_map)
    {
        // РС‰РµРј СЂР°СЃРїРѕР»РѕР¶РµРЅРёРµ РѕР±СЉРµРєС‚РѕРІ РјР°СЂС€СЂСѓС‚Р° - С„Р°Р№Р» route1.map
        std::ifstream route1_map = std::ifstream();

        // РЎРЅР°С‡Р°Р»Р° РїСЂРѕР±СѓРµРј РЅР°Р№С‚Рё РєРѕРЅРІРµСЂС‚РёСЂРѕРІР°РЅРЅС‹Р№ .map
        std::string map_path = combine_path(out_gltf_route_path, "topology");
        map_path = combine_path(map_path, "map");
        map_path = combine_path(map_path, "route1.map");
        route1_map.open(map_path, std::ios::in);
        if (route1_map.is_open())
        {
            LOG_INFO("Info: opened converted route1.map: %s", map_path.c_str());
        }
        else
        {
            LOG_WARN("Warn: failed to open converted route1.map: %s", map_path.c_str());

            // РџСЂРѕР±СѓРµРј РЅР°Р№С‚Рё РѕСЂРёРіРёРЅР°Р»СЊРЅС‹Р№ .map
            map_path = combine_path(in_dmd_route_path, "route1.map");
            route1_map.open(map_path, std::ios::in);
            if (route1_map.is_open())
            {
                LOG_INFO("Info: opened ZDSimulator's route1.map: %s", map_path.c_str());
            }
            else
            {
                LOG_WARN("Warn: failed to open ZDSimulator's route1.map: %s", map_path.c_str());
                return false;
            }
        }

        while (std::getline(route1_map, line_buffer))
        {
            // РџСѓСЃС‚РѕРµ РЅР°Р·РІР°РЅРёРµ РѕР±СЉРµРєС‚Р°
            if (line_buffer.empty() || (*(line_buffer.begin()) == ',') )
            {
                continue;
            }
            // РЎС‚СЂРѕРєР° СЃ РѕР±СЉРµРєС‚РѕРј РґРѕР»Р¶РЅР° Р·Р°РєР°РЅС‡РёРІР°С‚СЊСЃСЏ С‚РѕС‡РєРѕР№ СЃ Р·Р°РїСЏС‚РѕР№
            if (*(line_buffer.end() - 1) != ';')
            {
                continue;
            }
            // РЎС‚СЂРѕРєР° СЃ РѕР±СЉРµРєС‚РѕРј РґРѕР»Р¶РЅР° СЃРѕРґРµСЂР¶Р°С‚СЊ С€РµСЃС‚СЊ Р·Р°РїСЏС‚С‹С… - СЂР°Р·РґРµР»РёС‚РµР»РµР№
            if (std::count(line_buffer.begin(), line_buffer.end(), ',') != 6)
            {
                continue;
            }

            // Р§РёС‚Р°РµРј РїРµСЂРІС‹Р№ СЌР»РµРјРµРЅС‚ РІ СЃС‚СЂРѕРєРµ - СЃРѕРєСЂР°С‰С‘РЅРЅРѕРµ РЅР°Р·РІР°РЅРёРµ РѕР±СЉРµРєС‚Р°
            std::string label = "";
            std::istringstream ss(line_buffer);
            if (std::getline(ss, label, ','))
            {
                map_objects.insert(label);
            }
        }

        if (map_objects.empty())
        {
            LOG_WARN("Warn: failed to find any objects in route1.map");
            route1_map.close();
            return false;
        }

        route1_map.close();
    }

    // Р§РёС‚Р°РµРј СЃРїРёСЃРѕРє РѕР±СЉРµРєС‚РѕРІ РёР· Р±Р°Р·С‹ РјР°СЂС€СЂСѓС‚Р°
    std::string ref_path = combine_path(in_dmd_route_path, "objects.ref");
    std::ifstream objects_ref(ref_path, std::ios::in);
    if (!objects_ref.is_open())
    {
        LOG_WARN("Warn: failed to open ZDSimulator's objects.ref: %s", ref_path.c_str());
        return false;
    }
    LOG_INFO("Info: opened ZDSimulator's objects.ref: %s", ref_path.c_str());

    // РЎРїРёСЃРѕРє РјРѕРґРµР»РµР№, Рё СЃРїРёСЃРєРё СЃРѕРєСЂР°С‰С‘РЅРЅС‹С… РёРјС‘РЅ Рё С‚РµРєСЃС‚СѓСЂ Рє СЌС‚РёРј РјРѕРґРµР»СЏРј
    struct mutexed_objects
    {
    public:
        std::map<RelativeModelPath, std::vector<LabelTextureSmooth>> objects;
        bool get_next(RelativeModelPath& _relative_model_path, std::vector<LabelTextureSmooth>& _labels_textures_smooth)
        {
            std::scoped_lock lock(mutex);
            if (objects.empty())
            {
                return false;
            }
            try
            {
                const auto it = objects.erase(objects.begin());
                _relative_model_path = it->first;
                _labels_textures_smooth = it->second;
                return true;
            }
            catch (...)
            {
                LOG_WARN("Warn: exception while erase objects.begin(): size = %u", objects.size());
                return false;
            }
        }
    private:
        std::mutex mutex;
    } objects;

    std::map<RelativeTexturePath, int> out_textures_and_path_count;
    std::map<RelativeTexturePath, RelativeTexturePath> textures_path_and_out_path;
    std::set<Label> unique_refs;
    bool light_found = false;
    bool smooth = false;

    while (std::getline(objects_ref, line_buffer))
    {
        std::istringstream ss(line_buffer);
        if (!ss)
        {
            continue;
        }

        // РСЃС…РѕРґРЅР°СЏ РёРЅС„РѕСЂРјР°С†РёСЏ - СЃРѕРєСЂР°С‰С‘РЅРЅРѕРµ РёРјСЏ, РїСѓС‚СЊ Рє РјРѕРґРµР»Рё, РїСѓС‚СЊ Рє С‚РµРєСЃС‚СѓСЂРµ
        std::string label, relative_dmd_model_path, relative_texture_path, out_relative_texture_path;
        ss >> label >> relative_dmd_model_path >> relative_texture_path;
        if (is_slash(label.front())
            || relative_dmd_model_path.empty()
            || !is_slash(relative_dmd_model_path.front())
            || relative_texture_path.empty()
            || !is_slash(relative_texture_path.front()))
        {
            if (label == "[smooth]")
            {
                smooth = true;
                LOG_INFO("Info: smoothing models enabled");
            }
            else if (label == "[not_smooth]")
            {
                smooth = false;
                LOG_INFO("Info: smoothing models disabled");
            }
            continue;
        }

        // РџСЂРѕРІРµСЂСЏРµРј С‡С‚Рѕ СЃРѕРєСЂР°С‰С‘РЅРЅРѕРµ РёРјСЏ СѓРЅРёРєР°Р»СЊРЅРѕ
        if (unique_refs.find(label) == unique_refs.end())
        {
            unique_refs.insert(label);
        }
        else
        {
            LOG_WARN("Warn: ref \"%s\":", line_buffer.c_str());
            LOG_WARN("      name \"%s\" is written already. This ref will be ignored", label.c_str());
            continue;
        }

        // Р•СЃР»Рё РїСЂРѕС‡С‚С‘РЅ С„Р°Р№Р» .map - РїСЂРѕРІРµСЂСЏРµРј С‡С‚Рѕ СЃРѕРєСЂР°С‰С‘РЅРЅРѕРµ РёРјСЏ РёСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ
        if (only_used_at_map && (map_objects.find(label) == map_objects.end()))
        {
            LOG_WARN("Warn: ref \"%s\":", line_buffer.c_str());
            LOG_WARN("      name \"%s\" does not used at map. Model will not be converted", label.c_str());
            continue;
        }

        // Р—Р°РїРѕРјРёРЅР°РµРј, С‡С‚Рѕ РЅР°С€Р»Рё РјРѕРґРµР»СЊ, РЅР° РєРѕС‚РѕСЂСѓСЋ ZDS РІРµС€Р°РµС‚ РёСЃС‚РѕС‡РЅРёРє СЃРІРµС‚Р°
        if (label == "light")
        {
            light_found = true;
            continue;
        }

        std::replace(relative_dmd_model_path.begin(), relative_dmd_model_path.end(), '\\', '/');
        std::replace(relative_texture_path.begin(), relative_texture_path.end(), '\\', '/');

        auto texture_path_it = textures_path_and_out_path.find(relative_texture_path);
        if (texture_path_it == textures_path_and_out_path.end())
        {
            // РџСѓС‚СЊ Рє С‚РµРєСЃС‚СѓСЂРµ
            std::string in_texture_path =
                in_dmd_route_path + relative_texture_path;
            path_to_native_separator(in_texture_path);

            std::ifstream texture(in_texture_path, std::ios::in);
            if (!texture.is_open())
            {
                LOG_WARN("Warn: failed to find and open texture file: %s", in_texture_path.c_str());
                LOG_WARN("      model with name \"%s\" will not be converted", label.c_str());
                continue;
            }
            texture.close();

            fs::path texture_path = relative_texture_path;
            const std::string texture_filename = texture_path.filename().stem().string();
            const std::string texture_ext = texture_path.filename().extension().string();
            out_relative_texture_path = "textures/";
            out_relative_texture_path += texture_filename;

            auto out_path_it = out_textures_and_path_count.find(out_relative_texture_path);
            if (out_path_it == out_textures_and_path_count.end())
            {
                out_textures_and_path_count.insert({out_relative_texture_path, 1});
            }
            else
            {
                out_path_it->second += 1;
                int add_number = out_path_it->second;

                while (true)
                {
                    const std::string out_path = out_relative_texture_path + "_" + std::to_string(add_number);
                    if (out_textures_and_path_count.find(out_path) == out_textures_and_path_count.end())
                    {
                        out_relative_texture_path = out_path;
                        out_textures_and_path_count.insert({out_relative_texture_path, 1});
                        break;
                    }

                    ++add_number;
                }
            }

            if (compress_texture)
            {
                out_relative_texture_path += ".ktx2";
            }
            else
            {
                out_relative_texture_path += texture_ext;
            }

            textures_path_and_out_path.insert({relative_texture_path, out_relative_texture_path});
        }
        else
        {
            out_relative_texture_path = texture_path_it->second;
        }

        auto it = objects.objects.find(relative_dmd_model_path);
        if (it == objects.objects.end())
        {
            // Р”РѕР±Р°РІР»СЏРµРј С„Р°Р№Р» РјРѕРґРµР»Рё, РµС‘ СЃРѕРєСЂР°С‰С‘РЅРЅРѕРµ РЅР°РёРјРµРЅРѕРІР°РЅРёРµ, С‚РµРєСЃС‚СѓСЂСѓ Рё СЃРіР»Р°Р¶РёРІР°РЅРёРµ
            objects.objects.insert({ relative_dmd_model_path, {{label, relative_texture_path, out_relative_texture_path, smooth}} });
        }
        else
        {
            // Рљ СѓР¶Рµ РґРѕР±Р°РІР»РµРЅРѕР№ РјРѕРґРµР»Рё РµС‰С‘ РІР°СЂРёР°РЅС‚ РЅР°РёРјРµРЅРѕРІР°РЅРёСЏ, С‚РµРєСЃС‚СѓСЂС‹ Рё СЃРіР»Р°Р¶РёРІР°РЅРёСЏ
            it->second.push_back({label, relative_texture_path, out_relative_texture_path, smooth});
            it->second.shrink_to_fit();
        }
    }

    out_textures_and_path_count.clear();
    textures_path_and_out_path.clear();
    objects_ref.close();

    if (objects.objects.empty())
    {
        LOG_WARN("Warn: failed to find any objects in objects.ref");
        return false;
    }

    // РЎРѕР·РґР°РµРј РєР°С‚Р°Р»РѕРі РїРѕРґ РЅРѕРІС‹Р№ РјР°СЂС€СЂСѓС‚
    fs::create_directories(out_gltf_route_path);
    // РЎРѕР·РґР°РµРј РєР°С‚Р°Р»РѕРі РїРѕРґ С‚РµРєСЃС‚СѓСЂС‹
    fs::create_directory(combine_path(out_gltf_route_path, "textures"));

    // РќРѕРІС‹Р№ СЃРїРёСЃРѕРє СЃСЃС‹Р»РѕРє РЅР° С„Р°Р№Р»С‹ РјРѕРґРµР»РµР№
    struct mutexed_map_for_new_objects
    {
    public:
        std::map<Label, RelativeModelPath> objects_label_and_path;
        void add_object(const Label& label, const RelativeModelPath& path)
        {
            std::lock_guard lock(mutex);
            objects_label_and_path.insert({label, path});
        }
    private:
        std::mutex mutex;
    } new_objects;

    // Р”РѕР±Р°РІР»СЏРµРј РјРѕРґРµР»СЊ СЃ РёСЃС‚РѕС‡РЅРёРєРѕРј СЃРІРµС‚Р° РІРјРµСЃС‚Рѕ РєРѕСЃС‚С‹Р»СЊРЅРѕР№ РјРѕРґРµР»Рё РґР»СЏ ZDS
    if (lights_at_map && light_found)
    {
        new_objects.add_object("light", "/../../data/models/default-objects/light.gltf");
    }

    // РЎРїРёСЃРѕРє СЃРєРѕРїРёСЂРѕРІР°РЅРЅС‹С…/СЃР¶Р°С‚С‹С… С‚РµРєСЃС‚СѓСЂ
    struct mutexed_map_for_ready_textures
    {
    public:
        bool texture_not_ready_or_get_alpha_blending(const RelativeTexturePath& output_texture_path, bool& is_alpha_blending)
        {
            std::lock_guard lock(mutex);
            auto it = output_texture_path_and_alpha_blending.find(output_texture_path);
            if (it == output_texture_path_and_alpha_blending.end())
            {
                return true;
            }

            is_alpha_blending = it->second;
            return false;
        }

        void add_texture_ready(const RelativeTexturePath& output_texture_path, bool& is_alpha_blending)
        {
            std::lock_guard lock(mutex);
            output_texture_path_and_alpha_blending.insert({output_texture_path, is_alpha_blending});
        }
    private:
        std::map<RelativeTexturePath, bool> output_texture_path_and_alpha_blending;
        std::mutex mutex;
    } ready_textures;

    struct mutexed_models_count
    {
    public:
        uint32_t models_total;
        void increase()
        {
            std::lock_guard lock(mutex);
            ++models_count;

            // Р§РµСЂРµР· std::cerr РІС‹РґР°С‘Рј РїСЂРѕРіСЂРµСЃСЃ РєРѕРЅРІРµСЂС‚Р°С†РёРё РІ GUI
            const auto cur_time = std::chrono::steady_clock::now();
            if (std::chrono::duration<double> diff = cur_time - prev_time; diff.count() > 0.5)
            {
                std::cerr << " (" << models_count << "/" << models_total << ")";
                prev_time = cur_time;
            }
        }
    private:
        std::chrono::time_point<std::chrono::steady_clock> prev_time = std::chrono::steady_clock::now();
        uint32_t models_count = 0;
        std::mutex mutex;
    } models_count;
    models_count.models_total = objects.objects.size();

    // Р§РµСЂРµР· std::cerr РІС‹РґР°С‘Рј РІ GUI РїСЂРѕРіСЂРµСЃСЃ РєРѕРЅРІРµСЂС‚Р°С†РёРё
    std::cerr << " (" << 0 << "/" << models_count.models_total << ")";

    auto convert = [&](mutexed_objects& _objects,
                       mutexed_map_for_new_objects& _new_objects,
                       mutexed_map_for_ready_textures& _ready_textures,
                       mutexed_models_count& _models_count,
                       const std::string& _in_dmd_route_path,
                       const std::string& _out_gltf_route_path,
                       bool _compress_textures)
    {
        RelativeModelPath _relative_model_path;
        std::vector<LabelTextureSmooth> _labels_textures_smooth;
        while (_objects.get_next(_relative_model_path, _labels_textures_smooth))
        {
            fs::path model_path = _relative_model_path;

            // РџСѓС‚СЊ Рє РёСЃС…РѕРґРЅРѕР№ РјРѕРґРµР»Рё
            std::string in_dmd_model_path =
                _in_dmd_route_path + _relative_model_path;
            path_to_native_separator(in_dmd_model_path);

            // РџСѓС‚СЊ Рє РїР°РїРєРµ СЃ РЅРѕРІС‹Рј С„Р°Р№Р»РѕРј РјРѕРґРµР»Рё
            std::string out_gltf_model_dir =
                _out_gltf_route_path + model_path.parent_path().string();

            // РћС‚РЅРѕСЃРёС‚РµР»СЊРЅС‹Р№ РїСѓС‚СЊ Рє С„Р°Р№Р»Сѓ СЃ РёРЅС„РѕСЂРјР°С†РёРµР№ Рѕ РјРѕРґРµР»Рё РІ С„РѕСЂРјР°С‚Рµ bin
            fs::create_directories(_out_gltf_route_path + model_path.parent_path().string() + "/bin");
            std::string out_relative_bin_path = "bin/"s + model_path.stem().string() + ".bin";

            // РЎРѕР·РґР°С‘Рј РјРѕРґРµР»Рё РґР»СЏ РІСЃРµС… РІР°СЂРёР°РЅС‚РѕРІ С‚РµРєСЃС‚СѓСЂ
            bool add_texture_name = (_labels_textures_smooth.size() > 1);
            for (const auto& [label, relative_texture_path, out_relative_texture, smooth] : _labels_textures_smooth)
            {
                fs::path texture_path = _in_dmd_route_path + relative_texture_path;

                // Р§РёС‚Р°РµРј С„Р°Р№Р» РјРѕРґРµР»Рё
                Geometry model_data;
                std::string texture_ext = texture_path.extension().string();
                model_data.is_reversed_texture_coord = (texture_ext != ".tga");
                model_data.is_blend_material = ((texture_ext == ".tga") || (texture_ext == ".png"));
                if (!model_data.get_dmd_model_data(in_dmd_model_path, smooth))
                {
                    LOG_WARN("      model with name \"%s\" will not be converted", label.c_str());
                    continue;
                }

                // РџСѓС‚СЊ Рє РЅРѕРІРѕРјСѓ С„Р°Р№Р»Сѓ РјРѕРґРµР»Рё
                std::string out_gltf_model_name = model_path.stem().string();
                if (add_texture_name)
                {
                    out_gltf_model_name += '_' + texture_path.stem().string();
                }
                model_data.model_file_name = out_gltf_model_name;

                std::string out_texture_path = out_gltf_route_path + '/' + out_relative_texture;
                const int slash_count = std::count(_relative_model_path.begin(), _relative_model_path.end(), '/');
                std::string out_relative_texture_path;
                for (int i = 1; i < slash_count; ++i)
                {
                    out_relative_texture_path += "../";
                }
                out_relative_texture_path += out_relative_texture;

                if (_ready_textures.texture_not_ready_or_get_alpha_blending(out_texture_path, model_data.is_blend_material))
                {
                    if (!std::filesystem::exists(out_texture_path))
                    {
                        try
                        {
                            if (_compress_textures)
                            {
                                compress_to_ktx2(texture_path.string(), out_texture_path, model_data.is_blend_material);
                            }
                            else
                            {
                                std::filesystem::copy(texture_path, out_texture_path);
                            }
                        }
                        catch (std::exception &e)
                        {
                            LOG_INFO("Info: %s", e.what());
                        }
                    }
                    _ready_textures.add_texture_ready(out_texture_path, model_data.is_blend_material);
                }

                float change_vertices_Z = 0.0f;
#if 0
// РџРѕ РёС‚РѕРіР°Рј РѕР±С‰РµРЅРёСЏ СЃ РјР°СЂС€СЂСѓС‚РѕСЃС‚СЂРѕРёС‚РµР»СЏРјРё,
// РІС‹СЂР°РІРЅРёРІР°РЅРёРµ РјРµС€РµР№ СЃ СЂРµР»СЊСЃР°РјРё РїРѕРґ РЈР“Р  РЅР° РґР°РЅРЅС‹Р№ РјРѕРјРµРЅС‚ РЅРµ РЅСѓР¶РЅРѕ
                if (   (label.find("track") != label.npos)
                    || (label.find("Track") != label.npos))
                {
                    // РџРѕСЃРєРѕР»СЊРєСѓ СѓСЂРѕРІРµРЅСЊ РіРѕР»РѕРІРєРё СЂРµР»СЊСЃР° РІ ZDS РјР°СЂС€СЂСѓС‚Р°С… РЅР°С…РѕРґРёС‚СЃСЏ РЅРµ РІ РЅСѓР»Рµ,
                    // Р° РїРѕРґРЅСЏС‚ РІ СЃСЂРµРґРЅРµРј РЅР° 0.3114, СЃРјРµС‰Р°РµРј РјРµС€ Сѓ РјРѕРґРµР»РµР№ СЂРµР»СЊСЃ (СЃ "track" РІ РёРјРµРЅРё РѕР±СЉРµРєС‚Р°)
                    change_vertices_Z = -0.3114f;
                }
#endif
                if (generate_gltf_model(model_data,
                                        out_gltf_model_dir,
                                        out_relative_bin_path,
                                        out_relative_texture_path,
                                        _compress_textures,
                                        change_vertices_Z))
                {
                    // Р—Р°РїРёСЃС‹РІР°РµРј РЅРѕРІС‹Рµ РјРѕРґРµР»Рё
                    _new_objects.add_object(label, model_path.parent_path().string() + '/' + out_gltf_model_name + ".gltf");
                }
            }

            // Р§РµСЂРµР· std::cerr РІС‹РґР°С‘Рј РІ GUI РїСЂРѕРіСЂРµСЃСЃ РєРѕРЅРІРµСЂС‚Р°С†РёРё
            _models_count.increase();
        }
    };

    std::vector<std::thread> threads;
    int possible_threads = static_cast<int>(std::thread::hardware_concurrency()) - 2;
    for (size_t i = 0; i < (std::max(1, std::min(num_threads, possible_threads))); ++i)
    {
        threads.emplace_back(convert, std::ref(objects), std::ref(new_objects),
                             std::ref(ready_textures), std::ref(models_count),
                             std::ref(in_dmd_route_path), std::ref(out_gltf_route_path),
                             std::ref(compress_texture));
    }
    convert(objects, new_objects, ready_textures, models_count,
            in_dmd_route_path, out_gltf_route_path,
            compress_texture);
/*
    for (const auto& [relative_model_path, labels_textures_smooth] : objects)
    {
        convert(new_objects, ready_textures, models_count,
                in_dmd_route_path, out_gltf_route_path,
                relative_model_path, labels_textures_smooth,
                compress_texture);
    }*/
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::ofstream new_objects_ref(combine_path(out_gltf_route_path, "objects.ref"), std::ios::out);
    if (!new_objects_ref.is_open())
    {
        LOG_WARN("Warn: failed to create new objects.ref: %s", combine_path(out_gltf_route_path, "objects.ref").c_str());
        return false;
    }

    for (const auto& [label, path] : new_objects.objects_label_and_path)
    {
        new_objects_ref << label << "\t" << path << '\n';
    }

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::convert_model(std::string &in_dmd_model_path,
                                std::string &in_texture_path,
                                std::string &out_gltf_model_path,
                                bool smooth,
                                bool compress_texture,
                                std::string out_relative_bin_path,
                                std::string out_relative_texture_path)
{
    Geometry model_data;

    std::ifstream texture(in_texture_path, std::ios::in);
    if (!texture.is_open())
    {
        LOG_WARN("Warn: failed to open texture file: %s", in_texture_path.c_str());
        return false;
    }
    texture.close();

    std::string texture_ext = fs::path(in_texture_path).extension().string();
    model_data.is_reversed_texture_coord = (texture_ext != ".tga");
    model_data.is_blend_material = ((texture_ext == ".tga") || (texture_ext == ".png"));
    if (compress_texture)
    {
        texture_ext = ".ktx2";
    }

    if (!model_data.get_dmd_model_data(in_dmd_model_path, smooth))
    {
        return false;
    }

    fs::path model_path = out_gltf_model_path;
    std::string gltf_directory_path = model_path.parent_path().string();
    fs::create_directories(gltf_directory_path);

    // РћС‚РЅРѕСЃРёС‚РµР»СЊРЅС‹Р№ РїСѓС‚СЊ Рє С„Р°Р№Р»Сѓ СЃ РёРЅС„РѕСЂРјР°С†РёРµР№ Рѕ РјРѕРґРµР»Рё РІ С„РѕСЂРјР°С‚Рµ bin
    if (out_relative_bin_path.empty())
    {
        out_relative_bin_path = model_path.stem().string() + ".bin";
    }
    fs::path bin_path = out_relative_bin_path;
    fs::create_directories(bin_path);
    if (bin_path.extension() != ".bin")
    {
        out_relative_bin_path = out_relative_bin_path + ".bin";
    }

    // РћС‚РЅРѕСЃРёС‚РµР»СЊРЅС‹Р№ РїСѓС‚СЊ Рє С‚РµРєСЃС‚СѓСЂРµ
    if (out_relative_texture_path.empty())
    {
        if (compress_texture)
        {
            out_relative_texture_path = fs::path(in_texture_path).filename().stem().string() + ".ktx2";
        }
        else
        {
            out_relative_texture_path = fs::path(in_texture_path).filename().string();
        }
    }
    fs::path texture_path = out_relative_texture_path;
    fs::create_directories(texture_path);
    if (texture_path.extension() != texture_ext)
    {
        out_relative_texture_path = out_relative_texture_path + texture_ext;
    }

    std::string out_texture_path = gltf_directory_path + '/' + out_relative_texture_path;
    if (!std::filesystem::exists(out_texture_path))
    {
        try
        {
            if (compress_texture)
            {
                compress_to_ktx2(in_texture_path, out_texture_path, model_data.is_blend_material);
            }
            else
            {
                std::filesystem::copy(in_texture_path, out_texture_path);
            }
        }
        catch (std::exception &e)
        {
            LOG_INFO("Info: %s", e.what());
        }
    }

    return generate_gltf_model(model_data,
                               gltf_directory_path,
                               out_relative_bin_path,
                               out_relative_texture_path,
                               compress_texture,
                               0.0f);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::generate_gltf_model(Geometry& model_data,
                                      const std::string &gltf_directory_path,
                                      const std::string &out_relative_bin_path,
                                      const std::string &out_relative_texture_path,
                                      bool compress_texture,
                                      float change_vertices_Z)
{
    for (auto& vertex : model_data.vertices)
    {
        std::swap(vertex.pos.y, vertex.pos.z);
        vertex.pos.y += change_vertices_Z;
        vertex.pos.z = -vertex.pos.z;

        std::swap(vertex.normal.y, vertex.normal.z);
        vertex.normal.z = -vertex.normal.z;
    }

    std::string full_bin_path = combine_path(gltf_directory_path, out_relative_bin_path);

    std::ofstream bin_file(full_bin_path, std::ios::binary | std::ios::out);
    if (!bin_file.is_open())
    {
        LOG_WARN("Warn: failed to create and open bin file: %s", full_bin_path.c_str());
        return false;
    }

    for (const auto& vertex : model_data.vertices)
    {
        bin_file.write(reinterpret_cast<const char*>(&vertex.pos), sizeof(vertex.pos));
    }

    auto positions_byte_length = bin_file.tellp();

    for (const auto& vertex : model_data.vertices)
    {
        bin_file.write(reinterpret_cast<const char*>(&vertex.normal), sizeof(vertex.normal));
    }

    auto normals_byte_length = bin_file.tellp() - positions_byte_length;

    for (const auto& vertex : model_data.vertices)
    {
        bin_file.write(reinterpret_cast<const char*>(&vertex.tex_coord), sizeof(vertex.tex_coord));
    }

    auto tex_coords_byte_length = bin_file.tellp() - normals_byte_length - positions_byte_length;

    std::uint32_t indices_count = 0;
    std::string indices_buffer_componentType = "";
    if (model_data.indices16.size())
    {
        indices_buffer_componentType = "5123";
        indices_count = model_data.indices16.size();
        for (auto index16 : model_data.indices16)
        {
            bin_file.write(reinterpret_cast<const char*>(&index16), sizeof(index16));
        }
    }
    else if (model_data.indices32.size())
    {
        indices_buffer_componentType = "5125";
        indices_count = model_data.indices32.size();
        for (auto index32 : model_data.indices32)
        {
            bin_file.write(reinterpret_cast<const char*>(&index32), sizeof(index32));
        }
    }

    auto indices_byte_length = bin_file.tellp() - tex_coords_byte_length - normals_byte_length - positions_byte_length;

    bin_file.close();

    Vec3 min_pos, max_pos;
    Vec3 min_norm, max_norm;
    Vec2 min_tex, max_tex;
    min_pos = max_pos = model_data.vertices[0].pos;
    min_norm = max_norm = model_data.vertices[0].normal;
    min_tex = max_tex = model_data.vertices[0].tex_coord;

    for (const auto& vertex : model_data.vertices)
    {
        min_pos.x = std::min(min_pos.x, vertex.pos.x);
        min_pos.y = std::min(min_pos.y, vertex.pos.y);
        min_pos.z = std::min(min_pos.z, vertex.pos.z);

        max_pos.x = std::max(max_pos.x, vertex.pos.x);
        max_pos.y = std::max(max_pos.y, vertex.pos.y);
        max_pos.z = std::max(max_pos.z, vertex.pos.z);

        min_norm.x = std::min(min_norm.x, vertex.normal.x);
        min_norm.y = std::min(min_norm.y, vertex.normal.y);
        min_norm.z = std::min(min_norm.z, vertex.normal.z);

        max_norm.x = std::max(max_norm.x, vertex.normal.x);
        max_norm.y = std::max(max_norm.y, vertex.normal.y);
        max_norm.z = std::max(max_norm.z, vertex.normal.z);

        min_tex.x = std::min(min_tex.x, vertex.tex_coord.x);
        min_tex.y = std::min(min_tex.y, vertex.tex_coord.y);

        max_tex.x = std::max(max_tex.x, vertex.tex_coord.x);
        max_tex.y = std::max(max_tex.y, vertex.tex_coord.y);
    }

    std::string gltf_path = combine_path(gltf_directory_path, model_data.model_file_name + ".gltf");

    std::ofstream gltf_file(gltf_path, std::ios::out);
    if (!gltf_file.is_open())
    {
        LOG_WARN("Warn: failed to create and open gltf file: %s", gltf_path.c_str());
        return false;
    }

    std::ostringstream indices_bufferView;
    std::ostringstream indices_accessor;
    std::string primitives_indices = "";
    if (indices_count)
    {
        indices_bufferView << ",\n"
            "        {\n"
            "            \"buffer\": 0,\n"
            "            \"byteOffset\": " << positions_byte_length + normals_byte_length + tex_coords_byte_length << ",\n"
            "            \"byteLength\": " << indices_byte_length << ",\n"
            "            \"target\": 34963\n"
            "        }";

        indices_accessor << ",\n"
            "        {\n"
            "            \"bufferView\": 3,\n"
            "            \"componentType\": "<< indices_buffer_componentType << ",\n"
            "            \"count\": " << indices_count << ",\n"
            "            \"type\": \"SCALAR\",\n"
            "            \"max\": [\n"
            "                " << indices_count - 1 << "\n"
            "            ],\n"
            "            \"min\": [\n"
            "                0\n"
            "            ]\n"
            "        }";

        primitives_indices = ",\n                    \"indices\": 3";
    }

    std::string blend = "";
    if (model_data.is_blend_material)
    {
        blend = ",\n            \"alphaMode\": \"BLEND\"";
    }

    gltf_file << "{\n"
        "    \"asset\": {\n"
        "        \"generator\": \"dmd2gltf\",\n"
        "        \"version\": \"2.0\"\n"
        "    },\n"
        "    \"buffers\": [\n"
        "        {\n"
        "            \"uri\": \"" << out_relative_bin_path << "\",\n"
        "            \"byteLength\": " << positions_byte_length + normals_byte_length + tex_coords_byte_length + indices_byte_length << "\n"
        "        }\n"
        "    ],\n"
        "    \"bufferViews\": [\n"
        "        {\n"
        "            \"buffer\": 0,\n"
        "            \"byteOffset\": 0,\n"
        "            \"byteLength\": " << positions_byte_length << ",\n"
        "            \"target\": 34962\n"
        "        },\n"
        "        {\n"
        "            \"buffer\": 0,\n"
        "            \"byteOffset\": " << positions_byte_length << ",\n"
        "            \"byteLength\": " << normals_byte_length << ",\n"
        "            \"target\": 34962\n"
        "        },\n"
        "        {\n"
        "            \"buffer\": 0,\n"
        "            \"byteOffset\": " << positions_byte_length + normals_byte_length << ",\n"
        "            \"byteLength\": " << tex_coords_byte_length << ",\n"
        "            \"target\": 34962\n"
        "        }" << indices_bufferView.str() << "\n"
        "    ],\n"
        "    \"accessors\": [\n"
        "        {\n"
        "            \"bufferView\": 0,\n"
        "            \"componentType\": 5126,\n"
        "            \"count\": " << model_data.vertices.size() << ",\n"
        "            \"type\": \"VEC3\",\n"
        "            \"max\": [\n"
        "                " << max_pos.x << ",\n"
        "                " << max_pos.y << ",\n"
        "                " << max_pos.z << "\n"
        "            ],\n"
        "            \"min\": [\n"
        "                " << min_pos.x << ",\n"
        "                " << min_pos.y << ",\n"
        "                " << min_pos.z << "\n"
        "            ]\n"
        "        },\n"
        "        {\n"
        "            \"bufferView\": 1,\n"
        "            \"componentType\": 5126,\n"
        "            \"count\": " << model_data.vertices.size() << ",\n"
        "            \"type\": \"VEC3\",\n"
        "            \"max\": [\n"
        "                " << max_norm.x << ",\n"
        "                " << max_norm.y << ",\n"
        "                " << max_norm.z << "\n"
        "            ],\n"
        "            \"min\": [\n"
        "                " << min_norm.x << ",\n"
        "                " << min_norm.y << ",\n"
        "                " << min_norm.z << "\n"
        "            ]\n"
        "        },\n"
        "        {\n"
        "            \"bufferView\": 2,\n"
        "            \"componentType\": 5126,\n"
        "            \"count\": " << model_data.vertices.size() << ",\n"
        "            \"type\": \"VEC2\",\n"
        "            \"max\": [\n"
        "                " << max_tex.x << ",\n"
        "                " << max_tex.y << "\n"
        "            ],\n"
        "            \"min\": [\n"
        "                " << min_tex.x << ",\n"
        "                " << min_tex.y << "\n"
        "            ]\n"
        "        }" << indices_accessor.str() << "\n"
        "    ],\n"
        "    \"images\": [\n"
        "        {\n"
        "            \"uri\": \"" << out_relative_texture_path << "\"\n"
        "        }\n"
        "    ],\n"
        "    \"samplers\": [\n"
        "        {\n"
        "            \"magFilter\": 9729,\n"
        "            \"minFilter\": 9987,\n"
        "            \"wrapS\": 10497,\n"
        "            \"wrapT\": 10497\n"
        "        }\n"
        "    ],\n"
        "    \"textures\": [\n"
        "        {\n"
        "            \"sampler\": 0,\n"
        "            \"source\": 0\n"
        "        }\n"
        "    ],\n"
        "    \"materials\": [\n"
        "        {\n"
        "            \"pbrMetallicRoughness\": {\n"
        "                \"baseColorTexture\": {\n"
        "                    \"index\": 0,\n"
        "                    \"texCoord\": 0\n"
        "                }\n"
        "            }" << blend << "\n"
        "        }\n"
        "    ],\n"
        "    \"meshes\": [\n"
        "        {\n"
        "            \"primitives\": [\n"
        "                {\n"
        "                    \"attributes\": {\n"
        "                        \"POSITION\": 0,\n"
        "                        \"NORMAL\": 1,\n"
        "                        \"TEXCOORD_0\": 2\n"
        "                    }" << primitives_indices << ",\n"
        "                    \"material\": 0,\n"
        "                    \"mode\": 4\n"
        "                }\n"
        "            ]\n"
        "        }\n"
        "    ],\n"
        "    \"nodes\": [\n"
        "        {\n"
        "            \"mesh\": 0\n"
        "        }\n"
        "    ],\n"
        "    \"scenes\": [\n"
        "        {\n"
        "            \"nodes\": [ 0 ]\n"
        "        }\n"
        "    ],\n"
        "    \"scene\": 0\n"
        "}\n";

    gltf_file.close();

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::compress_to_ktx2(const std::string& in_texture_path, const std::string& out_texture_path, bool& is_alpha)
{
    int w = 0, h = 0, ch = 0;
    stbi_uc* data = stbi_load(in_texture_path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        LOG_WARN("STBI: error (%s) while loading texture file: %s", stbi_failure_reason(), in_texture_path.c_str());
        return false;
    }

    is_alpha = false;
    if (ch >= 4)
    {
        for (size_t i = 0; i < w * h; ++i)
        {
            unsigned char* a = data + i * 4 + 3;
            if (*a < 255)
            {
                is_alpha = true;
                break;
            }
        }
    }

    size_t size = w * h * 4;

    ktxTextureCreateInfo ci = {};
    // --format R8G8B8A8_SRGB
    ci.vkFormat        = VK_FORMAT_R8G8B8A8_SRGB;

    ci.baseWidth       = static_cast<ktx_uint32_t>(w);
    ci.baseHeight      = static_cast<ktx_uint32_t>(h);
    ci.baseDepth       = 1;
    ci.numDimensions   = 2;
    ci.numLayers       = 1;
    ci.numFaces        = 1;
    ci.isArray         = KTX_FALSE;

    // Р§РёСЃР»Рѕ mipmap-СѓСЂРѕРІРЅРµР№ РѕРїСЂРµРґРµР»СЏРµРј РЇР’РќРћ, С‚Р°Рє РєР°Рє СЃР°РјРё РёС… СЃРіРµРЅРµСЂРёСЂСѓРµРј
    ci.numLevels       = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h))) + 1);
    // РќРµ РїРѕР»Р°РіР°РµРјСЃСЏ РЅР° Р°РїРїСЂР°С‚РЅСѓСЋ РіРµРЅРµСЂР°С†РёСЋ - СЌС‚Рѕ РґР»СЏ РЅРµСЃР¶Р°С‚С‹С… С‚РµРєСЃС‚СѓСЂ
    ci.generateMipmaps = KTX_FALSE;

    ktxTexture2* Ktexture2 = nullptr;
    KTX_error_code result = ktxTexture2_Create(&ci,
                                               KTX_TEXTURE_CREATE_ALLOC_STORAGE,
                                               &Ktexture2);

    if (result != KTX_SUCCESS)
    {
        LOG_WARN("KTX: error (%u) while creating ktxTexture2 for file: %s", result, in_texture_path.c_str());
        stbi_image_free(data);
        return false;
    }

    ktxTexture* Ktexture = ktxTexture(Ktexture2);
    result = ktxTexture_SetImageFromMemory(Ktexture,
                                           0, 0, 0,
                                           data, size);
    if (result != KTX_SUCCESS)
    {
        LOG_WARN("KTX: error (%u) to set 0 level of image from file: %s", result, in_texture_path.c_str());
        ktxTexture_Destroy(Ktexture);
        stbi_image_free(data);
        return false;
    }

    // РњР°СЃСЃРёРІ РїРѕРґ СѓРєР°Р·Р°С‚РµР»Рё РЅР° РґР°РЅРЅС‹Рµ РІСЃРµС… СѓСЂРѕРІРЅРµР№, РЅРµ СѓРґР°Р»СЏРµРј СЌС‚Рё РґР°РЅРЅС‹Рµ
    // РґРѕ СЃР¶Р°С‚РёСЏ Рё СЃРѕС…СЂР°РЅРµРЅРёСЏ С‚РµРєСЃС‚СѓСЂС‹ РЅР° РґРёСЃРє!
    std::vector<stbi_uc *> levels_data = {data};

    // Р’СЂСѓС‡РЅСѓСЋ РіРµРЅРµСЂРёСЂСѓРµРј mipmaps, РїРѕСЃР»РµРґРѕРІР°С‚РµР»СЊРЅРѕ СѓРјРµРЅСЊС€Р°СЏ
    // СЂР°Р·РјРµСЂС‹ С‚РµРєСЃС‚СѓСЂС‹ РІ РґРІР° СЂР°Р·Р° РЅР° РєР°Р¶РґРѕРј СѓСЂРѕРІРЅРµ
    for (uint32_t level = 1; level < ci.numLevels; ++level)
    {
        int next_w = std::max(1, w >> 1);
        int next_h = std::max(1, h >> 1);
        size = next_w * next_h * 4;
        stbi_uc* next_resized = new stbi_uc[size];

        // Р РµСЃРµРјРїР»РёРЅРі
        stbir_resize_uint8(levels_data.back(), w, h, 0,
                           next_resized, next_w, next_h, 0, 4);
        levels_data.push_back(next_resized);

        // Р—Р°РіСЂСѓР¶Р°РµРј СѓСЂРѕРІРµРЅСЊ РІ ktxTexture
        result = ktxTexture_SetImageFromMemory(Ktexture,
                                               level, 0, 0,
                                               next_resized, size);
        if (result != KTX_SUCCESS)
        {
            LOG_WARN("KTX: error (%u) to set %u level of image from file: %s", result, level, in_texture_path.c_str());
            ktxTexture_Destroy(Ktexture);
            for (auto d : levels_data)
            {
                stbi_image_free(d);
            }
            return false;
        }
        w = next_w;
        h = next_h;
    }

    ktxBasisParams params = {};
    params.structSize = sizeof(ktxBasisParams);  // Р’СЃРµРіРґР° РїРµСЂРІС‹Рј РїРѕР»РµРј

    // --encode basis-lz

    // --qlevel (РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ 128)
    params.qualityLevel = 128;

    // --clevel (CLI РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ 1, С…РѕС‚СЏ РІ libktx РєРѕРЅСЃС‚Р°РЅС‚Р° = 2)
    params.compressionLevel = 1;

    // РџРѕС‚РѕРєРё
    params.threadCount = 1;

    // RDO-РїРѕСЂРѕРіРё (РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ 1.25, РїСЂРёРјРµРЅСЏСЋС‚СЃСЏ РїСЂРё qlevel <= 128)
    params.endpointRDOThreshold = 1.25f;
    params.selectorRDOThreshold = 1.25f;

    // РђРІС‚РѕРјР°С‚РёС‡РµСЃРєРёР№ РІС‹Р±РѕСЂ РєР»Р°СЃС‚РµСЂРѕРІ (РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ)
    params.maxEndpoints = 0;
    params.maxSelectors = 0;

    // Р¤Р»Р°РіРё РїРѕ СѓРјРѕР»С‡Р°РЅРёСЋ
    params.verbose           = KTX_FALSE;
    params.noSSE             = KTX_FALSE;
    params.normalMap         = KTX_FALSE;
    params.preSwizzle        = KTX_FALSE;
    params.noEndpointRDO     = KTX_FALSE;
    params.noSelectorRDO     = KTX_FALSE;

    result = ktxTexture2_CompressBasisEx(Ktexture2, &params);

    if (result != KTX_SUCCESS)
    {
        LOG_WARN("KTX: error (%u) while compressing image from file: %s", result, in_texture_path.c_str());
    }
    else
    {
        result = ktxTexture_WriteToNamedFile(Ktexture, out_texture_path.c_str());

        if (result != KTX_SUCCESS)
        {
            LOG_WARN("KTX: error (%u) while writing file: %s", result, out_texture_path.c_str());
        }
    }

    // РћСЃРІРѕР±РѕР¶РґР°РµРј РїР°РјСЏС‚СЊ РѕС‚ ktx-РїР°СЂР°РјРµС‚СЂРѕРІ Рё РѕС‚ РїРёРєСЃРµР»РµР№ РІСЃРµС… СѓСЂРѕРІРЅРµР№
    ktxTexture_Destroy(Ktexture);
    for (auto d : levels_data)
    {
        stbi_image_free(d);
    }

    return result == KTX_SUCCESS;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Application::configure_parser(cli::Parser &parser)
{
    parser.set_optional<std::string>("i", "input-route",
                                     "",
                                     "Input DMD route path");

    parser.set_optional<bool>("u", "used-only",
                              false,
                              "Convert only models used at map");

    parser.set_optional<bool>("l", "lights",
                              false,
                              "Convert lights at map");

    parser.set_optional<bool>("c", "compress-textures",
                              false,
                              "Convert textures to compressed .ktx2");

    parser.set_optional<bool>("s", "smooth",
                              false,
                              "Smooth normals (single-model use)");

    parser.set_optional<int>("n", "num-threads",
                             std::thread::hardware_concurrency(),
                             "Count of threads (route use)");

    parser.set_optional<std::string>("o", "output-route",
                                     "",
                                     "Output GLTF route path");

    parser.set_optional<std::string>("m", "input-model",
                                     "",
                                     "Input DMD model path");

    parser.set_optional<std::string>("t", "input-texture",
                                     "",
                                     "Input DMD texture path");

    parser.set_optional<std::string>("g", "output-model",
                                     "",
                                     "Output GLTF model path");

    parser.enable_help();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Application::parse_command_line(cli::Parser &parser, cmd_line_t &cmd_line)
{
    parser.run_and_exit_if_error();
    cmd_line.input_route_path = parser.get<std::string>("i");
    cmd_line.input_only_used_at_map = parser.get<bool>("u");
    cmd_line.input_lights_at_map = parser.get<bool>("l");
    cmd_line.input_compress_textures = parser.get<bool>("c");
    cmd_line.smooth = parser.get<bool>("s");
    cmd_line.num_threads = parser.get<int>("n");
    cmd_line.output_route_path = parser.get<std::string>("o");
    cmd_line.input_model_path = parser.get<std::string>("m");
    cmd_line.input_texture_path = parser.get<std::string>("t");
    cmd_line.output_model_path = parser.get<std::string>("g");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Application::set_convert_mode(const cmd_line_t &cmd_line,
                                   ConvertMode &convert_mode)
{
    if (cmd_line.input_route_path.isPresent())
    {
        if (cmd_line.output_route_path.isPresent())
        {
            convert_mode = CONVERT_ROUTE;
            return true;
        }
        else
        {
            LOG_WARN("ERROR: Missing route output path");
            return false;
        }
    }

    if (cmd_line.input_model_path.isPresent())
    {
        if (cmd_line.input_texture_path.isPresent())
        {
            if (cmd_line.output_model_path.isPresent())
            {
                convert_mode = CONVERT_MODEL;
                return true;
            }
            else
            {
                LOG_WARN("ERROR: Missing output GLTF model path");
            }
        }
        else
        {
            LOG_WARN("ERROR: Missing input DMD texture path");
        }
    }
    else
    {
        LOG_WARN("ERROR: Missing input route path or DMD model path");
    }

    return false;
}

