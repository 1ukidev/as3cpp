#include "index/Project.hpp"

#include "abc/Library.hpp"
#include "syntax/Mxml.hpp"
#include "syntax/Parser.hpp"
#include "util/Files.hpp"
#include "util/Strings.hpp"
#include "util/Uri.hpp"

#include "json/Json.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <unordered_set>

namespace as3lsp::index {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t batchSize = 64;

bool hasExtension(const fs::path& path, std::string_view extension) {
    return util::iequals(path.extension().string(), extension);
}

bool isSkippedDirectory(const fs::path& path) {
    const std::string name = path.filename().string();
    return name.starts_with('.') || name == "node_modules";
}

void collectSources(const fs::path& root, std::vector<fs::path>& out, std::unordered_set<std::string>& seen) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return;
    }
    fs::recursive_directory_iterator it{root, fs::directory_options::skip_permission_denied, error};
    for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
        const auto& entry = *it;
        if (entry.is_directory(error)) {
            if (isSkippedDirectory(entry.path())) {
                it.disable_recursion_pending();
            }
            continue;
        }
        const bool isSource = hasExtension(entry.path(), ".as") || hasExtension(entry.path(), ".mxml");
        if (isSource && seen.insert(util::pathKey(entry.path())).second) {
            out.push_back(entry.path());
        }
    }
}

void collectLibraries(const fs::path& path, std::vector<fs::path>& out,
                      std::unordered_set<std::string>& seen) {
    std::error_code error;
    if (fs::is_directory(path, error)) {
        for (const auto& entry : fs::directory_iterator{path, error}) {
            if (entry.is_regular_file(error) && hasExtension(entry.path(), ".swc") &&
                seen.insert(util::pathKey(entry.path())).second) {
                out.push_back(entry.path());
            }
        }
    } else if (fs::is_regular_file(path, error) && seen.insert(util::pathKey(path)).second) {
        out.push_back(path);
    }
}

// The SWC files of the Flex framework, without the player and AIR globals that the SDK path already provides.
void collectFrameworkLibraries(const fs::path& sdk, std::vector<fs::path>& out,
                               std::unordered_set<std::string>& seen) {
    const fs::path libs = sdk / "frameworks" / "libs";
    std::error_code error;
    fs::recursive_directory_iterator it{libs, error};
    for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        const std::string name = path.filename().string();
        if (it->is_directory(error)) {
            if (name == "player") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (hasExtension(path, ".swc") && name.find("global") == std::string::npos &&
            seen.insert(util::pathKey(path)).second) {
            out.push_back(path);
        }
    }
}

void appendPaths(const json::Value& list, const fs::path& base, std::vector<fs::path>& out) {
    if (!list.isArray()) {
        return;
    }
    for (const auto& item : list.array()) {
        const std::string_view text = item.asString();
        if (text.empty() || text.find("${") != std::string_view::npos) {
            continue;
        }
        fs::path path{std::string{text}};
        out.push_back(path.is_absolute() ? path : base / path);
    }
}

// Adds the paths configured in `root/asconfig.json`, used by the vscode-as3mxml extension.
void readAsconfig(const fs::path& root, std::vector<fs::path>& sources, std::vector<fs::path>& libraries) {
    const auto text = util::readTextFile(root / "asconfig.json");
    if (!text) {
        return;
    }
    const auto config = json::parse(*text, {.lenient = true});
    if (!config) {
        return;
    }
    const json::Value& options = (*config)["compilerOptions"];
    appendPaths(options["source-path"], root, sources);
    appendPaths(options["library-path"], root, libraries);
    appendPaths(options["external-library-path"], root, libraries);
}

} // namespace

std::optional<fs::path> findGlobalLibrary(const fs::path& sdk) {
    std::error_code error;
    const fs::path libs = sdk / "frameworks" / "libs";
    if (const auto air = libs / "air" / "airglobal.swc"; fs::is_regular_file(air, error)) {
        return air;
    }
    std::optional<fs::path> best;
    double bestVersion = -1;
    for (const auto& entry : fs::directory_iterator{libs / "player", error}) {
        const std::string name = entry.path().filename().string();
        double version = 0;
        if (std::from_chars(name.data(), name.data() + name.size(), version).ec != std::errc{}) {
            continue;
        }
        if (const auto library = entry.path() / "playerglobal.swc";
            version > bestVersion && fs::is_regular_file(library, error)) {
            best = library;
            bestVersion = version;
        }
    }
    if (!best) {
        if (const auto abc = libs / "air" / "airglobal.abc"; fs::is_regular_file(abc, error)) {
            return abc;
        }
    }
    return best;
}

