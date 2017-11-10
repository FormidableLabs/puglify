'use strict';

const { create } = require('@most/create');
const createWorker = require('bindings')('PuglifyWorker').StreamingWorker;

const identity = x => x;

module.exports = () => {
  // TODO: this is dumb,, just let the args be optional
  const worker = createWorker(identity, identity, identity, {});

  const stream = create((add, end, error) => {
    worker.setOnData(add);
    worker.setOnComplete(end);
    worker.setOnError(error);
  });

  return {
    stream,
    worker
  };
};
