#include "server_manifest.hpp"
#include <windows.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace fetcher {
namespace {
void need(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error("FOMOD: " + message);
}
std::string label(const Json &value) {
    auto text = value.get<std::string>();
    need(!text.empty() && text.size() <= 256 &&
             std::none_of(text.begin(), text.end(),
                          [](unsigned char c) { return c < 32 || c == 127; }),
         "invalid step, group or option name.");
    return text;
}
std::string pathName(std::string path, bool root = false) {
    std::replace(path.begin(), path.end(), '\\', '/');
    need(path.empty() || path.front() != '/', "absolute installer paths are forbidden.");
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    if (root && (path.empty() || path == "."))
        return "";
    auto relative = safeRelative(path);
    for (const auto &part : relative)
        need(lower(utf8(part.wstring())).rfind(".fetcher-", 0) != 0,
             "installer paths cannot address launcher metadata.");
    return path;
}
struct Node {
    std::string name, text;
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<Node>> children;
    std::string attr(const std::string &key, const std::string &fallback = "") const {
        auto found = attrs.find(key);
        return found == attrs.end() ? fallback : found->second;
    }
    const Node *child(const std::string &key) const {
        const Node *result = nullptr;
        for (const auto &c : children)
            if (c->name == key) {
                need(!result, "duplicate " + key + " in " + name + ".");
                result = c.get();
            }
        return result;
    }
    const Node &required(const std::string &key) const {
        auto result = child(key);
        need(result, "missing " + key + " in " + name + ".");
        return *result;
    }
};
std::unique_ptr<Node> parseXml(const fs::path &path) {
    auto bytes = read(path, 4 * 1024 * 1024);
    using Microsoft::WRL::ComPtr;
    ComPtr<IStream> input;
    input.Attach(
        SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.data()), UINT(bytes.size())));
    ComPtr<IXmlReader> reader;
    need(input &&
             SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader),
                                       reinterpret_cast<void **>(reader.GetAddressOf()), nullptr)),
         "could not initialize native XML reader.");
    need(SUCCEEDED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) &&
             SUCCEEDED(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 32)) &&
             SUCCEEDED(reader->SetProperty(XmlReaderProperty_XmlResolver, 0)) &&
             SUCCEEDED(reader->SetInput(input.Get())),
         "could not configure bounded XML reader.");
    std::unique_ptr<Node> root;
    std::vector<Node *> stack;
    XmlNodeType kind;
    HRESULT status;
    std::size_t nodes = 0;
    while ((status = reader->Read(&kind)) == S_OK) {
        need(++nodes <= 100000, "too many XML nodes.");
        if (kind == XmlNodeType_Element) {
            auto node = std::make_unique<Node>();
            const wchar_t *value;
            need(SUCCEEDED(reader->GetNamespaceUri(&value, nullptr)) && !*value,
                 "namespaced installer elements are unsupported.");
            reader->GetLocalName(&value, nullptr);
            node->name = utf8(value);
            bool empty = reader->IsEmptyElement() != FALSE;
            auto attrStatus = reader->MoveToFirstAttribute();
            while (attrStatus == S_OK) {
                const wchar_t *ns, *name, *text;
                reader->GetNamespaceUri(&ns, nullptr);
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&text, nullptr);
                if (!*ns)
                    need(node->attrs.emplace(utf8(name), utf8(text)).second,
                         "duplicate XML attribute.");
                else
                    need(std::wstring(ns) == L"http://www.w3.org/2000/xmlns/" ||
                             (stack.empty() &&
                              std::wstring(ns) == L"http://www.w3.org/2001/XMLSchema-instance" &&
                              std::wstring(name) == L"noNamespaceSchemaLocation"),
                         "unsupported XML attribute namespace.");
                attrStatus = reader->MoveToNextAttribute();
            }
            need(attrStatus == S_FALSE, "invalid XML attributes.");
            reader->MoveToElement();
            auto current = node.get();
            if (stack.empty()) {
                need(!root, "multiple document roots.");
                root = std::move(node);
            } else
                stack.back()->children.push_back(std::move(node));
            if (!empty)
                stack.push_back(current);
        } else if (kind == XmlNodeType_EndElement) {
            need(!stack.empty(), "unexpected closing element.");
            stack.pop_back();
        } else if (kind == XmlNodeType_Text || kind == XmlNodeType_CDATA ||
                   kind == XmlNodeType_Whitespace) {
            const wchar_t *text;
            reader->GetValue(&text, nullptr);
            if (!stack.empty())
                stack.back()->text += utf8(text);
        } else
            need(kind == XmlNodeType_Comment || kind == XmlNodeType_XmlDeclaration,
                 "DTD, entities and processing instructions are unsupported.");
    }
    need(status == S_FALSE && root && stack.empty(), "malformed or prohibited XML.");
    return root;
}
void shape(const Node &node, const std::set<std::string> &attrs,
           const std::set<std::string> &children, bool text = false) {
    for (const auto & [ key, unused ] : node.attrs)
        need(attrs.count(key), "unsupported attribute " + key + " on " + node.name + ".");
    for (const auto &child : node.children)
        need(children.count(child->name), "unsupported " + child->name + " in " + node.name + ".");
    if (!text)
        need(node.text.find_first_not_of(" \r\n\t") == std::string::npos,
             "unexpected text in " + node.name + ".");
}
bool boolean(const Node &node, const std::string &name) {
    auto value = node.attr(name, "false");
    need(value == "true" || value == "false" || value == "1" || value == "0",
         "invalid boolean " + name + ".");
    return value == "true" || value == "1";
}
std::int64_t priority(const Node &node) {
    auto value = node.attr("priority", "0");
    need(!value.empty() && value.size() <= 11, "invalid file priority.");
    std::size_t end = 0;
    auto result = std::stoll(value, &end);
    need(end == value.size() && result >= INT_MIN && result <= INT_MAX, "invalid file priority.");
    return result;
}
void validateTree(const Node &n) {
    const auto &tag = n.name;
    if (tag == "config") {
        shape(n, {}, {"moduleName", "moduleImage", "moduleDependencies", "requiredInstallFiles",
                      "installSteps", "conditionalFileInstalls"});
        n.required("moduleName");
        for (const auto &child : n.children)
            n.child(child->name); // Top-level sections must be unique.
    } else if (tag == "moduleName")
        shape(n, {"position", "colour"}, {}, true);
    else if (tag == "description")
        shape(n, {}, {}, true);
    else if (tag == "moduleImage" || tag == "image")
        shape(n, {"path", "showImage", "showFade", "height"},
              {}); // Display metadata, never opened.
    else if (tag == "moduleDependencies" || tag == "dependencies" || tag == "visible") {
        shape(n, {"operator"}, {"dependencies", "flagDependency"});
        need(!n.children.empty() &&
                 (n.attr("operator", "And") == "And" || n.attr("operator") == "Or"),
             "invalid dependency expression.");
    } else if (tag == "flagDependency") {
        shape(n, {"flag", "value"}, {});
        label(n.attr("flag"));
        need(n.attrs.count("value"), "flag dependency needs a value.");
    } else if (tag == "installSteps" || tag == "optionalFileGroups" || tag == "plugins") {
        shape(n, {"order"},
              {tag == "installSteps" ? "installStep" : tag == "plugins" ? "plugin" : "group"});
        auto order = n.attr("order", "Ascending");
        need(order == "Explicit" || order == "Ascending" || order == "Descending",
             "invalid ordering.");
        std::set<std::string> names;
        for (const auto &child : n.children)
            need(names.insert(lower(label(child->attr("name")))).second,
                 "duplicate step/group/option name.");
    } else if (tag == "installStep") {
        shape(n, {"name"}, {"visible", "optionalFileGroups"});
        n.child("visible");
        n.required("optionalFileGroups");
    } else if (tag == "group") {
        shape(n, {"name", "type"}, {"plugins"});
        const std::set<std::string> types = {"SelectAny", "SelectAll", "SelectExactlyOne",
                                             "SelectAtLeastOne", "SelectAtMostOne"};
        need(types.count(n.attr("type")), "unsupported group type.");
        need(!n.required("plugins").children.empty(), "empty option group.");
    } else if (tag == "plugin") {
        shape(n, {"name"}, {"description", "image", "files", "conditionFlags", "typeDescriptor"});
        for (auto key : {"description", "image", "files", "conditionFlags"})
            n.child(key);
        n.required("typeDescriptor");
    } else if (tag == "typeDescriptor") {
        shape(n, {}, {"type", "dependencyType"});
        need(n.children.size() == 1, "option requires exactly one type descriptor.");
    } else if (tag == "type" || tag == "defaultType") {
        shape(n, {"name"}, {});
        const std::set<std::string> types = {"Required", "Optional", "Recommended", "NotUsable",
                                             "CouldBeUsable"};
        need(types.count(n.attr("name")), "unsupported option type.");
    } else if (tag == "dependencyType") {
        shape(n, {}, {"defaultType", "patterns"});
        n.required("defaultType");
        for (const auto &pattern : n.required("patterns").children) {
            pattern->required("dependencies");
            pattern->required("type");
            need(!pattern->child("files"), "file pattern in option type descriptor.");
        }
    } else if (tag == "requiredInstallFiles" || tag == "files")
        shape(n, {}, {"file", "folder"});
    else if (tag == "file" || tag == "folder") {
        shape(n, {"source", "destination", "priority", "alwaysInstall", "installIfUsable"}, {});
        pathName(n.attr("source"));
        pathName(n.attr("destination", n.attr("source")), true);
        priority(n);
        boolean(n, "alwaysInstall");
        boolean(n, "installIfUsable");
    } else if (tag == "conditionFlags") {
        shape(n, {}, {"flag"});
        std::set<std::string> flags;
        for (const auto &flag : n.children)
            need(flags.insert(flag->attr("name")).second, "duplicate option flag.");
    } else if (tag == "flag") {
        shape(n, {"name"}, {}, true);
        label(n.attr("name"));
    } else if (tag == "conditionalFileInstalls") {
        shape(n, {}, {"patterns"});
        for (const auto &pattern : n.required("patterns").children) {
            pattern->required("dependencies");
            pattern->required("files");
            need(!pattern->child("type"), "type pattern in conditional files.");
        }
    } else if (tag == "patterns")
        shape(n, {}, {"pattern"});
    else if (tag == "pattern")
        shape(n, {}, {"dependencies", "type", "files"});
    else
        need(false, "unsupported installer element " + tag +
                        ". Use a curated file layout for this installer.");
    for (const auto &child : n.children)
        validateTree(*child);
}
using Flags = std::map<std::string, std::string>;
bool matches(const Node &n, const Flags &flags) {
    if (n.name == "flagDependency") {
        auto found = flags.find(n.attr("flag"));
        return found != flags.end() && found->second == n.attr("value");
    }
    bool conjunction = n.attr("operator", "And") == "And";
    bool result = conjunction;
    for (const auto &child : n.children) {
        auto value = matches(*child, flags);
        result = conjunction ? result && value : result || value;
    }
    return result;
}
std::vector<const Node *> ordered(const Node &n) {
    std::vector<const Node *> result;
    for (const auto &child : n.children)
        result.push_back(child.get());
    auto order = n.attr("order", "Ascending");
    if (order != "Explicit")
        std::stable_sort(result.begin(), result.end(), [&](const Node *a, const Node *b) {
            auto left = lower(a->attr("name")), right = lower(b->attr("name"));
            return order == "Ascending" ? left < right : left > right;
        });
    return result;
}
std::string optionType(const Node &n, const Flags &flags) {
    auto &descriptor = n.required("typeDescriptor");
    if (auto type = descriptor.child("type"))
        return type->attr("name");
    auto &dependent = descriptor.required("dependencyType");
    for (const auto &pattern : dependent.required("patterns").children)
        if (matches(pattern->required("dependencies"), flags))
            return pattern->required("type").attr("name");
    return dependent.required("defaultType").attr("name");
}
} // namespace

