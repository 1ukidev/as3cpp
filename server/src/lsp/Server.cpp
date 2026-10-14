#include "lsp/Server.hpp"

#include "analysis/Analyzer.hpp"
#include "lsp/Protocol.hpp"
#include "syntax/Mxml.hpp"
#include "util/Strings.hpp"
#include "util/Uri.hpp"

#include <thread>
#include <unordered_map>

namespace as3lsp::lsp {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view progressToken = "as3cpp/indexing";
constexpr std::size_t maxWorkspaceSymbols = 500;
// Diagnostics are published when the user stops typing for this long.
constexpr std::chrono::milliseconds diagnosticsDelay{200};

std::vector<fs::path> pathsFrom(const json::Value& list, const std::optional<fs::path>& base) {
    std::vector<fs::path> paths;
    if (!list.isArray()) {
        return paths;
    }
    for (const auto& item : list.array()) {
        if (const std::string_view text = item.asString(); !text.empty()) {
            fs::path path{std::string{text}};
            paths.push_back(path.is_relative() && base ? *base / path : path);
        }
    }
    return paths;
}

bool isSourceFile(const fs::path& path) {
    return util::iequals(path.extension().string(), ".as") || syntax::isMxmlFile(path);
}

json::Value symbolInformation(std::string_view name, int kind, const std::string& uri,
                              const model::Range& range, std::string_view container) {
    json::Value info = json::Object{
        {"name", name}, {"kind", kind}, {"location", json::Object{{"uri", uri}, {"range", toJson(range)}}}};
    if (!container.empty()) {
        info["containerName"] = container;
    }
    return info;
}

} // namespace

Server::Server(Transport& transport) : transport_(transport) {}

int Server::run() {
    std::thread{[this] { readMessages(); }}.detach();
    while (!exitRequested_) {
        std::optional<Event> next =
            pendingDiagnostics_.empty() ? events_.pop() : events_.popFor(diagnosticsDelay);
        if (!next) {
            publishPendingDiagnostics();
            continue;
        }
        Event& event = *next;
        if (auto* message = std::get_if<Message>(&event)) {
            dispatch(message->value);
        } else if (auto* batch = std::get_if<IndexBatch>(&event)) {
            applyIndexBatch(*batch);
        } else {
            return 1;
        }
    }
    return shuttingDown_ ? 0 : 1;
}

void Server::readMessages() {
    while (auto body = transport_.read()) {
        auto message = json::parse(*body);
        if (!message) {
            sendError(nullptr, ErrorCode::ParseError, "Invalid JSON message");
            continue;
        }
        if ((*message)["method"].asString() == "$/cancelRequest") {
            std::scoped_lock lock{cancelMutex_};
            cancelled_.insert(json::serialize((*message)["params"]["id"]));
            continue;
        }
        events_.push(Message{std::move(*message)});
    }
    events_.push(InputClosed{});
}

bool Server::takeCancellation(const json::Value& id) {
    std::scoped_lock lock{cancelMutex_};
    return cancelled_.erase(json::serialize(id)) > 0;
}

void Server::dispatch(const json::Value& message) {
    const auto* method = message.find("method");
    if (!method) {
        return;
    }
    if (const auto* id = message.find("id")) {
        handleRequest(*id, method->asString(), message["params"]);
    } else {
        handleNotification(method->asString(), message["params"]);
    }
}

