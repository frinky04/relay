#include "lua_commands.h"
#include "util.h"
#include <urlmon.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <sol/sol.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {
void checkFields(sol::table table, std::initializer_list<std::string_view> allowed) {
    for (auto [key, value] : table) {
        if (key.get_type() != sol::type::string ||
            std::find(allowed.begin(), allowed.end(), key.as<std::string>()) == allowed.end())
            throw std::runtime_error("Unknown field; compare the command with a bundled plugin");
    }
}
std::string readString(sol::object object) {
    if (object.get_type() != sol::type::string) throw std::runtime_error("Expected a string; compare the command with a bundled plugin");
    auto value = object.as<std::string>();
    if (value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos)
        throw std::runtime_error("Use single-line command metadata");
    return value;
}
sol::table readTable(sol::object object) {
    if (object.get_type() != sol::type::table) throw std::runtime_error("Expected a table; compare the command with a bundled plugin");
    return object.as<sol::table>();
}
sol::table readArray(sol::object object) {
    auto result = readTable(object);
    const size_t length = result.size();
    size_t count = 0;
    for (auto [key, value] : result) {
        if (!key.is<int>() || key.as<int>() < 1 || key.as<size_t>() > length)
            throw std::runtime_error("Use consecutive array entries starting at 1");
        ++count;
    }
    if (count != length) throw std::runtime_error("Remove gaps from the array");
    return result;
}
bool readBoolean(sol::object object) {
    if (object.get_type() == sol::type::nil) return false;
    if (!object.is<bool>()) throw std::runtime_error("Use true or false for boolean fields");
    return object.as<bool>();
}
struct LuaFunction {
    std::shared_ptr<sol::state> state;
    sol::protected_function run; // destroyed before its state
};

std::shared_ptr<LuaFunction> readFunction(const std::shared_ptr<sol::state>& lua, sol::object value) {
    if (value.get_type() != sol::type::function) throw std::runtime_error("Supply a function for the callback");
    return std::make_shared<LuaFunction>(LuaFunction{lua, value.as<sol::protected_function>()});
}

void checkResult(const sol::protected_function_result& result) {
    if (!result.valid()) { sol::error error = result; throw std::runtime_error(error.what()); }
}

bool webUrl(const std::string& url) {
    // Validate the same complete string handed to the shell, including NULs.
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; })) return false;
    Microsoft::WRL::ComPtr<IUri> uri;
    if (FAILED(CreateUri(widen(url).c_str(), Uri_CREATE_CANONICALIZE, 0, &uri))) return false;
    DWORD scheme = 0, hostLength = 0;
    return SUCCEEDED(uri->GetScheme(&scheme)) && (scheme == URL_SCHEME_HTTP || scheme == URL_SCHEME_HTTPS) &&
        SUCCEEDED(uri->GetPropertyLength(Uri_PROPERTY_HOST, &hostLength, 0)) && hostLength != 0;
}
}