IndexPlan planIndex(const ProjectConfig& config) {
    IndexPlan plan;
    std::unordered_set<std::string> seenSources;
    std::unordered_set<std::string> seenLibraries;

    std::vector<fs::path> sourcePaths = config.sourcePaths;
    std::vector<fs::path> libraryPaths;
    for (const auto& root : config.roots) {
        readAsconfig(root, sourcePaths, libraryPaths);
    }
    if (config.sdkPath) {
        if (auto global = findGlobalLibrary(*config.sdkPath)) {
            collectLibraries(*global, plan.libraries, seenLibraries);
        }
    }
    for (const auto& path : config.libraryPaths) {
        collectLibraries(path, plan.libraries, seenLibraries);
    }
    for (const auto& path : libraryPaths) {
        collectLibraries(path, plan.libraries, seenLibraries);
    }
    for (const auto& root : config.roots) {
        collectSources(root, plan.sources, seenSources);
    }
    for (const auto& path : sourcePaths) {
        collectSources(path, plan.sources, seenSources);
    }
    plan.sourceRoots = sourcePaths;

    const bool hasMxml =
        std::ranges::any_of(plan.sources, [](const fs::path& path) { return syntax::isMxmlFile(path); });
    const auto frameworkSdk = config.flexSdkPath ? config.flexSdkPath : config.sdkPath;
    if (frameworkSdk && (config.frameworkLibraries || hasMxml)) {
        collectFrameworkLibraries(*frameworkSdk, plan.libraries, seenLibraries);
    }
    return plan;
}

std::shared_ptr<const syntax::MxmlContext> createMxmlContext(const ProjectConfig& config,
                                                             const IndexPlan& plan) {
    auto context = std::make_shared<syntax::MxmlContext>();
    if (const auto sdk = config.flexSdkPath ? config.flexSdkPath : config.sdkPath) {
        context->loadSdk(*sdk);
    }
    for (const auto& root : plan.sourceRoots) {
        context->addSourceRoot(root);
    }
    return context;
}

model::UnitPtr parseSourceFile(const fs::path& path, const syntax::MxmlContext& mxml) {
    const auto text = util::readTextFile(path);
    if (!text) {
        return nullptr;
    }
    auto unit = syntax::isMxmlFile(path) ? syntax::parseMxml(*text, path, mxml, {.collectScopes = false})
                                         : syntax::parse(*text, {.collectScopes = false});
    unit->key = util::pathKey(path);
    unit->uri = util::pathToUri(path);
    unit->origin = model::Origin::Source;
    return unit;
}

BackgroundIndexer::BackgroundIndexer(IndexPlan plan, std::shared_ptr<const syntax::MxmlContext> mxml,
                                     Deliver deliver)
    : thread_([plan = std::move(plan), mxml = std::move(mxml),
               deliver = std::move(deliver)](std::stop_token stop) {
          const std::size_t total = plan.libraries.size() + plan.sources.size();
          std::atomic<std::size_t> next{0};
          const auto work = [&] {
              std::vector<model::UnitPtr> batch;
              for (std::size_t i = next++; i < total && !stop.stop_requested(); i = next++) {
                  const bool isLibrary = i < plan.libraries.size();
                  model::UnitPtr unit = isLibrary
                                            ? abc::loadLibrary(plan.libraries[i])
                                            : parseSourceFile(plan.sources[i - plan.libraries.size()], *mxml);
                  if (unit) {
                      batch.push_back(std::move(unit));
                  }
                  if (batch.size() >= batchSize) {
                      deliver(std::exchange(batch, {}), false);
                  }
              }
              if (!batch.empty()) {
                  deliver(std::move(batch), false);
              }
          };
          const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1U, 16U);
          {
              std::vector<std::jthread> workers;
              for (unsigned i = 1; i < count; ++i) {
                  workers.emplace_back(work);
              }
              work();
          }
          deliver({}, true);
      }) {}

} // namespace as3lsp::index
