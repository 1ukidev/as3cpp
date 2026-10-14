#pragma once

#include "model/Model.hpp"
#include "syntax/Mxml.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace as3lsp::index {

struct ProjectConfig {
    std::vector<std::filesystem::path> roots;
    std::vector<std::filesystem::path> sourcePaths;
    // SWC, SWF or ABC files, or directories that contain SWC files.
    std::vector<std::filesystem::path> libraryPaths;
    std::optional<std::filesystem::path> sdkPath;
    // An SDK with the Flex framework: its manifests map MXML tags to classes.
    std::optional<std::filesystem::path> flexSdkPath;
    // Adds the SWC files of the Flex framework. They are added automatically when the project has MXML files.
    bool frameworkLibraries = false;
};

struct IndexPlan {
    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> libraries;
    // Source paths from the configuration and from asconfig.json files. They give the package of MXML files.
    std::vector<std::filesystem::path> sourceRoots;
};

// Collects the files to index. Reads the "source-path" and "library-path" options of asconfig.json files.
[[nodiscard]] IndexPlan planIndex(const ProjectConfig& config);

// The playerglobal or airglobal library of an AIR, Flex or Royale SDK.
[[nodiscard]] std::optional<std::filesystem::path> findGlobalLibrary(const std::filesystem::path& sdk);

// Builds the MXML context of a project: the manifests of the Flex SDK and the source roots of the plan.
[[nodiscard]] std::shared_ptr<const syntax::MxmlContext> createMxmlContext(const ProjectConfig& config,
                                                                           const IndexPlan& plan);

// Parses an ActionScript or MXML file for the index. Function scopes are skipped to save memory.
[[nodiscard]] model::UnitPtr parseSourceFile(const std::filesystem::path& path,
                                             const syntax::MxmlContext& mxml);

// Parses all files of a plan on worker threads. `deliver` receives batches of units on the worker threads,
// then one final call with `done` set.
class BackgroundIndexer {
public:
    using Deliver = std::function<void(std::vector<model::UnitPtr> units, bool done)>;

    BackgroundIndexer(IndexPlan plan, std::shared_ptr<const syntax::MxmlContext> mxml, Deliver deliver);
    BackgroundIndexer(const BackgroundIndexer&) = delete;
    BackgroundIndexer& operator=(const BackgroundIndexer&) = delete;
    ~BackgroundIndexer() = default;

private:
    std::jthread thread_;
};

} // namespace as3lsp::index
