# puglify

`puglify` is a multithreaded wrapper around `uglify`. It speeds up batch minification of many small modules.

`puglify` stands for:
- parallel uglify
- the fact that a pug's face looks minified

`puglify` uses `node-webworker-threads` under the hood. No compiler is required to build it, unless you're on an esoteric architecture.

# Usage

```js
const puglifier = new Puglifier({
  // Required. Can be a string or array of strings containing code to minify.
  code: [
    'var pug = "lify"; function puglify() { return true; }',
    'var two = 1 + 1; function add(a, b) { return a + b; }'
  ]
  // Optional. Number of threads to utilize. Defaults to `os.cpus().length`.
  threadCount: os.cpus().length,
  // Optional. Timeout for each minification tasks. Defaults to one minute.
  timeout: 60000
})
  .then(batch => console.log(batch))
  .catch(err => console.log(err))
  // If you're not running `puglify` frequently, feel free to
  // terminate the instance. Persisting it eliminates subsequent startup
  // times, which is useful for interactive applications.
  .then(() => puglifier.terminate());
```

The result is either an object or array of objects with the minified code:

```js
[{
  code: 'function puglify(){return!0}var pug="lify";'
}, {
 code: 'function add(n,r){return n+r}var two=2;'
}]
```
