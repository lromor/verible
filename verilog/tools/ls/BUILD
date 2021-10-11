# This package contains a SystemVerilog language server [1] implementation.
#
# [1]: https://microsoft.github.io/language-server-protocol/specification

licenses(["notice"])

load("//common/lsp:jcxxgen.bzl", "jcxxgen")

package(
    default_visibility = [
        "//:__subpackages__",
    ],
)

cc_binary(
    name = "verible-verilog-ls",
    srcs = ["verilog_ls.cc"],
    deps = [
      "//common/lsp:lsp-protocol",
      "//common/lsp:json-rpc-dispatcher",
      "//common/lsp:lsp-text-buffer",
      "//common/lsp:message-stream-splitter",
      "//common/util:init_command_line",
      "//verilog/analysis:verilog_analyzer",
      "//verilog/analysis:verilog_linter",
    ],
)
