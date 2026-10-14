#pragma once

#include "analysis/Document.hpp"
#include "index/Index.hpp"
#include "index/Project.hpp"
#include "index/SourceLocator.hpp"
#include "lsp/Transport.hpp"
#include "util/BlockingQueue.hpp"

#include "json/Json.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace as3lsp::lsp {

// The language server. All state is owned by the thread that calls run(); other threads only post events.
class Server {
public:
    explicit Server(Transport& transport);

    // Processes messages until the client sends "exit" or closes the input. Returns the process exit code.
    int run();

private:
    struct Message {
        json::Value value;
    };
    struct IndexBatch {
        std::vector<model::UnitPtr> units;
        bool done = false;
    };
    struct InputClosed {};
    using Event = std::variant<Message, IndexBatch, InputClosed>;

    using RequestHandler = json::Value (Server::*)(const json::Value&);
    using NotificationHandler = void (Server::*)(const json::Value&);

    void readMessages();
    void dispatch(const json::Value& message);
    void handleRequest(const json::Value& id, std::string_view method, const json::Value& params);
    void handleNotification(std::string_view method, const json::Value& params);
    [[nodiscard]] bool takeCancellation(const json::Value& id);

    void sendResult(const json::Value& id, json::Value result);
    void sendError(const json::Value& id, int code, std::string_view message);
    void sendNotification(std::string_view method, json::Value params);
    void sendRequest(std::string_view method, json::Value params);
    void log(std::string_view message);

    json::Value initialize(const json::Value& params);
    json::Value shutdown(const json::Value& params);
    json::Value completion(const json::Value& params);
    json::Value definition(const json::Value& params);
    json::Value typeDefinition(const json::Value& params);
    json::Value hover(const json::Value& params);
    json::Value signatureHelp(const json::Value& params);
    json::Value documentSymbol(const json::Value& params);
    json::Value workspaceSymbol(const json::Value& params);

    void initialized(const json::Value& params);
    void exit(const json::Value& params);
    void didOpen(const json::Value& params);
    void didChange(const json::Value& params);
    void didClose(const json::Value& params);
    void didChangeWatchedFiles(const json::Value& params);
    void ignore(const json::Value& params);

    // Returns the refreshed document named in params.textDocument.uri.
    analysis::Document& documentFor(const json::Value& params);
    void refresh(analysis::Document& document);
    void refreshAll();
    void publishDiagnostics(const analysis::Document& document);
    void publishPendingDiagnostics();
    void startIndexing();
    void applyIndexBatch(IndexBatch& batch);
    void indexFileFromDisk(const std::string& key, const std::filesystem::path& path);

    Transport& transport_;
    util::BlockingQueue<Event> events_;
    index::Index index_;
    index::StringMap<std::unique_ptr<analysis::Document>> documents_;
    std::unordered_set<std::string> workspaceFiles_;
    // Documents edited since their diagnostics were last published.
    std::unordered_set<std::string> pendingDiagnostics_;
    index::ProjectConfig config_;
    std::shared_ptr<const syntax::MxmlContext> mxml_ = std::make_shared<syntax::MxmlContext>();
    std::unique_ptr<index::SourceLocator> sources_;
    std::unique_ptr<index::BackgroundIndexer> indexer_;

    std::mutex cancelMutex_;
    std::unordered_set<std::string> cancelled_;

    bool initialized_ = false;
    bool shuttingDown_ = false;
    bool exitRequested_ = false;
    bool progressSupported_ = false;
    std::int64_t nextRequestId_ = 1;
    std::chrono::steady_clock::time_point indexStart_;
};

} // namespace as3lsp::lsp
