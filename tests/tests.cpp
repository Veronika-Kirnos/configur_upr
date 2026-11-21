#include "../src/app.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int checks = 0;
void check(bool condition, const std::string& name) {
    ++checks;
    if (!condition) throw std::runtime_error("Test failed: " + name);
}

void fails(const std::function<void()>& action, const std::string& name) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, name);
}

void config_tests() {
    std::filesystem::create_directories("build");
    std::string good = R"({"package":"A","repository":"../config.json",
        "test_mode":true,"max_depth":3,"filter":""})";
    auto load = [&](std::string text) {
        std::ofstream("build/test-config.json") << text;
        return load_config("build/test-config.json");
    };
    check(load(good).max_depth == 3, "valid config");
    for (const auto& edit : std::vector<std::pair<std::string, std::string>>{
        {"\"A\"", "\"a\""}, {"../config.json", "missing.json"}, {"true", "\"true\""},
        {"3", "-1"}, {"3", "1.5"}, {"3", "99999999999999999999"}, {"\"filter\":\"\"", "\"filter\":7"}}) {
        std::string bad = good;
        bad.replace(bad.find(edit.first), edit.first.size(), edit.second);
        fails([&] { load(bad); }, "bad parameter " + edit.first);
    }
    fails([&] { load("{}"); }, "missing parameters");
    fails([&] { load("[]"); }, "config is not an object");
    fails([&] { load("{broken}"); }, "broken JSON");
    fails([] { load_config("missing-config.json"); }, "missing config file");
    check(valid_package("org.slf4j:slf4j-api:1.7.36", false), "Maven coordinates");
    check(!valid_package("../x:a:1", false), "unsafe coordinates");
    std::string real = R"({"package":"a:b:1","repository":"https://repo.example/maven2",
        "test_mode":false,"max_depth":0,"filter":""})";
    check(!load(real).test_mode, "real repository config");
    std::string bad_url = real;
    bad_url.replace(bad_url.find("https://repo.example/maven2"), 27, "ftp://repo");
    fails([&] { load(bad_url); }, "repository URL scheme");
    for (const auto& key : {"package", "repository", "test_mode", "max_depth", "filter"}) {
        std::string text = real;
        size_t begin = text.find(std::string("\"") + key + "\"");
        size_t colon = text.find(':', begin);
        size_t end = text.find_first_of(",}", colon);
        text.replace(colon + 1, end - colon - 1, "null");
        fails([&] { load(text); }, std::string("wrong field type: ") + key);
    }
    std::string unknown = good;
    unknown.insert(unknown.size() - 1, ",\"extra\":true");
    fails([&] { load(unknown); }, "unknown config parameter");
    check(!valid_package("a:.:1", false), "invalid artifact");
    std::filesystem::remove("build/test-config.json");
}

void json_tests() {
    check(parse_json(R"({"s":"\u0410\n\uD83D\uDE00"})").object.at("s").text == "А\n😀", "Unicode JSON");
    for (const std::string text : {"{\"a\":1,\"a\":2}", "[1,]", "true false", "01", "\"\\q\""})
        fails([&] { parse_json(text); }, "invalid JSON " + text);
}

