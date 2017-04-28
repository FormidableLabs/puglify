/* eslint-disable no-console */

'use strict';

const path = require('path');
const pify = require('pify');
const fs = pify(require('fs-extra'));
const argv = require('yargs').argv;
const chokidar = require('chokidar');
const chalk = require('chalk');

const rollup = require('rollup');
const alias = require('rollup-plugin-alias');
const nodeResolve = require('rollup-plugin-node-resolve');
const commonjs = require('rollup-plugin-commonjs');

const src = path.resolve(__dirname, '../src');
const lib = path.resolve(__dirname, '../lib');
const uglifyLibPath = path.resolve(lib, 'uglify.js');
const workerSrcPath = path.resolve(src, 'worker/index.js');
const workerLibPath = path.resolve(lib, 'worker/index.js');

const isBundled = srcPath =>
  srcPath.indexOf('src/worker/index.js') === -1;

const build = () => {
  const copyUnbundledFiles = fs.emptyDir(lib)
    .then(() => fs.copy(src, lib, {
      filter: isBundled
    }));

  // UglifyJS uses globals and implicit dependencies throughout its codebase.
  // This concatenates lib/ into the correct dependency order and converts it
  // to a CommonJS module for the worker to consume.
  //
  // See https://github.com/mishoo/UglifyJS2/blob/9e626281716c0f11ed6b289d6a48c7b681a99a1e/tools/node.js
  const bundleUglify = Promise.all([
    'uglify-js/lib/utils.js',
    'uglify-js/lib/ast.js',
    'uglify-js/lib/parse.js',
    'uglify-js/lib/transform.js',
    'uglify-js/lib/scope.js',
    'uglify-js/lib/output.js',
    'uglify-js/lib/compress.js',
    'uglify-js/lib/sourcemap.js',
    'uglify-js/lib/mozilla-ast.js',
    'uglify-js/lib/propmangle.js',
    'uglify-js/lib/minify.js'
  ]
    .map(file => fs.readFile(require.resolve(file)), 'utf8')
  )
    .then(files => files.join('\n\n'))
    .then(bundle => `${bundle}\nmodule.exports = minify;\n`)
    .then(bundle => fs.writeFile(uglifyLibPath, bundle));

  const bundleWorker = bundleUglify
    .then(() =>
      rollup.rollup({
        entry: workerSrcPath,
        plugins: [
          alias({
            'uglify-js': uglifyLibPath
          }),
          nodeResolve({
            main: true
          }),
          commonjs({
            include: uglifyLibPath
          })
        ]
      })
    )
    .then(bundle => {
      bundle.write({
        format: 'iife',
        dest: workerLibPath
      });
    })
    .then(() => fs.remove(uglifyLibPath));

  return Promise.all([copyUnbundledFiles, bundleWorker])
    .then(() => console.log(chalk.green('Finished build.')));
};

build().then(() => {
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


