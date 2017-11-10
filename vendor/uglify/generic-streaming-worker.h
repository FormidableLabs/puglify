#ifndef ____StreamingWorker__
#define ____StreamingWorker__

// A generic version of streaming-worker by Scott Frees:
// https://github.com/freezer333/streaming-worker/blob/master/sdk/streaming-worker.h

#include <nan.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

using namespace Nan;
using namespace std;
using v8::Local;
using v8::Value;

template <typename Data>
class PCQueue {
 public:
  void write(Data data) {
    while (true) {
      std::unique_lock<std::mutex> locker(mu);
      buffer_.push_back(data);
      locker.unlock();
      cond.notify_all();
      return;
    }
  }
  Data read() {
    while (true) {
      std::unique_lock<std::mutex> locker(mu);
      cond.wait(locker, [this]() { return buffer_.size() > 0; });
      Data back = buffer_.front();
      buffer_.pop_front();
      locker.unlock();
      cond.notify_all();
      return back;
    }
  }
  void readAll(std::deque<Data> &target) {
    std::unique_lock<std::mutex> locker(mu);
    std::copy(buffer_.begin(), buffer_.end(), std::back_inserter(target));
    buffer_.clear();
    locker.unlock();
  }
  PCQueue() {}

 private:
  std::mutex mu;
  std::condition_variable cond;
  std::deque<Data> buffer_;
};

template <typename Message>
using RunCallback = void (*)(Message);

template <typename Message>
class MarshalDelegate {
 public:
  virtual Message MarshalToWorker(Nan::NAN_METHOD_ARGS_TYPE &args) = 0;
  virtual std::vector<Local<Value>> MarshalToScript(Message message) = 0;
};

template <typename Message>
class StreamingWorker : public AsyncProgressWorker,
                        public MarshalDelegate<Message> {
 public:
  StreamingWorker(
      Callback *event_callback, Callback *callback, Callback *error_callback)
      : AsyncProgressWorker(callback),
        event_callback(event_callback),
        error_callback(error_callback) {
    input_closed = false;
  }

  ~StreamingWorker() {
    delete event_callback;
    delete error_callback;
  }

  void HandleErrorCallback() {
    HandleScope scope;

    v8::Local<v8::Value> argv[] = {
        v8::Exception::Error(New<v8::String>(ErrorMessage()).ToLocalChecked())};
    error_callback->Call(1, argv);
  }

  void HandleOKCallback() {
    drainQueue();
    callback->Call(0, NULL);
  }

  void HandleProgressCallback(const char *data, size_t size) { drainQueue(); }

  void close() { input_closed = true; }

  void writeToWorker(Message message) { toWorkerQueue.write(message); }

  void writeToMainThread(
      const AsyncProgressWorker::ExecutionProgress &progress,
      const Message &msg) {
    toMainThreadQueue.write(msg);
    progress.Send(
        reinterpret_cast<const char *>(&toMainThreadQueue),
        sizeof(toMainThreadQueue));
  }

  void SendToWorker(Nan::NAN_METHOD_ARGS_TYPE &args) {
    writeToWorker(this->MarshalToWorker(args));
  }

  void SetDataCallback(Callback *data) { event_callback = data; }

  void SetOKCallback(Callback *ok) { callback = ok; }

  void SetErrorCallback(Callback *error) { error_callback = error; }

  Callback *event_callback;
  PCQueue<Message> toWorkerQueue;

 protected:
  bool closed() { return input_closed; }

  Callback *error_callback;
  PCQueue<Message> toMainThreadQueue;
  bool input_closed;

 private:
  void drainQueue() {
    HandleScope scope;

    // drain the queue - since we might only get called once for many writes
    std::deque<Message> contents;
    toMainThreadQueue.readAll(contents);
    for (Message &msg : contents) {
      auto args = this->MarshalToScript(msg);
      event_callback->Call(args.size(), &args[0]);
    }
  }
};

template <typename Message>
StreamingWorker<Message> *create_worker(
    Callback *, Callback *, Callback *, v8::Local<v8::Object> &);

template <typename Message>
class StreamWorkerWrapper : public Nan::ObjectWrap {
 public:
  static NAN_MODULE_INIT(Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("StreamingWorker").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(2);

    SetPrototypeMethod(tpl, "start", start);
    SetPrototypeMethod(tpl, "sendToWorker", sendToWorker);
    SetPrototypeMethod(tpl, "closeInput", closeInput);
    SetPrototypeMethod(tpl, "setOnData", setOnData);
    SetPrototypeMethod(tpl, "setOnComplete", setOnComplete);
    SetPrototypeMethod(tpl, "setOnError", setOnError);

    constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(
        target, Nan::New("StreamingWorker").ToLocalChecked(),
        Nan::GetFunction(tpl).ToLocalChecked());
  }

 private:
  explicit StreamWorkerWrapper(StreamingWorker<Message> *worker)
      : _worker(worker) {}
  ~StreamWorkerWrapper() {}

  static NAN_METHOD(New) {
    if (info.IsConstructCall()) {
      Callback *data_callback = new Callback(info[0].As<v8::Function>());
      Callback *complete_callback = new Callback(info[1].As<v8::Function>());
      Callback *error_callback = new Callback(info[2].As<v8::Function>());

      // TODO: actually provide options
      auto options = Nan::New<v8::Object>();
      StreamWorkerWrapper *obj = new StreamWorkerWrapper(create_worker<Message>(
          data_callback, complete_callback, error_callback, options));

      obj->Wrap(info.This());
      info.GetReturnValue().Set(info.This());
    } else {
      const int argc = 3;
      v8::Local<v8::Value> argv[argc] = {info[0], info[1], info[2]};
      v8::Local<v8::Function> cons = Nan::New(constructor());
      v8::Local<v8::Object> instance =
          Nan::NewInstance(cons, argc, argv).ToLocalChecked();
      info.GetReturnValue().Set(instance);
    }
  }

  static NAN_METHOD(start) {
    // start the worker
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    AsyncQueueWorker(obj->_worker);
  }

  static NAN_METHOD(sendToWorker) {
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    obj->_worker->SendToWorker(info);
  }

  static NAN_METHOD(closeInput) {
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    obj->_worker->close();
  }

  static NAN_METHOD(setOnData) {
    Callback *data_callback = new Callback(info[0].As<v8::Function>());
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    obj->_worker->SetDataCallback(data_callback);
  }

  static NAN_METHOD(setOnComplete) {
    Callback *completion_callback = new Callback(info[0].As<v8::Function>());
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    obj->_worker->SetOKCallback(completion_callback);
  }

  static NAN_METHOD(setOnError) {
    Callback *error_callback = new Callback(info[0].As<v8::Function>());
    StreamWorkerWrapper *obj =
        Nan::ObjectWrap::Unwrap<StreamWorkerWrapper>(info.Holder());
    obj->_worker->SetErrorCallback(error_callback);
  }

  static inline Nan::Persistent<v8::Function> &constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }

  StreamingWorker<Message> *_worker;
};
#endif  // ____StreamingWorker__
