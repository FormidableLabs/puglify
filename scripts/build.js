/* eslint-disable no-console */

'use strict';

const path = require('path');
const pify = require('pify');
const fs = pify(require('fs-extra'));
const argv = require('yargs').argv;
const chokidar = require('chokidar');
const chalk = require('chalk');
const { minify } = require('uglify-es');

const src = path.resolve(__dirname, '../src');
const vendor = path.resolve(__dirname, '../vendor/uglify');
const uglifyLibPath = path.resolve(vendor, 'uglify.js.c');

const isBundled = srcPath => srcPath.indexOf('src/worker/index.js') === -1;

const build = () => {
  const copyUnbundledFiles = fs.emptyDir(vendor).then(() =>
    fs.copy(src, vendor, {
      filter: isBundled
    })
  );

  // UglifyJS uses globals and implicit dependencies throughout its codebase.
  // This concatenates lib/ into the correct dependency order and converts it
  // to a CommonJS module for the worker to consume.
  //
  // See https://github.com/mishoo/UglifyJS2/blob/9e626281716c0f11ed6b289d6a48c7b681a99a1e/tools/node.js
  const bundleUglify = Promise.all(
    [
      'utils.js',
      'ast.js',
      'parse.js',
      'transform.js',
      'scope.js',
      'output.js',
      'compress.js',
      'sourcemap.js',
      'mozilla-ast.js',
      'propmangle.js',
      'minify.js'
    ]
      .map(file => `uglify-es/lib/${file}`)
      .map(file => fs.readFile(require.resolve(file)), 'utf8')
  )
    .then(files => files.join('\n\n'))
    .then(minify)
    .then(result => {
      if (result.error) {
        throw result.error;
      }
      return result.code;
    })
    .then(
      code =>
        `
exports = {};

${code}`
    )
    .then(bundle => Buffer.from(bundle, 'utf8'))
    .then(buffer => [...buffer.values()])
    .then(bytes =>
      bytes.reduce((acc, byte) => `${(acc && `${acc},`) || ''}${byte}`, '')
    )
    .then(
      byteString => `static const unsigned char uglify[] = {${byteString}, 0};`
    )
    .then(bundle => fs.writeFile(uglifyLibPath, bundle));

  return Promise.all([copyUnbundledFiles, bundleUglify]).then(() =>
    console.log(chalk.green('Finished build.'))
  );
};

build()
  .then(() => {
    if (argv.watch) {
      const watcher = chokidar.watch(src, {
        ignoreInitial: true
      });
      watcher.on('add', build);
      watcher.on('change', build);
      watcher.on('unlink', build);
      console.log(chalk.yellow('Watching...'));
    }
  })
  .catch(err => console.error(err));