void Server::handleRequest(const json::Value& id, std::string_view method, const json::Value& params) {
    static const std::unordered_map<std::string_view, RequestHandler> handlers = {
        {"initialize", &Server::initialize},
        {"shutdown", &Server::shutdown},
        {"textDocument/completion", &Server::completion},
        {"textDocument/definition", &Server::definition},
        {"textDocument/typeDefinition", &Server::typeDefinition},
        {"textDocument/hover", &Server::hover},
        {"textDocument/signatureHelp", &Server::signatureHelp},
        {"textDocument/documentSymbol", &Server::documentSymbol},
        {"workspace/symbol", &Server::workspaceSymbol},
    };
    if (takeCancellation(id)) {
        sendError(id, ErrorCode::RequestCancelled, "Request cancelled");
        return;
    }
    if (!initialized_ && method != "initialize") {
        sendError(id, ErrorCode::ServerNotInitialized, "Server not initialized");
        return;
    }
    if (shuttingDown_) {
        sendError(id, ErrorCode::InvalidRequest, "Server is shutting down");
        return;
    }
    const auto handler = handlers.find(method);
    if (handler == handlers.end()) {
        sendError(id, ErrorCode::MethodNotFound, "Method not found: " + std::string{method});
        return;
    }
    try {
        sendResult(id, (this->*handler->second)(params));
    } catch (const LspError& error) {
        sendError(id, error.code(), error.what());
    } catch (const std::exception& error) {
        sendError(id, ErrorCode::InternalError, error.what());
    }
}

void Server::handleNotification(std::string_view method, const json::Value& params) {
    static const std::unordered_map<std::string_view, NotificationHandler> handlers = {
        {"initialized", &Server::initialized},
        {"exit", &Server::exit},
        {"textDocument/didOpen", &Server::didOpen},
        {"textDocument/didChange", &Server::didChange},
        {"textDocument/didClose", &Server::didClose},
        {"workspace/didChangeWatchedFiles", &Server::didChangeWatchedFiles},
        {"workspace/didChangeConfiguration", &Server::ignore},
        {"$/setTrace", &Server::ignore},
    };
    const auto handler = handlers.find(method);
    if (handler == handlers.end()) {
        return;
    }
    try {
        (this->*handler->second)(params);
    } catch (const std::exception& error) {
        log(std::string{method} + " failed: " + error.what());
    }
}

void Server::sendResult(const json::Value& id, json::Value result) {
    transport_.write(
        json::serialize(json::Object{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}));
}

void Server::sendError(const json::Value& id, int code, std::string_view message) {
    transport_.write(json::serialize(json::Object{
        {"jsonrpc", "2.0"}, {"id", id}, {"error", json::Object{{"code", code}, {"message", message}}}}));
}

void Server::sendNotification(std::string_view method, json::Value params) {
    transport_.write(
        json::serialize(json::Object{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}));
}

void Server::sendRequest(std::string_view method, json::Value params) {
    transport_.write(json::serialize(json::Object{
        {"jsonrpc", "2.0"}, {"id", nextRequestId_++}, {"method", method}, {"params", std::move(params)}}));
}

void Server::log(std::string_view message) {
    sendNotification("window/logMessage", json::Object{{"type", 4}, {"message", message}});
}