command::Command loadLuaCommand(const std::filesystem::path& path,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string(const std::string&)> openUrl) {
    auto lua = std::make_shared<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8);
    auto host = lua->create_named_table("host");
    auto executing = std::make_shared<bool>(false);
    host.set_function("copy", [executing, copy = std::move(copy)](const std::string& text) -> sol::optional<std::string> {
        if (!*executing) throw std::runtime_error("Call host.copy only from a verb's run function");
        auto error = copy(text);
        if (error.empty()) return sol::nullopt;
        return error;
    });
    host.set_function("open_url", [executing, openUrl = std::move(openUrl)](const std::string& url) -> sol::optional<std::string> {
        if (!*executing) throw std::runtime_error("Call host.open_url only from a verb's run function");
        if (!webUrl(url)) return std::string("Use a complete http:// or https:// URL with a host and encode spaces");
        auto error = openUrl(url);
        if (error.empty()) return sol::nullopt;
        return error;
    });
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read plugin; check file permissions and run /relay Reload Plugins");
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    auto result = lua->safe_script(source, sol::script_pass_on_error, path.string());
    checkResult(result);
    if (!result.return_count()) throw std::runtime_error("Return a command table from the plugin");
    auto root = readTable(result.get<sol::object>());
    checkFields(root, {"name", "help", "args", "verbs", "recognize", "preview", "search"});
    command::Command cmd{readString(root["name"]), readString(root["help"])};
    cmd.search = readBoolean(root["search"]);
    sol::object argsObject = root["args"];
    if (argsObject.valid() && argsObject.get_type() != sol::type::nil) {
        auto args = readArray(argsObject);
        for (size_t i = 1; i <= args.size(); ++i) {
            sol::object entry = args[i];
            command::Argument arg;
            if (entry.get_type() == sol::type::string) arg.name = readString(entry);
            else {
                auto declaration = readTable(entry); checkFields(declaration, {"name", "choices", "default", "rest"});
                arg.name = readString(declaration["name"]);
                sol::object choicesObject = declaration["choices"];
                if (choicesObject.get_type() != sol::type::nil) {
                    auto choices = readArray(choicesObject);
                    arg.choices.emplace();
                    for (size_t j = 1; j <= choices.size(); ++j) arg.choices->push_back({readString(choices[j])});
                }
                sol::object defaultObject = declaration["default"];
                if (defaultObject.get_type() != sol::type::nil) arg.defaultValue = readString(defaultObject);
                arg.rest = readBoolean(declaration["rest"]);
            }
            cmd.args.push_back(std::move(arg));
        }
    }
    auto verbs = readArray(root["verbs"]);
    for (size_t i = 1; i <= verbs.size(); ++i) {
        auto declaration = readTable(verbs[i]); checkFields(declaration, {"name", "danger", "run", "help"});
        command::Verb verb; verb.name = readString(declaration["name"]);
        sol::object help = declaration["help"];
        if (help.get_type() != sol::type::nil) verb.help = readString(help);
        verb.danger = readBoolean(declaration["danger"]);
        auto callback = readFunction(lua, declaration["run"]);
        verb.run = [callback, executing](const std::vector<std::string>& args) -> std::string {
            struct Execution {
                bool& active;
                Execution(bool& value) : active(value) { active = true; }
                ~Execution() { active = false; }
            } execution(*executing);
            auto result = callback->run(sol::as_table(args));
            if (!result.valid()) { sol::error error = result; return std::string(error.what()) + "; fix the plugin and run /relay Reload Plugins"; }
            if (!result.return_count()) return {};
            auto value = result.get<sol::object>();
            if (value.get_type() == sol::type::nil) return {};
            if (value.get_type() == sol::type::string) return value.as<std::string>();
            return "Return nil on success or an error string; fix the plugin and run /relay Reload Plugins";
        };
        cmd.verbs.push_back(std::move(verb));
    }
    sol::object recognize = root["recognize"];
    if (recognize.get_type() != sol::type::nil) {
        auto callback = readFunction(lua, recognize);
        cmd.recognize = [callback](const std::string& text) -> std::optional<std::vector<std::string>> {
            auto result = callback->run(text); checkResult(result);
            if (!result.return_count() || result.get<sol::object>().get_type() == sol::type::nil) return std::nullopt;
            auto values = readArray(result.get<sol::object>());
            std::vector<std::string> args;
            for (size_t i = 1; i <= values.size(); ++i) args.push_back(readString(values[i]));
            return args;
        };
    }
    sol::object preview = root["preview"];
    if (preview.get_type() != sol::type::nil) {
        auto callback = readFunction(lua, preview);
        cmd.preview = [callback](const std::vector<std::string>& args) -> command::Preview {
            auto result = callback->run(sol::as_table(args)); checkResult(result);
            if (result.return_count()) {
                auto value = result.get<sol::object>();
                if (value.get_type() == sol::type::string) {
                    auto title = readString(value);
                    if (!title.empty()) return {title, {}};
                } else if (value.get_type() == sol::type::nil && result.return_count() == 2) {
                    auto error = readString(result.get<sol::object>(1));
                    if (!error.empty()) return {{}, error};
                }
            }
            throw std::runtime_error("Return a nonempty preview string, or nil and an error with a recovery step");
        };
    }
    auto error = command::validate(cmd);
    if (!error.empty()) throw std::runtime_error(error);
    return cmd;
}

void loadLuaCommands(std::vector<command::Command>& commands, const std::filesystem::path& directory,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string(const std::string&)> openUrl, const std::function<void(std::string)>& report) {
    try {
        if (!std::filesystem::exists(directory)) return;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == L".lua") files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            try {
                auto cmd = loadLuaCommand(path, copy, openUrl);
                if (std::any_of(commands.begin(), commands.end(), [&](auto& other) { return other.name == cmd.name; }))
                    throw std::runtime_error("Choose a unique noun; this name is already registered");
                commands.push_back(std::move(cmd));
            } catch (const std::exception& e) { report(path.filename().string() + ": " + e.what() + "; fix the file and run /relay Reload Plugins"); }
        }
    } catch (const std::exception& e) { report(std::string(e.what()) + "; check the plugins directory and run /relay Reload Plugins"); }
}
