# puglify 🐶

`puglify` is a multithreaded wrapper around `uglify`. It speeds up batch
minification of many small modules.

`puglify` stands for:

* parallel uglify
* the fact that a pug's face looks minified

## Usage

Start a `Puglifier` instance:

```js
const puglifier = new Puglifier({
  // Optional. Number of threads to utilize. Defaults to `os.cpus().length`.
  threadCount: os.cpus().length
});
```

Then, use either the Promise or Observable interfaces:

```js
// Promise interface, single code string
puglifier
  .minify({
    code: 'var pug = "lify"; function puglify() { return true; }'
  })
  .then(batch => console.log(batch))
  .catch(err => console.error(err))
  // If you're not running `puglify` frequently, feel free to
  // terminate the instance. Persisting it eliminates subsequent startup
  // times, which is useful for interactive applications.
  .then(() => puglifier.terminate());

// Promise interface, multiple code strings
puglifier
  .minify({
    code: {
      // Keys don't have to be valid filenames. They're just useful
      // for identifying which code strings get emitted from `next()`
      // when subscribing to an observable.
      'one.js': 'var pug = "lify"; function puglify() { return true; }',
      'two.js': 'var two = 1 + 1; function add(a, b) { return a + b; }'
    }
  })

// Observable interface, single code string
puglifier
  .minifyWithObservable({
    code: 'var pug = "lify"; function puglify() { return true; }'
  })
  .subscribe({
    next: code => {
      console.log(code);
    },
    error: err => {
      console.error(error)
    },
    complete: () => puglifier.terminate();
  })

// Promise interface, multiple code strings
puglifier
  .minifyWithObservable({
    code: {
      // Keys don't have to be valid filenames. They're just useful
      // for identifying which code strings get emitted from `next()`
      // when subscribing to an observable.
      'one.js': 'var pug = "lify"; functwion puglify() { return true; }',
      'two.js': 'var two = 1 + 1; function add(a, b) { return a + b; }'
    }
  })
```

For each code string, the result type looks like:

```js
{
  // We return Buffers so we can pass code off easily to gzip
  code: Buffer,
  error: string
}
```

Multi-string results are just an object with names as keys and code/error objects as values.

## Stability

`puglify` is wildly unstable! Use at your own risk until 1.0.0.

## TODO

* Provide prebuilt binaries for all major platforms.
* Ensure crash safety in all V8 code.
* Audit for memory leaks.
* Setup CI.
