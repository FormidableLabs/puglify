'use strict';

const Puglifier = require('./');

describe('Puglifier', () => {
  let puglifier;

  beforeEach(() => {
    puglifier = new Puglifier();
  });

  afterEach(() => {
    puglifier.terminate();
  });

  it('minifies a single code string with an observable', () => {
    return puglifier
      .minifyWithObservable({
        code: 'function stuff(thing) { return thing; }'
      })
      .reduce((x, y) => y)
      .then(result => {
        expect(result).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(result.code)).toBe(true);
      });
  });

  it('returns minification errors from Uglify with an observable', () => {
    return puglifier
      .minifyWithObservable({
        code: 'big jug hot cheese'
      })
      .reduce((x, y) => y)
      .then(result => {
        expect(result.error).toEqual(
          expect.stringContaining('Unexpected token')
        );
        expect(Buffer.isBuffer(result.code)).toBe(false);
      });
  });

  it('minifies multiple code strings with an observable', () => {
    return puglifier
      .minifyWithObservable({
        code: {
          first: 'function stuff(thing) { return thing; }',
          second: 'let four = 2 + 2;',
          third: 'cool and good'
        }
      })
      .reduce(
        (acc, result) => Object.assign(acc, { [result.name]: result }),
        {}
      )
      .then(({ first, second, third }) => {
        expect(first).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(first.code)).toBe(true);
        expect(second).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(second.code)).toBe(true);
        expect(third.error).toEqual(
          expect.stringContaining('Unexpected token')
        );
        expect(Buffer.isBuffer(third.code)).toBe(false);
      });
  });

  it('minifies a single code string with a promise', () => {
    return puglifier
      .minify({
        code: 'function stuff(thing) { return thing; }'
      })
      .then(result => {
        expect(result).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(result.code)).toBe(true);
      });
  });

  it('minifies multiple code strings with a promise', () => {
    return puglifier
      .minify({
        code: {
          first: 'function stuff(thing) { return thing; }',
          second: 'let four = 2 + 2;',
          third: 'cool and good'
        }
      })
      .then(({ first, second, third }) => {
        expect(first).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(first.code)).toBe(true);
        expect(second).toHaveProperty('error', undefined);
        expect(Buffer.isBuffer(second.code)).toBe(true);
        expect(third.error).toEqual(
          expect.stringContaining('Unexpected token')
        );
        expect(Buffer.isBuffer(third.code)).toBe(false);
      });
  });
});