json::Value Server::initialize(const json::Value& params) {
    if (initialized_) {
        throw LspError{ErrorCode::InvalidRequest, "Server already initialized"};
    }
    initialized_ = true;
    progressSupported_ = params["capabilities"]["window"]["workDoneProgress"].asBool();

    if (const auto& folders = params["workspaceFolders"]; folders.isArray()) {
        for (const auto& folder : folders.array()) {
            if (auto path = util::uriToPath(folder["uri"].asString())) {
                config_.roots.push_back(std::move(*path));
            }
        }
    } else if (auto root = util::uriToPath(params["rootUri"].asString())) {
        config_.roots.push_back(std::move(*root));
    } else if (const auto rootPath = params["rootPath"].asString(); !rootPath.empty()) {
        config_.roots.emplace_back(std::string{rootPath});
    }

    const auto& options = params["initializationOptions"];
    const std::optional<fs::path> base =
        config_.roots.empty() ? std::nullopt : std::optional<fs::path>{config_.roots.front()};
    if (const auto sdk = options["sdkPath"].asString(); !sdk.empty()) {
        config_.sdkPath = fs::path{std::string{sdk}};
    }
    config_.sourcePaths = pathsFrom(options["sourcePaths"], base);
    config_.libraryPaths = pathsFrom(options["libraryPaths"], base);
    config_.frameworkLibraries = options["frameworkLibraries"].asBool();
    if (const auto flexSdk = options["flexSdkPath"].asString(); !flexSdk.empty()) {
        config_.flexSdkPath = fs::path{std::string{flexSdk}};
    }

    json::Value capabilities = json::Object{
        {"positionEncoding", "utf-16"},
        {"textDocumentSync", json::Object{{"openClose", true}, {"change", 2}}},
        {"completionProvider", json::Object{{"triggerCharacters", json::Array{".", ":", " "}},
                                            {"resolveProvider", false},
                                            {"completionItem", json::Object{{"labelDetailsSupport", true}}}}},
        {"hoverProvider", true},
        {"signatureHelpProvider", json::Object{{"triggerCharacters", json::Array{"(", ","}},
                                               {"retriggerCharacters", json::Array{","}}}},
        {"definitionProvider", true},
        {"typeDefinitionProvider", true},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
    };
    return json::Object{{"capabilities", std::move(capabilities)},
                        {"serverInfo", json::Object{{"name", "as3lsp"}, {"version", AS3LSP_VERSION}}}};
}

json::Value Server::shutdown(const json::Value&) {
    shuttingDown_ = true;
    indexer_.reset();
    return nullptr;
}

void Server::initialized(const json::Value&) {
    startIndexing();
}

void Server::exit(const json::Value&) {
    exitRequested_ = true;
}

void Server::ignore(const json::Value&) {}

void Server::startIndexing() {
    indexStart_ = std::chrono::steady_clock::now();
    index::IndexPlan plan = index::planIndex(config_);
    mxml_ = index::createMxmlContext(config_, plan);
    if (const auto sdk = config_.flexSdkPath ? config_.flexSdkPath : config_.sdkPath) {
        sources_ = std::make_unique<index::SourceLocator>(index::SourceLocator::flexSourceRoots(*sdk), mxml_);
    }
    for (const auto& source : plan.sources) {
        workspaceFiles_.insert(util::pathKey(source));
    }
    const std::string summary = std::to_string(plan.sources.size()) + " source files and " +
                                std::to_string(plan.libraries.size()) + " libraries";
    log("Indexing " + summary);
    if (!config_.sdkPath) {
        log("No SDK configured: classes from the Flash Player and AIR APIs are not available.");
    }
    if (progressSupported_) {
        sendRequest("window/workDoneProgress/create", json::Object{{"token", progressToken}});
        sendNotification("$/progress", json::Object{{"token", progressToken},
                                                    {"value", json::Object{{"kind", "begin"},
                                                                           {"title", "Indexing ActionScript"},
                                                                           {"message", summary},
                                                                           {"cancellable", false}}}});
    }
    indexer_ =
        std::make_unique<index::BackgroundIndexer>(std::move(plan), mxml_, [this](auto units, bool done) {
            events_.push(IndexBatch{std::move(units), done});
        });
}

void Server::applyIndexBatch(IndexBatch& batch) {
    for (auto& unit : batch.units) {
        if (!documents_.contains(unit->key)) {
            index_.add(std::move(unit));
        }
    }
    if (!batch.done) {
        return;
    }
    indexer_.reset();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - indexStart_);
    const std::string message = "Indexed " + std::to_string(index_.unitCount()) + " units in " +
                                std::to_string(elapsed.count()) + " ms";
    log(message);
    if (progressSupported_) {
        sendNotification("$/progress",
                         json::Object{{"token", progressToken},
                                      {"value", json::Object{{"kind", "end"}, {"message", message}}}});
    }
}

void Server::indexFileFromDisk(const std::string& key, const fs::path& path) {
    if (auto unit = index::parseSourceFile(path, *mxml_)) {
        index_.add(std::move(unit));
    } else {
        index_.remove(key);
    }
}

