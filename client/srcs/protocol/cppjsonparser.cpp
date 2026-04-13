#include "PostProcessConfigLoader.hpp"
#include "../../incs/third_party/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static PostProcessEffect parseEffect(const std::string& s) {
    if (s == "CRT")                 return PostProcessEffect::CRT;
    if (s == "Scanlines")           return PostProcessEffect::Scanlines;
    if (s == "Bloom")               return PostProcessEffect::Bloom;
    if (s == "Vignette")            return PostProcessEffect::Vignette;
    if (s == "ChromaticAberration") return PostProcessEffect::ChromaticAberration;
    if (s == "Grain")               return PostProcessEffect::Grain;
    throw std::runtime_error("PostProcessConfigLoader: unknown effect: " + s);
}

PostProcessConfigLoader::PresetTable PostProcessConfigLoader::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("PostProcessConfigLoader: cannot open: " + path);

    json data;
    try {
        file >> data;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("PostProcessConfigLoader: parse error: " + std::string(e.what()));
    }

    PresetTable table;

    for (const auto& entry : data.at("presets")) {
        PostProcessConfig cfg;

        cfg.enabled              = entry.at("enabled").get<bool>();
        cfg.scanlineIntensity    = entry.at("scanlineIntensity").get<float>();
        cfg.curvatureAmount      = entry.at("curvatureAmount").get<float>();
        cfg.vignetteStrength     = entry.at("vignetteStrength").get<float>();
        cfg.chromaticAberration  = entry.at("chromaticAberration").get<float>();
        cfg.grainAmount          = entry.at("grainAmount").get<float>();
        cfg.bloomIntensity       = entry.at("bloomIntensity").get<float>();

        for (const auto& fx : entry.at("effects"))
            cfg.effects.push_back(parseEffect(fx.get<std::string>()));

        const std::string name = entry.at("name").get<std::string>();
        table[name] = cfg;
    }

    return table;
}