Json normalizeFomod(const Json &value) {
    need(value.is_object() && value.size() == 2 && value.contains("config") &&
             value.contains("selections"),
         "specify only config and selections.");
    auto config = pathName(value.at("config").get<std::string>());
    auto path = safeRelative(config);
    need(lower(utf8(path.filename().wstring())) == "moduleconfig.xml" &&
             lower(utf8(path.parent_path().filename().wstring())) == "fomod",
         "config must name an archive-relative fomod/ModuleConfig.xml.");
    const auto &choices = value.at("selections");
    need(choices.is_array() && choices.size() <= 256, "too many selected groups.");
    Json result = {{"config", config}, {"selections", Json::array()}};
    std::set<std::pair<std::string, std::string>> groups;
    std::size_t total = 0;
    for (const auto &choice : choices) {
        need(choice.is_object() && choice.size() == 3 && choice.contains("step") &&
                 choice.contains("group") && choice.contains("plugins"),
             "invalid selected group.");
        auto step = label(choice.at("step")), group = label(choice.at("group"));
        need(groups.emplace(step, group).second, "duplicate selected group.");
        auto plugins = choice.at("plugins");
        need(plugins.is_array() && plugins.size() <= 128 && (total += plugins.size()) <= 2048,
             "too many selected options.");
        std::set<std::string> names;
        for (auto &plugin : plugins)
            need(names.insert(label(plugin)).second, "duplicate selected option.");
        std::sort(plugins.begin(), plugins.end());
        result["selections"].push_back({{"step", step}, {"group", group}, {"plugins", plugins}});
    }
    auto &out = result["selections"];
    std::sort(out.begin(), out.end(), [](const Json &a, const Json &b) {
        return std::make_pair(a.at("step"), a.at("group")) <
               std::make_pair(b.at("step"), b.at("group"));
    });
    return result;
}

