#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

struct Json {
    enum Type { Null, String, Number, Boolean, Object, Array } type = Null;
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;
};

struct Config {
    std::string package;
    std::string repository;
    bool test_mode = false;
    int max_depth = 0;
    std::string filter;
};

using Graph = std::map<std::string, std::vector<std::string>>;
using Loader = std::function<std::vector<std::string>(const std::string&)>;

std::string read_file(const std::string& filename);
Json parse_json(const std::string& text);
Config load_config(const std::string& filename);
void print_config(const Config& config);
bool valid_package(const std::string& name, bool test_mode);

struct Dependency {
    std::string group, artifact, version;
    std::string scope, optional, type, classifier;
};

struct Pom {
    std::string parent;
    std::map<std::string, std::string> properties;
    std::vector<Dependency> dependencies, managed;
};

using Fetcher = std::function<std::string(const std::string&)>;
std::wstring wide(const std::string& text);
std::string utf8(const std::wstring& text);
std::string http_get(const std::string& url);
Pom parse_pom(const std::string& xml);
std::string expand(std::string value, const std::map<std::string, std::string>& properties);
std::string pom_url(std::string repository, const std::string& package);

class MavenRepository {
    std::string repository;
    Fetcher fetch;
    std::map<std::string, Pom> cache;
public:
    explicit MavenRepository(std::string url, Fetcher fetcher = http_get);
    std::vector<std::string> direct(const std::string& package);
};

Graph load_test_repository(const std::string& filename);
Graph build_graph(const std::string& root, const Loader& load, int max_depth, const std::string& filter);
void print_graph(const Graph& graph);
Graph reverse_graph(const Graph& graph);

std::string d2_text(const Graph& graph);
std::string save_diagram(const Graph& graph, const std::string& package, bool open_image);
