'use strict';

// Not that this path is aliased to the lib/ bundle
import minify from 'uglify-js';

const DEFAULT_UGLIFY_OPTS = {
  warnings: false,
  output: {
    // eslint-disable-next-line camelcase
    max_line_len: Infinity
  }
};

const puglify = args => minify(args.code, args.opts || DEFAULT_UGLIFY_OPTS);

thread.on('minify', rawMessage => {
  const message = JSON.parse(rawMessage);

  let result;
  try {
    result = Object.assign({}, message, puglify(message));
  } catch (error) {
    result = Object.assign({}, message, { error })
  }

  thread.emit('minify', JSON.stringify(result));
});


