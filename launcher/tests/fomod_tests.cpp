#include "server_manifest.hpp"
#include <windows.h>
#include <iostream>
using namespace fetcher;
namespace {
int assertions = 0;
void check(bool ok, const char *message) {
    ++assertions;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
void replace(std::string &text, const std::string &from, const std::string &to) {
    auto pos = text.find(from);
    check(pos != std::string::npos, "test replacement exists");
    text.replace(pos, from.size(), to);
}
}
int main() {
    auto root = fs::temp_directory_path() /
                (L"fetcher-fomod-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        noLinks(root);
        auto raw = root / "raw", stage = root / "result";
        fs::create_directories(stage);
        auto fixture = read(fs::u8path(FETCHER_FOMOD_FIXTURES) / "ModuleConfig.xml");
        auto config = raw / "Wrapper/fomod/ModuleConfig.xml";
        writeAtomic(config, fixture);
        auto package = config.parent_path().parent_path();
        for (auto name :
             {"core/Instrument.esp", "core/textures/tone.dds", "options/modern/tone.dds",
              "options/classic/tone.dds", "patch/Patch.esp", "patch/final.dds", "docs/readme.txt",
              "docs/usable.txt", "docs/disabled.txt"})
            writeAtomic(package / name, name);
        writeAtomic(package / L"options/modern/\u00e9.dds", "Unicode texture");
        Json records = Json::array();
        for (const auto &entry : fs::recursive_directory_iterator(raw))
            if (entry.is_regular_file())
                records.push_back(
                    {{"path", utf8(entry.path().lexically_relative(raw).generic_wstring())}});
        writeAtomic(raw / ".fetcher-package.json", Json{{"files", records}}.dump());
        auto spec = Json::parse(R"({"config":"Wrapper/fomod/ModuleConfig.xml","selections":[
          {"step":"Sound","group":"Variant","plugins":["Modern"]},
          {"step":"Patches","group":"Extras","plugins":["Patch"]}]})");
        installFomod(raw, stage, spec);
        check(read(stage / "textures/tone.dds") == "patch/final.dds",
              "FOMOD priorities and nested flag condition");
        check(read(stage / L"textures/\u00e9.dds") == "Unicode texture", "Unicode file mapping");
        check(fs::exists(stage / "Instrument.esp") && fs::exists(stage / "Patch.esp"),
              "required and selected files");
        check(fs::exists(stage / "docs/readme.txt") && fs::exists(stage / "docs/usable.txt") &&
                  !fs::exists(stage / "disabled.txt"),
              "alwaysInstall/installIfUsable and unavailable option");
        check(!fs::exists(stage / "options") && !fs::exists(stage / "fomod"),
              "unselected package trees excluded");
        check(read(package / "core/textures/tone.dds") == "core/textures/tone.dds",
              "raw archive unchanged");
        writeAtomic(package / "options/modern/untracked.dds", "not from the archive");
        auto untrackedStage = root / "untracked";
        fs::create_directories(untrackedStage);
        rejects([&] { installFomod(raw, untrackedStage, spec); }, "unreceipted raw files excluded");
        check(fs::is_empty(untrackedStage), "unreceipted source rejection writes nothing");
        removeManaged(raw, package / "options/modern/untracked.dds");
        auto classic = spec;
        classic["selections"] = Json::array(
            {{{"step", "Sound"}, {"group", "Variant"}, {"plugins", Json::array({"Classic"})}}});
        auto classicStage = root / "classic";
        fs::create_directories(classicStage);
        installFomod(raw, classicStage, classic);
        check(read(classicStage / "textures/tone.dds") == "options/classic/tone.dds" &&
                  !fs::exists(classicStage / "Patch.esp"),
              "hidden step excluded");
        check(modLayoutKey({{"fomod", spec}}, std::string(64, 'a')) !=
                  modLayoutKey({{"fomod", classic}}, std::string(64, 'a')),
              "selection changes cache identity");
        auto reversed = spec;
        std::reverse(reversed["selections"].begin(), reversed["selections"].end());
        check(normalizeFomod(reversed) == normalizeFomod(spec), "selection order canonicalized");
        auto badChoice = [&](Json selected) {
            auto out = root / ("bad-" + std::to_string(assertions));
            fs::create_directories(out);
            rejects([&] { installFomod(raw, out, selected); }, "invalid selection rejected");
            check(fs::is_empty(out), "selection failure writes no output");
        };
        auto bad = spec;
        bad["selections"][0]["plugins"] = Json::array();
        badChoice(bad);
        bad = spec;
        bad["selections"][0]["plugins"] = Json::array({"Modern", "Classic"});
        badChoice(bad);
        bad = spec;
        bad["selections"][0]["plugins"] = Json::array({"Missing"});
        badChoice(bad);
        bad = spec;
        bad["selections"][1]["plugins"] = Json::array();
        badChoice(bad);
        bad = spec;
        bad["selections"][1]["plugins"] = Json::array({"Patch", "Unavailable"});
        badChoice(bad);
        bad = spec;
        bad["selections"][0]["plugins"] = Json::array({"Classic"});
        badChoice(bad);
        bad = spec;
        bad["selections"].push_back(bad["selections"][0]);
        rejects([&] { normalizeFomod(bad); }, "duplicate group rejected");
        bad = spec;
        bad["config"] = "../fomod/ModuleConfig.xml";
        rejects([&] { normalizeFomod(bad); }, "unsafe config rejected");
        bad = spec;
        bad["command"] = "execute";
        rejects([&] { normalizeFomod(bad); }, "unknown recipe field rejected");
        auto badXml = [&](std::string xml) {
            writeAtomic(config, xml);
            auto out = root / ("xml-" + std::to_string(assertions));
            fs::create_directories(out);
            rejects([&] { installFomod(raw, out, spec); }, "invalid or unsupported XML rejected");
            check(fs::is_empty(out), "invalid XML writes no output");
            writeAtomic(config, fixture);
        };
        auto xml = fixture;
        replace(xml, "options\\modern", "../outside");
        badXml(xml);
        xml = fixture;
        replace(xml, "destination=\"textures\"", "destination=\"C:\\outside\"");
        badXml(xml);
        xml = fixture;
        replace(xml, "destination=\"textures\"", "destination=\".fetcher-package.json\"");
        badXml(xml);
        xml = fixture;
        replace(xml, "<visible><flagDependency flag=\"voice\" value=\"modern\" /></visible>",
                "<visible><fileDependency file=\"other.esp\" state=\"Active\" /></visible>");
        badXml(xml);
        xml = fixture;
        replace(xml, "<type name=\"NotUsable\" />", "<execute command=\"bad\" />");
        badXml(xml);
        xml = fixture;
        replace(
            xml, "<config ",
            "<!DOCTYPE config [<!ENTITY external SYSTEM 'http://127.0.0.1:9/private'>]><config ");
        badXml(xml);
        xml = fixture;
        replace(xml, "<moduleName>Fetcher authored FOMOD test</moduleName>",
                "<moduleName>&undefined;</moduleName>");
        badXml(xml);
        xml = fixture;
        replace(xml, "<type name=\"Recommended\" />",
                "<type name=\"Optional\"/><type name=\"Required\"/>");
        badXml(xml);
        xml = fixture;
        replace(xml, "priority=\"100\"", "priority=\"2oops\"");
        badXml(xml);
        xml = fixture;
        replace(xml, "alwaysInstall=\"true\"", "alwaysInstall=\"maybe\"");
        badXml(xml);
        xml = fixture;
        replace(xml, "source=\"patch/final.dds\"", "source=\"missing.dds\"");
        badXml(xml);
        badXml("<config><moduleName>incomplete");
        std::string deep = "<config><moduleName>deep</moduleName><moduleDependencies>";
        for (int i = 0; i < 40; ++i)
            deep += "<dependencies>";
        deep += "<flagDependency flag=\"x\" value=\"x\"/>";
        for (int i = 0; i < 40; ++i)
            deep += "</dependencies>";
        badXml(deep + "</moduleDependencies></config>");
        // Exercise the other group constraints independently of the exactly-one fixture.
        for (auto type : {"SelectAll", "SelectAtLeastOne", "SelectAtMostOne"}) {
            xml = fixture;
            replace(xml, "type=\"SelectExactlyOne\"", std::string("type=\"") + type + "\"");
            writeAtomic(config, xml);
            bad = classic;
            bad["selections"][0]["plugins"] = std::string(type) == "SelectAtMostOne"
                                                  ? Json::array({"Modern", "Classic"})
                                                  : Json::array();
            badChoice(bad);
        }
        writeAtomic(config, fixture);
        // Small independent plan: all group types accept their valid boundary counts.
        for (auto type : {"SelectAny", "SelectAll", "SelectExactlyOne", "SelectAtLeastOne",
                          "SelectAtMostOne"}) {
            std::string simple =
                "<config><moduleName>Groups</moduleName><requiredInstallFiles>"
                "<file source=\"docs/readme.txt\" destination=\"readme.txt\"/>"
                "</requiredInstallFiles><installSteps><installStep name=\"Step\">"
                "<optionalFileGroups><group name=\"Group\" type=\"" +
                std::string(type) +
                "\"><plugins order=\"Explicit\">"
                "<plugin name=\"A\"><files><file source=\"options/modern/tone.dds\" "
                "destination=\"tone.dds\"/></files>"
                "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
                "<plugin name=\"B\"><files><file source=\"options/classic/tone.dds\" "
                "destination=\"tone.dds\"/></files>"
                "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
                "</plugins></group></optionalFileGroups></installStep></installSteps></config>";
            auto picked =
                std::string(type) == "SelectAll"
                    ? Json::array({"A", "B"})
                    : std::string(type) == "SelectAtMostOne" ? Json::array() : Json::array({"A"});
            auto selected = spec;
            selected["selections"] =
                Json::array({{{"step", "Step"}, {"group", "Group"}, {"plugins", picked}}});
            writeAtomic(config, simple);
            auto out = root / type;
            fs::create_directories(out);
            installFomod(raw, out, selected);
            check(fs::exists(out / "readme.txt"), "valid group cardinality accepted");
            if (std::string(type) == "SelectAll")
                check(read(out / "tone.dds") == "options/classic/tone.dds",
                      "equal-priority explicit later option wins");
        }
        writeAtomic(
            config,
            "<?xml version=\"1.0\" encoding=\"utf-16\"?><config><moduleName>UTF16</moduleName>"
            "<requiredInstallFiles><file source=\"docs/readme.txt\" "
            "destination=\"readme.txt\"/></requiredInstallFiles></config>");
        // Write a genuine UTF-16LE document, not just an encoding label on UTF-8 bytes.
        auto utf16 = wide(read(config));
        std::string utf16Bytes("\xff\xfe", 2);
        utf16Bytes.append(reinterpret_cast<const char *>(utf16.data()),
                          utf16.size() * sizeof(wchar_t));
        writeAtomic(config, utf16Bytes);
        auto utf16Stage = root / "utf16";
        fs::create_directories(utf16Stage);
        auto noGroups = spec;
        noGroups["selections"] = Json::array();
        installFomod(raw, utf16Stage, noGroups);
        check(read(utf16Stage / "readme.txt") == "docs/readme.txt", "UTF-16LE native XML decoding");
        writeAtomic(config, fixture);
        removeManaged(fs::temp_directory_path(), root);
        std::cout << "PASS native FOMOD: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