void Server::refresh(analysis::Document& document) {
    if (document.refresh()) {
        index_.add(document.unit());
        publishDiagnostics(document);
    }
}

void Server::publishDiagnostics(const analysis::Document& document) {
    pendingDiagnostics_.erase(document.key());
    json::Array diagnostics;
    for (const auto& diagnostic : document.unit()->diagnostics) {
        diagnostics.push_back(json::Object{{"range", toJson(diagnostic.range)},
                                           {"severity", static_cast<int>(diagnostic.severity)},
                                           {"source", "as3cpp"},
                                           {"message", diagnostic.message}});
    }
    sendNotification("textDocument/publishDiagnostics",
                     json::Object{{"uri", document.uri()},
                                  {"version", document.version()},
                                  {"diagnostics", std::move(diagnostics)}});
}

void Server::publishPendingDiagnostics() {
    for (const auto& key : std::exchange(pendingDiagnostics_, {})) {
        if (const auto it = documents_.find(key); it != documents_.end()) {
            refresh(*it->second);
        }
    }
}

void Server::refreshAll() {
    for (auto& [key, document] : documents_) {
        refresh(*document);
    }
}

analysis::Document& Server::documentFor(const json::Value& params) {
    const std::string_view uri = params["textDocument"]["uri"].asString();
    const auto it = documents_.find(util::uriKey(uri));
    if (it == documents_.end()) {
        throw LspError{ErrorCode::InvalidParams, "Document is not open: " + std::string{uri}};
    }
    // Other open documents can declare the types used here, so their unsaved edits must be indexed too.
    refreshAll();
    return *it->second;
}

void Server::didOpen(const json::Value& params) {
    const auto& item = params["textDocument"];
    std::string uri{item["uri"].asString()};
    std::string key = util::uriKey(uri);
    const auto path = util::uriToPath(uri);
    auto mxml = path && syntax::isMxmlFile(*path) ? mxml_ : nullptr;
    auto document = std::make_unique<analysis::Document>(
        std::move(uri), key, std::string{item["text"].asString()}, item["version"].asInt(), std::move(mxml));
    refresh(*document);
    documents_[key] = std::move(document);
}

void Server::didChange(const json::Value& params) {
    const auto it = documents_.find(util::uriKey(params["textDocument"]["uri"].asString()));
    if (it == documents_.end()) {
        return;
    }
    auto& document = *it->second;
    const std::int64_t version = params["textDocument"]["version"].asInt(document.version());
    if (const auto& changes = params["contentChanges"]; changes.isArray()) {
        for (const auto& change : changes.array()) {
            if (const auto range = rangeFrom(change["range"])) {
                document.applyChange(*range, change["text"].asString());
            } else {
                document.setText(std::string{change["text"].asString()}, version);
            }
        }
    }
    document.setVersion(version);
    pendingDiagnostics_.insert(document.key());
}

void Server::didClose(const json::Value& params) {
    const std::string_view uri = params["textDocument"]["uri"].asString();
    const std::string key = util::uriKey(uri);
    documents_.erase(key);
    pendingDiagnostics_.erase(key);
    sendNotification("textDocument/publishDiagnostics",
                     json::Object{{"uri", uri}, {"diagnostics", json::Array{}}});
    if (workspaceFiles_.contains(key)) {
        if (auto path = util::uriToPath(uri)) {
            indexFileFromDisk(key, *path);
        }
    } else {
        index_.remove(key);
    }
}

