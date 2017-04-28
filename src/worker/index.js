/* global thread: true */
/* eslint-disable strict, no-console */
'use strict';

// This import statement is the only ES6 in this file.
// Rollup only tracks imports, not requires.
// Note that this path is aliased to the lib/ bundle.
import minify from 'uglify-js';

const DEFAULT_UGLIFY_OPTS = {
  warnings: false,
  output: {
    // eslint-disable-next-line camelcase
    max_line_len: Infinity
  }
};

const safeParse = string => {
  try {
    return JSON.parse(string);
  } catch (err) {
    return err;
  }
};

const serializeError = err => {
  try {
    return JSON.stringify(err, ['name', 'message', 'stack']);
  } catch (error) {
    return JSON.stringify({
      name: 'Error',
      message: 'Failed to serialize error',
      stack: error.stack
    });
  }
};

const puglify = args =>
  minify(args.code, args.opts || DEFAULT_UGLIFY_OPTS);

thread.on('minify', rawMessage => {
  const message = safeParse(rawMessage);
  if (message instanceof Error) {
    console.error('FATAL: Could not parse worker payload. This worker will hang until timeout.');
    console.error(message.stack);
    throw message;
  }

  let payload;
  try {
    payload = JSON.stringify({
      taskId: message.taskId,
      code: puglify(message).code
    });
  } catch (err) {
    // This stringify is safe since we parsed `message` earlier and
    // know it's guaranteed to be free of circular references
    payload = JSON.stringify({
      taskId: message.taskId,
      error: serializeError(err)
    });
  }

  thread.emit('minify', payload);
});
