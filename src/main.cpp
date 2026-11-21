#include "app.h"
#include <iostream>
#include <stdexcept>
#include <windows.h>

struct Options {
    std::string filename = "config.json";
    std::string reverse;
    int stage = 5;
    bool open_image = true;
};

static Options options(int argc, wchar_t* argv[]) {
    Options result;
    bool has_file = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = utf8(argv[i]);
        if (argument == "--stage" && i + 1 < argc) {
            std::string value = utf8(argv[++i]);
            if (value.size() != 1 || value[0] < '1' || value[0] > '5')
                throw std::runtime_error("stage must be from 1 to 5");
            result.stage = value[0] - '0';
        } else if (argument == "--reverse" && i + 1 < argc) {
            result.reverse = utf8(argv[++i]);
        } else if (argument == "--no-open") result.open_image = false;
        else if (!argument.empty() && argument[0] != '-' && !has_file) {
            result.filename = argument;
            has_file = true;
        } else throw std::runtime_error("Usage: app.exe [config.json] [--stage 1..5] [--reverse PACKAGE] [--no-open]");
    }
    if (!result.reverse.empty() && result.stage != 4)
        throw std::runtime_error("--reverse requires --stage 4");
    return result;
}

static void run(const Options& args) {
    Config config = load_config(args.filename);
    if (args.stage == 1) { print_config(config); return; }
    MavenRepository maven(config.repository);
    if (args.stage == 2) {
        if (config.test_mode) throw std::runtime_error("Stage 2 requires a Maven repository");
        std::cout << config.package << '\n';
        for (const auto& dep : maven.direct(config.package)) std::cout << "  -> " << dep << '\n';
        return;
    }
    Graph repository;
    if (config.test_mode) {
        repository = load_test_repository(config.repository);
        if (!repository.count(config.package)) throw std::runtime_error("Package is absent in test repository");
    }
    Loader load = [&](const std::string& name) {
        return config.test_mode ? repository.at(name) : maven.direct(name);
    };
    Graph graph = build_graph(config.package, load, config.max_depth, config.filter);
    if (args.stage == 4) {
        std::string target = args.reverse.empty() ? config.package : args.reverse;
        Graph reversed = reverse_graph(config.test_mode ? repository : graph);
        if (!reversed.count(target)) throw std::runtime_error("Reverse target is absent: " + target);
        graph = build_graph(target, [&](const std::string& name) { return reversed.at(name); },
                            config.max_depth, config.filter);
        std::cout << "Reverse dependencies (target -> consumer):\n";
    }
    print_graph(graph);
    if (args.stage == 5) std::cout << "Image: " << save_diagram(graph, config.package, args.open_image) << '\n';
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        run(options(argc, argv));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
