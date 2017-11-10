#include <nan.h>
#include <node.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include "generic-streaming-worker.h"
#include "neither/either.hpp"
#include "uglify/uglify.js.c"

using namespace neither;
using Allocator = v8::ArrayBuffer::Allocator;
using v8::Context;
using v8::Function;
using v8::Isolate;
using v8::Local;
using v8::Locker;
using v8::Object;
using v8::Script;
using v8::Value;

// TODO: curry this function with working type inference.
template <typename T, typename E>
auto get_or(MaybeLocal<T> maybe, E error) {
  if (maybe.IsEmpty()) {
    const E real_error(error);
    return Either<E, Local<T>>{left(real_error)};
  }
  return Either<E, Local<T>>{right(maybe.ToLocalChecked())};
}

enum class PuglifyError {
  SourceMissing,
  UglifyCompilationFailure,
  UglifyRuntimeFailure,
  ContextMissingGlobals,
  GlobalMinifyNotFound,
  GlobalMinifyNotAFunction,
  BadInput,
  MalformedMinifyResult,
  CodeMissing
};

auto puglify_error_to_string(PuglifyError error) -> std::string {
  switch (error) {
    case PuglifyError::SourceMissing:
      return "The included uglify bundle is missing or corrupted.";
    case PuglifyError::UglifyCompilationFailure:
      return "Uglify failed to compile.";
    case PuglifyError::UglifyRuntimeFailure:
      return "Uglify encountered a runtime error while attaching to the global "
             "scope.";
    case PuglifyError::ContextMissingGlobals:
      return "Could not access globals from the context.";
    case PuglifyError::GlobalMinifyNotFound:
      return "The global minify function is not present in the context.";
    case PuglifyError::GlobalMinifyNotAFunction:
      return "The global minify was not a function.";
    case PuglifyError::BadInput:
      return "The input source could not be converted to a string";
    case PuglifyError::MalformedMinifyResult:
      return "The result from minify was not an object.";
    case PuglifyError::CodeMissing:
      return "The code property was not attached to the minify result.";
  }
}

class PuglifyMessage {
 public:
  string id;
  string name;
  string event;
  string code;
  string *minified_code;
  string *error;
  PuglifyMessage(string id, string name, string event, string code)
      : id(id), name(name), event(event), code(code) {}
  PuglifyMessage(
      string id, string name, string event, string *minified_code,
      string *error)
      : id(id),
        name(name),
        event(event),
        minified_code(minified_code),
        error(error) {}
};

void buffer_delete_callback(char *data, void *string) {
  delete reinterpret_cast<std::string *>(string);
}

class Puglify : public StreamingWorker<PuglifyMessage> {
 public:
  Puglify(
      Callback *data, Callback *complete, Callback *error_callback,
      Local<Object> &options)
      : StreamingWorker<PuglifyMessage>(data, complete, error_callback) {
    // Create the isolate for this thread.
    Isolate::CreateParams isolate_params;
    isolate_params.array_buffer_allocator = Allocator::NewDefaultAllocator();
    isolate = Isolate::New(isolate_params);

    {
      // Enter the isolate and acquire its lock.
      Isolate::Scope isolate_scope(isolate);
      Locker locker(isolate);
      v8::HandleScope handle_scope(isolate);
      context = Nan::Global<Context>(Context::New(isolate));
      Context::Scope context_scope(Nan::New(context));
      auto result = prepare_minify();
      if (!result) {
        auto error = result.left().hasValue
                         ? puglify_error_to_string(result.left().value)
                         : "Unknown error.";
        Local<Value> argv[] = {Nan::Error(error.c_str())};
        error_callback->Call(1, argv);
        Cleanup();
      }
    }
  }

  void Cleanup() {
    minify.Reset();
    context.Reset();
    isolate->Dispose();
  }

  ~Puglify() { Cleanup(); }

  virtual PuglifyMessage MarshalToWorker(Nan::NAN_METHOD_ARGS_TYPE &args) {
    auto opts = Nan::To<Object>(args[0]).ToLocalChecked();
    Utf8String id(
        To<v8::String>(
            Nan::Get(opts, Nan::New("id").ToLocalChecked()).ToLocalChecked())
            .ToLocalChecked());
    Utf8String name(
        To<v8::String>(
            Nan::Get(opts, Nan::New("name").ToLocalChecked()).ToLocalChecked())
            .ToLocalChecked());
    Utf8String event(
        To<v8::String>(
            Nan::Get(opts, Nan::New("event").ToLocalChecked()).ToLocalChecked())
            .ToLocalChecked());
    Utf8String code(
        To<v8::String>(
            Nan::Get(opts, Nan::New("code").ToLocalChecked()).ToLocalChecked())
            .ToLocalChecked());
    return PuglifyMessage(
        std::string(*id), std::string(*name), std::string(*event),
        std::string(*code));
  };