void installFomod(const fs::path &raw, const fs::path &stage, const Json &value) {
    auto spec = normalizeFomod(value);
    std::set<std::string> verified;
    auto receipt = readPackageReceipt(raw);
    for (const auto &record : receipt.at("files")) {
        auto name = record.at("path").get<std::string>();
        safeRelative(name);
        verified.insert(lower(name));
    }
    need(verified.count(lower(spec.at("config"))),
         "config is absent from the verified archive receipt.");
    auto config = raw / safeRelative(spec.at("config"));
    noLinks(config);
    auto root = config.parent_path().parent_path();
    auto tree = parseXml(config);
    need(tree->name == "config", "expected XML config root.");
    validateTree(
        *tree); // Validate even hidden/unselected branches before performing file operations.
    Flags flags;
    if (auto dependency = tree->child("moduleDependencies"))
        need(matches(*dependency, flags), "module dependencies are not satisfied.");
    std::vector<const Node *> files;
    auto queue = [&](const Node *list, bool selected = true, bool usable = true) {
        if (list)
            for (const auto &file : list->children)
                if (selected || boolean(*file, "alwaysInstall") ||
                    (usable && boolean(*file, "installIfUsable")))
                    files.push_back(file.get());
    };
    queue(tree->child("requiredInstallFiles"));
    std::set<std::pair<std::string, std::string>> consumed;
    if (auto steps = tree->child("installSteps"))
        for (auto step : ordered(*steps)) {
            if (auto visible = step->child("visible"); visible && !matches(*visible, flags))
                continue;
            Flags stepFlags;
            for (auto group : ordered(step->required("optionalFileGroups"))) {
                const Json *selection = nullptr;
                auto key = std::make_pair(step->attr("name"), group->attr("name"));
                for (const auto &choice : spec.at("selections"))
                    if (choice.at("step") == key.first && choice.at("group") == key.second)
                        selection = &choice.at("plugins");
                need(selection, "pin a selection (including an empty list) for " + key.first +
                                    " / " + key.second + ".");
                consumed.insert(key);
                std::set<std::string> selected = selection->get<std::set<std::string>>(), known;
                auto options = ordered(group->required("plugins"));
                for (auto option : options) {
                    auto name = option->attr("name"), type = optionType(*option, flags);
                    known.insert(name);
                    bool picked = selected.count(name) != 0;
                    need(!picked || type != "NotUsable",
                         "selected option is not usable: " + name + ".");
                    need(type != "Required" || picked,
                         "required option is not selected: " + name + ".");
                    queue(option->child("files"), picked, type != "NotUsable");
                    if (picked)
                        if (auto values = option->child("conditionFlags"))
                            for (const auto &flag : values->children) {
                                auto[found, inserted] =
                                    stepFlags.emplace(flag->attr("name"), flag->text);
                                need(inserted || found->second == flag->text,
                                     "conflicting flags within one step.");
                            }
                }
                for (const auto &name : selected)
                    need(known.count(name), "unknown selected option: " + name + ".");
                auto type = group->attr("type");
                need((type != "SelectExactlyOne" || selected.size() == 1) &&
                         (type != "SelectAtMostOne" || selected.size() <= 1) &&
                         (type != "SelectAtLeastOne" || !selected.empty()) &&
                         (type != "SelectAll" || selected.size() == options.size()),
                     "selection violates " + type + ": " + key.second + ".");
            }
            for (const auto & [ key, val ] : stepFlags)
                flags[key] = val;
        }
    need(consumed.size() == spec.at("selections").size(),
         "selection refers to a hidden or unknown group.");
    if (auto conditional = tree->child("conditionalFileInstalls"))
        for (const auto &pattern : conditional->required("patterns").children)
            if (matches(pattern->required("dependencies"), flags))
                queue(pattern->child("files"));
    need(files.size() <= 100000, "too many file directives.");
    std::stable_sort(files.begin(), files.end(),
                     [](const Node *a, const Node *b) { return priority(*a) < priority(*b); });
    struct Output {
        fs::path source, destination;
        bool directory;
    };
    std::vector<Output> output;
    std::uint64_t bytes = 0;
    auto add = [&](const fs::path &source, const fs::path &relative, bool directory) {
        noLinks(source);
        auto name = utf8(relative.generic_wstring());
        if (name.empty())
            return;
        pathName(name);
        need(directory || fs::is_regular_file(source), "source is not a regular file.");
        if (!directory) {
            need(verified.count(lower(utf8(source.lexically_relative(raw).generic_wstring()))),
                 "source is absent from the verified archive receipt.");
            auto size = fs::file_size(source);
            need(size <= 8ull * 1024 * 1024 * 1024 - bytes, "output expansion exceeds limit.");
            bytes += size;
        }
        need(output.size() < 100000, "too many output entries.");
        output.push_back({source, relative, directory});
    };
    for (auto directive : files) {
        auto sourceName = pathName(directive->attr("source"));
        auto source = root / safeRelative(sourceName);
        noLinks(source);
        bool folder = directive->name == "folder";
        need(folder ? fs::is_directory(source) : fs::is_regular_file(source),
             "missing source: " + sourceName + ".");
        auto destinationText = directive->attr("destination", sourceName);
        auto destination = pathName(destinationText, true);
        auto target = destination.empty() ? fs::path() : safeRelative(destination);
        if (!folder && (destination.empty() || destinationText.back() == '/' ||
                        destinationText.back() == '\\'))
            target /= source.filename();
        add(source, target, folder);
        if (folder) {
            std::vector<fs::path> entries;
            for (const auto &entry : fs::recursive_directory_iterator(source)) {
                noLinks(entry.path());
                entries.push_back(entry.path());
                need(entries.size() <= 100000, "too many folder entries.");
            }
            std::sort(entries.begin(), entries.end());
            for (const auto &entry : entries)
                add(entry, target / entry.lexically_relative(source), fs::is_directory(entry));
        }
    }
    need(!output.empty(), "installer produced no output.");
    noLinks(stage);
    need(fs::is_directory(stage) && fs::is_empty(stage), "output stage must be empty.");
    for (const auto &entry : output) {
        auto target = stage / entry.destination;
        noLinks(target);
        if (entry.directory) {
            need(!fs::exists(target) || fs::is_directory(target),
                 "file/directory output collision.");
            fs::create_directories(target);
        } else {
            need(!fs::exists(target) || fs::is_regular_file(target),
                 "file/directory output collision.");
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.source, target, fs::copy_options::overwrite_existing);
        }
    }
}
} // namespace fetcher
