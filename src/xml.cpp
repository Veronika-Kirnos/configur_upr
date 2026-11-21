#include "app.h"
#include <windows.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <stdexcept>

struct XmlNode {
    std::string name, text;
    std::vector<int> children;
};

static std::vector<XmlNode> xml_nodes(const std::string& xml) {
    IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), xml.size());
    IXmlReader* reader = nullptr;
    if (!stream) throw std::runtime_error("Cannot create XML stream");
    HRESULT result = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr);
    if (FAILED(result)) { stream->Release(); throw std::runtime_error("Cannot create XML reader"); }
    result = reader->SetInput(stream);
    std::vector<XmlNode> nodes(1);
    std::vector<int> stack{0};
    XmlNodeType type;
    while (SUCCEEDED(result) && (result = reader->Read(&type)) == S_OK) {
        const WCHAR* value = nullptr;
        UINT length = 0;
        if (type == XmlNodeType_Element) {
            reader->GetLocalName(&value, &length);
            int index = static_cast<int>(nodes.size());
            nodes.push_back({utf8(std::wstring(value, length)), "", {}});
            nodes[stack.back()].children.push_back(index);
            if (!reader->IsEmptyElement()) stack.push_back(index);
        } else if (type == XmlNodeType_EndElement) {
            if (stack.size() > 1) stack.pop_back();
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA) {
            reader->GetValue(&value, &length);
            nodes[stack.back()].text += utf8(std::wstring(value, length));
        }
    }
    reader->Release();
    stream->Release();
    if (FAILED(result) || nodes[0].children.size() != 1 || nodes[1].name != "project")
        throw std::runtime_error("Invalid POM XML: expected project root");
    return nodes;
}

static int child(const std::vector<XmlNode>& nodes, int index, const std::string& name) {
    for (int next : nodes[index].children) if (nodes[next].name == name) return next;
    return 0;
}

static std::string text(const std::vector<XmlNode>& nodes, int index, const std::string& name) {
    std::string value = nodes[child(nodes, index, name)].text;
    size_t first = value.find_first_not_of(" \r\n\t");
    return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \r\n\t") - first + 1);
}

static std::vector<Dependency> dependencies(const std::vector<XmlNode>& nodes, int index) {
    std::vector<Dependency> result;
    if (!index) return result;
    for (int next : nodes[index].children) {
        if (nodes[next].name != "dependency") continue;
        result.push_back({text(nodes, next, "groupId"), text(nodes, next, "artifactId"),
                          text(nodes, next, "version"), text(nodes, next, "scope"),
                          text(nodes, next, "optional"), text(nodes, next, "type"),
                          text(nodes, next, "classifier")});
    }
    return result;
}

Pom parse_pom(const std::string& xml) {
    auto nodes = xml_nodes(xml);
    Pom pom;
    int parent = child(nodes, 1, "parent");
    if (parent) pom.parent = text(nodes, parent, "groupId") + ":" + text(nodes, parent, "artifactId")
                          + ":" + text(nodes, parent, "version");
    int properties = child(nodes, 1, "properties");
    if (properties) for (int next : nodes[properties].children) {
        std::string value = nodes[next].text;
        size_t start = value.find_first_not_of(" \r\n\t");
        if (start != std::string::npos) value = value.substr(start, value.find_last_not_of(" \r\n\t") - start + 1);
        pom.properties[nodes[next].name] = value;
    }
    pom.dependencies = dependencies(nodes, child(nodes, 1, "dependencies"));
    int management = child(nodes, 1, "dependencyManagement");
    pom.managed = dependencies(nodes, child(nodes, management, "dependencies"));
    return pom;
}
