# C++ Code Style

NDS is C++20. Use `.cc` for C++ sources and `.hh` for C++ headers. Keep `.h`
headers C-compatible when they cross a runtime or device ABI boundary. Format
C++ with the repository `.clang-format` configuration.

## APIs and ownership

- Pass values, `const T&` inputs, or pointers. Do not use non-const lvalue
  references for output or in/out parameters.
- Return a newly produced successful value as `Result<T>`, not through an
  output pointer. For example, a registration function returns
  `Result<RegisteredRegion>` and a request-construction function returns
  `Result<Request>`.
- Use a pointer only when the caller intentionally provides mutable state that
  the function updates in place. Stored non-owning mutable dependencies are
  pointers, not references. The sole exception is a C++ language-required
  special member; prefer deleting it when it is unnecessary.
- Prefer native C++ ownership. Keep resources alive through the completion
  point required by their execution mode, then tear them down in reverse
  initialization order.

## Errors

- Operations that can fail during normal runtime execution return
  `nds::Result<T>`.
- Return the success value directly. Return failures with
  `nds::unexpected(ErrorCode::..., message)`, or propagate an existing
  `Error` with `nds::unexpected(result.error())`.
- Do not combine a boolean return with `error()`, `set_error()`, a mutable
  diagnostic string, or a caller-provided diagnostic buffer. Error state must
  travel with the result that reports the failure.
- C-compatible loader structures may retain their ABI-mandated error buffers
  internally. Translate those diagnostics into `nds::Error` immediately at the
  C++ boundary.

## Executables

- Use `nds::log` for executable diagnostics; do not add direct standard-stream
  or C stdio logging. Use `npu-client` and `cpu-server` as component names;
  applications may replace the spdlog sink with `nds::log::set_logger`.
- Use CLI11 for command-line option declarations and cross-option validation
  at the executable boundary.