void Server::didChangeWatchedFiles(const json::Value& params) {
    constexpr std::int64_t deleted = 3;
    const auto& changes = params["changes"];
    if (!changes.isArray()) {
        return;
    }
    for (const auto& change : changes.array()) {
        const auto path = util::uriToPath(change["uri"].asString());
        if (!path || !isSourceFile(*path)) {
            continue;
        }
        const std::string key = util::pathKey(*path);
        const bool isOpen = documents_.contains(key);
        if (change["type"].asInt() == deleted) {
            workspaceFiles_.erase(key);
            if (!isOpen) {
                index_.remove(key);
            }
            continue;
        }
        workspaceFiles_.insert(key);
        if (!isOpen) {
            indexFileFromDisk(key, *path);
        }
    }
}

json::Value Server::completion(const json::Value& params) {
    auto& document = documentFor(params);
    const auto position = positionFrom(params["position"]);
    if (!position) {
        throw LspError{ErrorCode::InvalidParams, "Missing position"};
    }
    const std::string_view trigger = params["context"]["triggerCharacter"].asString();
    return toJson(analysis::Analyzer{index_, document, sources_.get()}.completion(*position, trigger));
}

json::Value Server::definition(const json::Value& params) {
    auto& document = documentFor(params);
    const auto position = positionFrom(params["position"]);
    if (!position) {
        throw LspError{ErrorCode::InvalidParams, "Missing position"};
    }
    return toJson(analysis::Analyzer{index_, document, sources_.get()}.definition(*position));
}

json::Value Server::typeDefinition(const json::Value& params) {
    auto& document = documentFor(params);
    const auto position = positionFrom(params["position"]);
    if (!position) {
        throw LspError{ErrorCode::InvalidParams, "Missing position"};
    }
    return toJson(analysis::Analyzer{index_, document, sources_.get()}.typeDefinition(*position));
}

json::Value Server::hover(const json::Value& params) {
    auto& document = documentFor(params);
    const auto position = positionFrom(params["position"]);
    if (!position) {
        throw LspError{ErrorCode::InvalidParams, "Missing position"};
    }
    const auto result = analysis::Analyzer{index_, document, sources_.get()}.hover(*position);
    return result ? toJson(*result) : json::Value{nullptr};
}

json::Value Server::signatureHelp(const json::Value& params) {
    auto& document = documentFor(params);
    const auto position = positionFrom(params["position"]);
    if (!position) {
        throw LspError{ErrorCode::InvalidParams, "Missing position"};
    }
    const auto result = analysis::Analyzer{index_, document, sources_.get()}.signatureHelp(*position);
    return result ? toJson(*result) : json::Value{nullptr};
}

json::Value Server::documentSymbol(const json::Value& params) {
    return documentSymbols(*documentFor(params).unit());
}

json::Value Server::workspaceSymbol(const json::Value& params) {
    refreshAll();
    const std::string_view query = params["query"].asString();
    json::Array types;
    json::Array members;
    index_.forEachUnit([&](const model::Unit& unit) {
        if (!unit.hasSource()) {
            return;
        }
        for (const auto& type : unit.types) {
            if (util::fuzzyMatch(query, type.name)) {
                types.push_back(symbolInformation(type.name, symbolKind(type), unit.uri, type.selectionRange,
                                                  type.package));
            }
            for (const auto& member : type.members) {
                if (members.size() < maxWorkspaceSymbols && util::fuzzyMatch(query, member.name)) {
                    members.push_back(symbolInformation(member.name, symbolKind(member), unit.uri,
                                                        member.selectionRange, type.name));
                }
            }
        }
        for (const auto& global : unit.globals) {
            if (members.size() < maxWorkspaceSymbols && util::fuzzyMatch(query, global.decl.name)) {
                members.push_back(symbolInformation(global.decl.name, symbolKind(global.decl), unit.uri,
                                                    global.decl.selectionRange, global.package));
            }
        }
    });
    for (auto& member : members) {
        if (types.size() >= maxWorkspaceSymbols) {
            break;
        }
        types.push_back(std::move(member));
    }
    if (types.size() > maxWorkspaceSymbols) {
        types.resize(maxWorkspaceSymbols);
    }
    return types;
}

} // namespace as3lsp::lsp
