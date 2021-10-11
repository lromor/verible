// Copyright 2021 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A super-simple dummy LSP without functionality except responding
// to initialize and shutdown as well as tracking file contents.
// This is merely to test that the json-rpc plumbing is working.

#include "common/lsp/json-rpc-dispatcher.h"
#include "common/lsp/lsp-protocol.h"
#include "common/lsp/lsp-text-buffer.h"
#include "common/lsp/message-stream-splitter.h"
#include "common/util/init_command_line.h"
#include "verilog/analysis/verilog_analyzer.h"

#ifndef _WIN32
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
// Windows doesn't have Posix read(), but something called _read
#define read(fd, buf, size) _read(fd, buf, size)
#endif

using verible::lsp::BufferCollection;
using verible::lsp::EditTextBuffer;
using verible::lsp::InitializeResult;
using verible::lsp::JsonRpcDispatcher;
using verible::lsp::MessageStreamSplitter;
using verilog::VerilogAnalyzer;

static std::string GetVersionNumber() {
  return "0.0 alpha";   // TODO(hzeller): once ready, extract from build version
}

// The "initialize" method requests server capabilities.
InitializeResult InitializeServer(const nlohmann::json &params) {
  // Ignore passed client capabilities from params right now,
  // just announce what we do.
  InitializeResult result;
  result.serverInfo = {
      .name = "Verible Verilog language server.",
      .version = GetVersionNumber(),
  };
  result.capabilities = {
      {
          "textDocumentSync",
          {
              {"openClose", true},  // Want open/close events
              {"change", 2},        // Incremental updates
          },
      },
  };
  return result;
}

class VersionedAnalyzedBuffer {
public:
  VersionedAnalyzedBuffer(int64_t version, absl::string_view uri,
                          absl::string_view content)
    : version_(version),
      parser_(VerilogAnalyzer::AnalyzeAutomaticMode(content, uri)) {
    std::cerr << "Analyzed " << uri << " lex:" <<
      parser_->LexStatus() << "; parser:" << parser_->ParseStatus() << "\n";
  }

  bool is_good() const {
    return parser_->LexStatus().ok() && parser_->ParseStatus().ok();
  }

  std::vector<verible::lsp::Diagnostic> GetDiagnostics() const {
    const auto &rejected_tokens = parser_->GetRejectedTokens();
    std::vector<verible::lsp::Diagnostic> result;
    result.reserve(rejected_tokens.size());
    for (const auto &rejected_token : rejected_tokens) {
      parser_->ExtractLinterTokenErrorDetail(
          rejected_token,
          [&result](const std::string &filename, verible::LineColumnRange range,
                    verible::AnalysisPhase phase, absl::string_view token_text,
                    absl::string_view context_line, const std::string &msg) {
            // Note: msg is currently empty and not useful.
            const auto message = (phase == verible::AnalysisPhase::kLexPhase)
                                     ? "token error"
                                     : "syntax error";
            result.emplace_back(verible::lsp::Diagnostic{
                .range{.start{.line = range.start.line,
                              .character = range.start.column},
                       .end{.line = range.end.line,  //
                            .character = range.end.column}},
                .message = message,
            });
          });
    }
    return result;
  }

private:
  const int64_t version_;
  std::unique_ptr<VerilogAnalyzer> parser_;
};

class BufferTracker {
public:
  void Update(const std::string& filename,
              const EditTextBuffer &txt) {
    // TODO: remove file:// prefix.
    txt.RequestContent([&txt, &filename, this](absl::string_view content) {
      current_ = std::make_shared<VersionedAnalyzedBuffer>(
        txt.last_global_version(), filename, content);
    });
    if (current_->is_good()) {
      last_good_ = current_;
    }
  }

  const VersionedAnalyzedBuffer *current() const { return current_.get(); }

private:
  std::shared_ptr<VersionedAnalyzedBuffer> current_;
  std::shared_ptr<VersionedAnalyzedBuffer> last_good_;
};

class ParsedBufferContainer {
public:
  BufferTracker *Update(const std::string &filename,
              const EditTextBuffer &txt) {
    auto inserted = buffers_.insert({filename, nullptr});
    if (inserted.second) {
      inserted.first->second.reset(new BufferTracker());
    }
    inserted.first->second->Update(filename, txt);
    return inserted.first->second.get();
  }

private:
  std::unordered_map<std::string, std::unique_ptr<BufferTracker>> buffers_;
};

void ConsiderSendDiagnostics(const std::string &uri,
                             const VersionedAnalyzedBuffer *buffer,
                             JsonRpcDispatcher *dispatcher) {
  verible::lsp::PublishDiagnosticsParams params;
  params.uri = uri;
  params.diagnostics = buffer->GetDiagnostics();
  dispatcher->SendNotification("textDocument/publishDiagnostics", params);
}

int main(int argc, char *argv[]) {
  const auto file_args = verible::InitCommandLine(argv[0], &argc, &argv);

#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  std::cerr << "Verible Alpha Language Server " << GetVersionNumber()
            << std::endl;

  // Input and output is stdin and stdout
  static constexpr int in_fd = 0;  // STDIN_FILENO
  JsonRpcDispatcher::WriteFun write_fun = [](absl::string_view reply) {
    // Output formatting as header/body chunk as required by LSP spec.
    std::cout << "Content-Length: " << reply.size() << "\r\n\r\n";
    std::cout << reply << std::flush;
  };

  // We want the buffer size to be the largest message we could
  // receive. It typically would be in the order of largest file to
  // be opened (as it is sent verbatim in didOpen).
  // Should be chosen accordingly.
  MessageStreamSplitter stream_splitter(1 << 20);
  JsonRpcDispatcher dispatcher(write_fun);

  // All bodies the stream splitter extracts are pushed to the json dispatcher
  stream_splitter.SetMessageProcessor(
      [&dispatcher](absl::string_view /*header*/, absl::string_view body) {
        return dispatcher.DispatchMessage(body);
      });

  // The buffer collection keeps track of all the buffers opened in the editor.
  // It registers callbacks to receive the relevant events on the dispatcher.
  BufferCollection buffers(&dispatcher);
  ParsedBufferContainer parsed_buffers;

  // Exchange of capabilities.
  dispatcher.AddRequestHandler("initialize", InitializeServer);

  // The client sends a request to shut down. Use that to exit our loop.
  bool shutdown_requested = false;
  dispatcher.AddRequestHandler("shutdown",
                               [&shutdown_requested](const nlohmann::json &) {
                                 shutdown_requested = true;
                                 return nullptr;
                               });

  int64_t last_updated_version = 0;
  absl::Status status = absl::OkStatus();
  while (status.ok() && !shutdown_requested) {
    status = stream_splitter.PullFrom([](char *buf, int size) -> int {  //
      return read(in_fd, buf, size);
    });

    // TODO: this should work async.
    buffers.MapBuffersChangedSince(
        last_updated_version,
        [&parsed_buffers, &dispatcher](const std::string &uri,
                                       const EditTextBuffer &txt) {
          BufferTracker *const buffer = parsed_buffers.Update(uri, txt);
          ConsiderSendDiagnostics(uri, buffer->current(), &dispatcher);
        });
    last_updated_version = buffers.global_version();
  }

  std::cerr << status.message() << std::endl;

  if (shutdown_requested) {
    std::cerr << "Shutting down due to shutdown request." << std::endl;
  }

  std::cerr << "Statistics" << std::endl;
  for (const auto &stats : dispatcher.GetStatCounters()) {
    fprintf(stderr, "%30s %9d\n", stats.first.c_str(), stats.second);
  }
}
