#include "app.h"
#include <algorithm>
#include <set>
#include <stdexcept>

std::string expand(std::string value, const std::map<std::string, std::string>& properties) {
    std::set<std::string> seen;
    while (value.find("${") != std::string::npos) {
        if (!seen.insert(value).second || seen.size() > 100)
            throw std::runtime_error("Cyclic POM property: " + value);
        size_t first = value.find("${"), last = value.find('}', first);
        if (last == std::string::npos) throw std::runtime_error("Invalid POM property: " + value);
        std::string key = value.substr(first + 2, last - first - 2);
        auto property = properties.find(key);
        if (property == properties.end()) throw std::runtime_error("Unknown POM property: " + key);
        value.replace(first, last - first + 1, property->second);
    }
    return value;
}

std::string pom_url(std::string repository, const std::string& package) {
    if (!valid_package(package, false)) throw std::runtime_error("Invalid Maven package: " + package);
    size_t first = package.find(':'), last = package.rfind(':');
    std::string group = package.substr(0, first);
    std::string artifact = package.substr(first + 1, last - first - 1), version = package.substr(last + 1);
    std::replace(group.begin(), group.end(), '.', '/');
    while (!repository.empty() && repository.back() == '/') repository.pop_back();
    return repository + "/" + group + "/" + artifact + "/" + version + "/" + artifact + "-" + version + ".pom";
}

static void aliases(std::map<std::string, std::string>& properties, const std::string& package,
                    const std::string& prefix) {
    size_t first = package.find(':'), last = package.rfind(':');
    properties[prefix + "groupId"] = package.substr(0, first);
    properties[prefix + "artifactId"] = package.substr(first + 1, last - first - 1);
    properties[prefix + "version"] = package.substr(last + 1);
}

MavenRepository::MavenRepository(std::string url, Fetcher fetcher)
    : repository(std::move(url)), fetch(std::move(fetcher)) {}

std::vector<std::string> MavenRepository::direct(const std::string& package) {
    std::vector<Pom> chain;
    std::set<std::string> seen;
    std::string name = package;
    while (!name.empty()) {
        if (!seen.insert(name).second) throw std::runtime_error("Cyclic POM parents: " + name);
        auto found = cache.find(name);
        if (found == cache.end()) found = cache.emplace(name, parse_pom(fetch(pom_url(repository, name)))).first;
        chain.push_back(found->second);
        name = found->second.parent;
    }
    std::map<std::string, std::string> properties;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (const auto& entry : it->properties) properties[entry.first] = entry.second;
    aliases(properties, package, "project.");
    aliases(properties, package, "pom.");
    if (!chain[0].parent.empty()) {
        aliases(properties, chain[0].parent, "project.parent.");
        aliases(properties, chain[0].parent, "parent.");
    }
    std::map<std::string, Dependency> managed, specs;
    auto key = [&](const Dependency& dep) {
        std::string type = dep.type.empty() ? "jar" : expand(dep.type, properties);
        return expand(dep.group, properties) + ":" + expand(dep.artifact, properties)
             + ":" + type + ":" + expand(dep.classifier, properties);
    };
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (const auto& dep : it->managed) {
            if (dep.scope == "import") throw std::runtime_error("BOM import is not supported");
            managed[key(dep)] = dep;
        }
        for (const auto& dep : it->dependencies) specs[key(dep)] = dep;
    }
    std::vector<std::string> result;
    for (const auto& entry : specs) {
        Dependency dep = entry.second;
        auto found = managed.find(entry.first);
        if (found != managed.end()) {
            if (dep.version.empty()) dep.version = found->second.version;
            if (dep.scope.empty()) dep.scope = found->second.scope;
            if (dep.type.empty()) dep.type = found->second.type;
        }
        std::string scope = expand(dep.scope, properties), optional = expand(dep.optional, properties);
        if ((!scope.empty() && scope != "compile" && scope != "runtime") || optional == "true") continue;
        std::string type = expand(dep.type, properties);
        if ((!type.empty() && type != "jar") || !dep.classifier.empty())
            throw std::runtime_error("Only JAR dependencies without classifier are supported");
        std::string version = expand(dep.version, properties);
        std::string full = expand(dep.group, properties) + ":" + expand(dep.artifact, properties) + ":" + version;
        if (!valid_package(full, false)) throw std::runtime_error("Missing or unsupported version: " + full);
        result.push_back(full);
    }
    return result;
}