  virtual std::vector<Local<Value>> MarshalToScript(PuglifyMessage message) {
    auto id = Nan::New(message.id).ToLocalChecked();
    auto name = Nan::New(message.name).ToLocalChecked();
    auto event = Nan::New(message.event).ToLocalChecked();
    auto error = *message.error == "undefined"
                     ? static_cast<Local<Value>>(Nan::Undefined())
                     : static_cast<Local<Value>>(
                           Nan::New(*message.error).ToLocalChecked());

    auto result = Nan::New<Object>();
    Nan::Set(result, Nan::New("id").ToLocalChecked(), id);
    Nan::Set(result, Nan::New("name").ToLocalChecked(), name);
    Nan::Set(result, Nan::New("event").ToLocalChecked(), event);
    Nan::Set(result, Nan::New("error").ToLocalChecked(), error);

    if (message.minified_code != nullptr &&
        *message.minified_code != "undefined") {
      auto buffer = Nan::NewBuffer(
                        const_cast<char *>(message.minified_code->c_str()),
                        message.minified_code->length(), buffer_delete_callback,
                        &message.minified_code)
                        .ToLocalChecked();
      Nan::Set(result, Nan::New("code").ToLocalChecked(), buffer);
    }

    return std::vector<Local<Value>>{result};
  };

  void Execute(const AsyncProgressWorker::ExecutionProgress &progress) {
    // Enter the isolate and acquire its lock.
    Isolate::Scope isolate_scope(isolate);
    Locker locker(isolate);
    v8::HandleScope handle_scope(isolate);

    // Get a local handle to the persistent context and enter it.
    auto local_context = Nan::New(context);
    Context::Scope context_scope(local_context);

    while (true) {
      // `fromNode.read()` blocks until a message is received.
      auto incoming = toWorkerQueue.read();

      // Escape the loop if we receive a termination signal.
      if (incoming.event == "terminate") {
        break;
      }

      // Ignore all events except for minify.
      if (incoming.event != "minify") {
        continue;
      }

      // Minify the provided code.
      const auto &code = incoming.code;
      Either<PuglifyError, string> data{right(code)};

      data.rightMap([](auto data) { return New<v8::String>(data); })
          .rightFlatMap(
              [](auto maybe) { return get_or(maybe, PuglifyError::BadInput); })
          .rightMap([this, &local_context](auto source) {
            Local<Value> argv[] = {source};
            return Nan::To<Object>(
                Nan::New(minify)->Call(local_context->Global(), 1, argv));
          })
          .rightFlatMap([](auto maybe) {
            return get_or(maybe, PuglifyError::MalformedMinifyResult);
          })
          .join(
              [this, &incoming, &progress](auto error) {
                auto error_string =
                    new std::string(puglify_error_to_string(error));
                auto message = PuglifyMessage(
                    incoming.id, incoming.name, "result", nullptr,
                    error_string);
                writeToMainThread(progress, message);
              },
              [this, &incoming, &progress](auto result) {
                auto maybe_code =
                    Nan::Get(result, Nan::New("code").ToLocalChecked());
                auto code = !maybe_code.IsEmpty()
                                ? maybe_code.ToLocalChecked()
                                : static_cast<Local<Value>>(Nan::Undefined());

                auto maybe_error =
                    Nan::Get(result, Nan::New("error").ToLocalChecked());
                auto error = !maybe_error.IsEmpty()
                                 ? maybe_error.ToLocalChecked()
                                 : static_cast<Local<Value>>(Nan::Undefined());

                Nan::Utf8String code_utf8(code);
                Nan::Utf8String error_utf8(error);

                // Heap allocate the results so they don't become dangling
                // pointers to garbage
                auto code_string = new std::string(*code_utf8);
                auto error_string = new std::string(*error_utf8);

                auto message = PuglifyMessage(
                    incoming.id, incoming.name, "result", code_string,
                    error_string);
                writeToMainThread(progress, message);
              });
    }
  }

 private:
  Isolate *isolate;
  Nan::Global<Context> context;
  Nan::Global<Function> minify;

  auto prepare_minify() -> Either<PuglifyError, Local<v8::Function>> {
    auto local_context = Nan::New(context);

    Either<PuglifyError, MaybeLocal<v8::String>> source =
        right(Nan::New(reinterpret_cast<const char *>(uglify)));

    return source
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::SourceMissing);
        })
        .rightMap([](auto source) { return Nan::CompileScript(source); })
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::UglifyCompilationFailure);
        })
        .rightMap([](auto script) { return Nan::RunScript(script); })
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::UglifyRuntimeFailure);
        })
        .rightMap([&local_context](auto _) {
          return Nan::To<Object>(local_context->Global());
        })
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::ContextMissingGlobals);
        })
        .rightMap([](auto global) {
          return Nan::Get(global, Nan::New("minify").ToLocalChecked());
        })
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::GlobalMinifyNotFound);
        })
        .rightMap([](auto value) { return Nan::To<Function>(value); })
        .rightFlatMap([](auto maybe) {
          return get_or(maybe, PuglifyError::GlobalMinifyNotAFunction);
        })
        .rightMap([this](auto fn) {
          minify = fn;
          return fn;
        });
  }
};

// Fill out this method to make StreamWorkerWrapper's forward decl happy.
template <typename Message>
StreamingWorker<Message> *create_worker(
    Callback *data, Callback *complete, Callback *error_callback,
    v8::Local<v8::Object> &options) {
  return new Puglify(data, complete, error_callback, options);
}

StreamingWorker<PuglifyMessage> *create_worker(
    Callback *data, Callback *complete, Callback *error_callback,
    v8::Local<v8::Object> &options) {
  return new Puglify(data, complete, error_callback, options);
}

NODE_MODULE(PuglifyWorker, StreamWorkerWrapper<PuglifyMessage>::Init)
