#include "app.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

Graph load_test_repository(const std::string& filename) {
    Json json = parse_json(read_file(filename));
    if (json.type != Json::Object) throw std::runtime_error("Test repository must be a JSON object");
    Graph graph;
    for (const auto& entry : json.object) {
        if (!valid_package(entry.first, true) || entry.second.type != Json::Array)
            throw std::runtime_error("Test repository: expected A-Z name and dependency array");
        auto& deps = graph[entry.first];
        for (const auto& dep : entry.second.array) {
            if (dep.type != Json::String || !valid_package(dep.text, true))
                throw std::runtime_error("Test repository: invalid dependency name");
            if (std::find(deps.begin(), deps.end(), dep.text) == deps.end()) deps.push_back(dep.text);
        }
    }
    for (const auto& entry : graph)
        for (const auto& dep : entry.second)
            if (!graph.count(dep)) throw std::runtime_error("Test repository: missing package " + dep);
    return graph;
}

Graph build_graph(const std::string& root, const Loader& load, int max_depth, const std::string& filter) {
    Graph graph;
    if (!filter.empty() && root.find(filter) != std::string::npos) return graph;
    std::vector<std::pair<std::string, int>> stack{{root, 0}};
    std::map<std::string, int> depths;
    while (!stack.empty()) {
        auto current = stack.back();
        stack.pop_back();
        const std::string& name = current.first;
        int depth = current.second;
        auto seen = depths.find(name);
        if (seen != depths.end() && seen->second <= depth) continue;
        depths[name] = depth;
        graph[name];
        if (depth >= max_depth) continue;
        std::vector<std::string> deps;
        for (const auto& dep : load(name)) {
            if (!filter.empty() && dep.find(filter) != std::string::npos) continue;
            if (std::find(deps.begin(), deps.end(), dep) == deps.end()) deps.push_back(dep);
        }
        graph[name] = deps;
        for (auto it = deps.rbegin(); it != deps.rend(); ++it) {
            graph[*it];
            stack.push_back({*it, depth + 1});
        }
    }
    return graph;
}

void print_graph(const Graph& graph) {
    if (graph.empty()) std::cout << "Graph is empty\n";
    for (const auto& entry : graph) {
        std::cout << entry.first << '\n';
        for (const auto& dep : entry.second) std::cout << "  -> " << dep << '\n';
    }
}

Graph reverse_graph(const Graph& graph) {
    Graph reversed;
    for (const auto& entry : graph) {
        reversed[entry.first];
        for (const auto& dep : entry.second) reversed[dep].push_back(entry.first);
    }
    return reversed;
}