void maven_tests() {
    check(pom_url("https://repo/", "a.b:c:1") == "https://repo/a/b/c/1/c-1.pom", "POM URL");
    std::string parent = R"(<project><properties><v>2</v></properties><dependencyManagement><dependencies>
        <dependency><groupId>x</groupId><artifactId>y</artifactId><version>${v}</version></dependency>
        </dependencies></dependencyManagement></project>)";
    std::string child = R"(<project xmlns="http://maven.apache.org/POM/4.0.0">
        <parent><groupId>a</groupId><artifactId>parent</artifactId><version>1</version></parent>
        <dependencies><dependency><groupId>x</groupId><artifactId>y</artifactId></dependency>
        <dependency><groupId>x</groupId><artifactId>y</artifactId><type>test-jar</type>
        <version>1</version><scope>test</scope></dependency>
        <dependency><groupId>x</groupId><artifactId>test</artifactId><scope>test</scope></dependency>
        <dependency><groupId>x</groupId><artifactId>optional</artifactId><optional>true</optional></dependency>
        </dependencies><build><plugins><plugin><dependencies><dependency><groupId>ignored</groupId>
        </dependency></dependencies></plugin></plugins></build></project>)";
    int requests = 0;
    MavenRepository repository("https://repo", [&](const std::string& url) {
        ++requests;
        return url.find("/parent/") != std::string::npos ? parent : child;
    });
    check(repository.direct("a:child:1") == std::vector<std::string>{"x:y:2"}, "parent and managed version");
    repository.direct("a:child:1");
    check(requests == 2, "POM cache");
    fails([] { parse_pom("<project><broken></project>"); }, "malformed XML");
    fails([] { parse_pom("<html/>"); }, "non-POM XML");
    fails([] { expand("${a}", {{"a", "${a}"}}); }, "cyclic property");
    fails([] { expand("${missing}", {}); }, "unknown property");
    MavenRepository cycle("https://repo", [&](const std::string&) {
        return "<project><parent><groupId>a</groupId><artifactId>b</artifactId>"
               "<version>1</version></parent></project>";
    });
    fails([&] { cycle.direct("a:b:1"); }, "parent cycle");
}
void graph_tests() {
    Graph repository{{"A", {"B", "C"}}, {"B", {"X"}}, {"X", {"D"}},
                     {"C", {"D"}}, {"D", {"E"}}, {"E", {}}};
    Loader load = [&](const std::string& name) { return repository.at(name); };
    Graph graph = build_graph("A", load, 3, "");
    check(graph.count("E") && graph.at("D") == std::vector<std::string>{"E"}, "shorter path reopens node");
    check(build_graph("A", load, 0, "").size() == 1, "zero depth");
    check(build_graph("A", load, 1, "").size() == 3, "depth limit");
    check(!build_graph("A", load, 5, "B").count("B"), "filter");
    check(build_graph("A", load, 5, "A").empty(), "filtered root");
    check(build_graph("E", load, 5, "").size() == 1, "leaf");
    repository = {{"A", {"B"}}, {"B", {"C"}}, {"C", {"A"}}};
    check(build_graph("A", load, 100, "").size() == 3, "cycle");
    repository = {{"A", {"A"}}};
    check(build_graph("A", load, 100, "").at("A").size() == 1, "self cycle");
    int calls = 0;
    Loader chain = [&](const std::string& name) {
        ++calls;
        int index = std::stoi(name);
        return index < 1999 ? std::vector<std::string>{std::to_string(index + 1)} : std::vector<std::string>{};
    };
    check(build_graph("0", chain, 2000, "").size() == 2000 && calls == 2000, "non-recursive DFS");
    std::vector<std::string> order;
    repository = {{"A", {"B", "C"}}, {"B", {"D"}}, {"C", {}}, {"D", {}}};
    build_graph("A", [&](const std::string& name) { order.push_back(name); return repository.at(name); }, 5, "");
    check(order == std::vector<std::string>({"A", "B", "D", "C"}), "DFS order");
    check(load_test_repository("examples/repository.json").size() == 6, "read test repository");
    for (const auto& bad : {"[]", "{\"a\":[]}", "{\"A\":[7]}", "{\"A\":[\"B\"]}"}) {
        std::ofstream("build/test-repository.json") << bad;
        fails([] { load_test_repository("build/test-repository.json"); }, "invalid test repository");
    }
    std::filesystem::remove("build/test-repository.json");
}
void reverse_tests() {
    Graph repository{{"A", {"B", "C"}}, {"B", {"D"}}, {"C", {"D"}}, {"D", {}}, {"F", {"A"}}};
    Graph reversed = reverse_graph(repository);
    Loader load = [&](const std::string& name) { return reversed.at(name); };
    check(build_graph("D", load, 5, "").size() == 5, "reverse transitive dependencies");
    check(build_graph("D", load, 1, "").size() == 3, "reverse depth");
    check(!build_graph("D", load, 5, "B").count("B"), "reverse filter");
    check(build_graph("F", load, 5, "").size() == 1, "no reverse dependencies");
    reversed = reverse_graph({{"A", {"B"}}, {"B", {"A"}}});
    check(build_graph("A", load, 100, "").size() == 2, "reverse cycle");
}
void diagram_tests() {
    std::string text = d2_text({{"a:b:1", {"x:y:2"}}, {"x:y:2", {}}});
    check(text.find("n0: \"a:b:1\"") != std::string::npos, "Maven D2 label");
    check(text.find("n0 -> n1") != std::string::npos, "D2 edge");
    check(d2_text({}).find("Graph is empty") != std::string::npos, "empty diagram");
    check(d2_text({{"A", {"A"}}}).find("n0 -> n0") != std::string::npos, "D2 self cycle");
    check(d2_text({{"A\"B", {}}}).find("A\\\"B") != std::string::npos, "escaped D2 label");
}
int main() {
    try {
        json_tests();
        maven_tests();
        graph_tests();
        reverse_tests();
        diagram_tests();
        config_tests();
        std::cout << "Passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
