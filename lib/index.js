'use strict';

const path = require('path');
const os = require('os');

const uuid = require('uuid/v4');
const Threads = require('@tptee/webworker-threads');

const DEFAULT_TIMEOUT = 60000; // one minute

class Puglifier {
  constructor(opts) {
    opts = opts || {};

    if (opts.timeout) {
      this.timeout = opts.timeout;
    }

    this.taskCallbacks = {};
    this.threadPool = Threads.createPool(opts.threadCount || os.cpus().length);
    this.threadPool.load(path.resolve(__dirname, 'worker.js'));
    this.threadPool.on('minify', rawResult => {
      const result = JSON.parse(rawResult);
      const taskCallback = this.taskCallbacks[result.taskId];
      if (taskCallback) {
        if (result.error) {
          taskCallback(result.error, null);
        } else {
          delete result.taskId;
          taskCallback(null, result);
        }
        delete this.taskCallbacks[result.taskId];
      }
    });
  }

  puglify(args) {
    if (Array.isArray(args.code)) {
      return Promise.all(
        args.code.map(code =>
          this.puglifyOne(Object.assign({}, args, { code }))
        )
      );
    }

    return this.puglifyOne(args);
  }

  puglifyOne(args) {
    // For some reason, this promise never resolves using native
    // Node promises, but works as expected in Bluebird. Wrapping
    // the calls in `setImmediate` solves the issue...but why???
    return new Promise((resolve, reject) =>
      this._minify(args, (err, result) => setImmediate(() =>
        err ? reject(err) : resolve(result)
      ))
    );
  }

  _minify(args, cb) {
    const taskId = uuid();
    this.taskCallbacks[taskId] = cb;
    this.threadPool.any.emit('minify', JSON.stringify({
      taskId,
      code: args.code,
      opts: args.opts
    }));

    setTimeout(() => {
      cb(new Error('The minification task timed out.'), null);
    }, this.timeout || DEFAULT_TIMEOUT);
  }

  terminate() {
    this.threadPool.destroy();
  }
}

module.exports = Puglifier;
