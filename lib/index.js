'use strict';

const os = require('os');
const uuid = require('uuid/v4');
const { mergeArray } = require('most');
const createWorkerSubject = require('./worker-subject');

const isObject = value =>
  value instanceof Object && value.constructor === Object;

class WorkerPool {
  constructor(opts) {
    const threadCount = opts.threadCount || os.cpus().length;
    this.workerSubjects = Array.from(new Array(threadCount)).map(
      createWorkerSubject
    );
    this.stream = mergeArray(this.workerSubjects.map(({ stream }) => stream));

    this.currentWorker = 0;
    this.workerSubjects.forEach(({ worker }) => worker.start());
  }

  run(message) {
    const currentWorker = this.currentWorker;

    // Cycle through the worker pool
    if (this.currentWorker === this.workerSubjects.length - 1) {
      this.currentWorker = 0;
    } else {
      this.currentWorker++;
    }

    this.workerSubjects[currentWorker].worker.sendToWorker(message);
  }

  terminate() {
    this.workerSubjects.forEach(({ worker }) =>
      worker.sendToWorker({ event: 'terminate' })
    );
  }
}

module.exports = class Puglifier {
  constructor({ timeout = null, threadCount = null } = {}) {
    this.timeout = timeout;
    this.pool = new WorkerPool({ threadCount });
  }

  _minifyOneWithObservable({ name, code }) {
    const id = uuid();
    const stream = this.pool.stream.filter(result => result.id === id).take(1);
    this.pool.run({
      event: 'minify',
      name: name || id,
      code,
      id
    });
    return stream;
  }

  minifyWithObservable({ code }) {
    if (isObject(code)) {
      return mergeArray(
        Object.keys(code).map(name =>
          this._minifyOneWithObservable({ name, code: code[name] })
        )
      );
    }

    return this._minifyOneWithObservable({ code });
  }

  minify(args) {
    if (isObject(args.code)) {
      return this.minifyWithObservable(args).reduce(
        (acc, result) => Object.assign(acc, { [result.name]: result }),
        {}
      );
    }

    return this.minifyWithObservable(args).reduce((x, y) => y);
  }

  terminate() {
    this.pool.terminate();
  }
};
