#include "app.h"
#include <filesystem>
#include <iostream>
#include <regex>
#include <stdexcept>

bool valid_package(const std::string& name, bool test_mode) {
    const std::regex test("[A-Z]+");
    const std::regex maven("[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)*:"
                           "[A-Za-z0-9_][A-Za-z0-9_.-]*:[A-Za-z0-9_][A-Za-z0-9_.-]*");
    return std::regex_match(name, test_mode ? test : maven)
        && name.find("..") == std::string::npos;
}

static const Json& field(const Json& root, const std::string& key, Json::Type type) {
    auto it = root.object.find(key);
    if (it == root.object.end()) throw std::runtime_error("Missing parameter: " + key);
    if (it->second.type != type) throw std::runtime_error("Wrong type of parameter: " + key);
    return it->second;
}

Config load_config(const std::string& filename) {
    Json json = parse_json(read_file(filename));
    if (json.type != Json::Object) throw std::runtime_error("Config must be a JSON object");
    Config config;
    config.package = field(json, "package", Json::String).text;
    config.repository = field(json, "repository", Json::String).text;
    config.test_mode = field(json, "test_mode", Json::Boolean).text == "true";
    config.filter = field(json, "filter", Json::String).text;
    std::string depth = field(json, "max_depth", Json::Number).text;
    if (!std::regex_match(depth, std::regex("[0-9]+")))
        throw std::runtime_error("max_depth must be a non-negative integer");
    try { config.max_depth = std::stoi(depth); }
    catch (...) { throw std::runtime_error("max_depth is too large"); }
    if (!valid_package(config.package, config.test_mode))
        throw std::runtime_error("Invalid package: use A-Z in test mode or group:artifact:version");
    if (config.repository.empty()) throw std::runtime_error("repository must not be empty");
    if (config.test_mode) {
        auto file = std::filesystem::u8path(config.repository);
        if (file.is_relative()) file = std::filesystem::u8path(filename).parent_path() / file;
        config.repository = file.lexically_normal().u8string();
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("repository file does not exist");
    } else if (!std::regex_match(config.repository, std::regex("https?://[^/\\s?#]+(/[^\\s?#]*)?"))) {
        throw std::runtime_error("repository must be an HTTP(S) URL");
    }
    if (json.object.size() != 5) throw std::runtime_error("Unknown parameter in config");
    return config;
}

void print_config(const Config& config) {
    std::cout << "package = " << config.package << '\n'
              << "repository = " << config.repository << '\n'
              << "test_mode = " << (config.test_mode ? "true" : "false") << '\n'
              << "max_depth = " << config.max_depth << '\n'
              << "filter = " << config.filter << '\n';
}
