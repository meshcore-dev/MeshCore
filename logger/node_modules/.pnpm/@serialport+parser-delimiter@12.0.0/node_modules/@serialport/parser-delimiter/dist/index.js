"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.DelimiterParser = void 0;
const stream_1 = require("stream");
/**
 * A transform stream that emits data each time a byte sequence is received.
 * @extends Transform
 *
 * To use the `Delimiter` parser, provide a delimiter as a string, buffer, or array of bytes. Runs in O(n) time.
 */
class DelimiterParser extends stream_1.Transform {
    includeDelimiter;
    delimiter;
    buffer;
    constructor({ delimiter, includeDelimiter = false, ...options }) {
        super(options);
        if (delimiter === undefined) {
            throw new TypeError('"delimiter" is not a bufferable object');
        }
        if (delimiter.length === 0) {
            throw new TypeError('"delimiter" has a 0 or undefined length');
        }
        this.includeDelimiter = includeDelimiter;
        this.delimiter = Buffer.from(delimiter);
        this.buffer = Buffer.alloc(0);
    }
    _transform(chunk, encoding, cb) {
        let data = Buffer.concat([this.buffer, chunk]);
        let position;
        while ((position = data.indexOf(this.delimiter)) !== -1) {
            this.push(data.slice(0, position + (this.includeDelimiter ? this.delimiter.length : 0)));
            data = data.slice(position + this.delimiter.length);
        }
        this.buffer = data;
        cb();
    }
    _flush(cb) {
        this.push(this.buffer);
        this.buffer = Buffer.alloc(0);
        cb();
    }
}
exports.DelimiterParser = DelimiterParser;
