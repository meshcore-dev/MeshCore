"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.RegexParser = void 0;
const stream_1 = require("stream");
/**
 * A transform stream that uses a regular expression to split the incoming text upon.
 *
 * To use the `Regex` parser provide a regular expression to split the incoming text upon. Data is emitted as string controllable by the `encoding` option (defaults to `utf8`).
 */
class RegexParser extends stream_1.Transform {
    regex;
    data;
    constructor({ regex, ...options }) {
        const opts = {
            encoding: 'utf8',
            ...options,
        };
        if (regex === undefined) {
            throw new TypeError('"options.regex" must be a regular expression pattern or object');
        }
        if (!(regex instanceof RegExp)) {
            regex = new RegExp(regex.toString());
        }
        super(opts);
        this.regex = regex;
        this.data = '';
    }
    _transform(chunk, encoding, cb) {
        const data = this.data + chunk;
        const parts = data.split(this.regex);
        this.data = parts.pop() || '';
        parts.forEach(part => {
            this.push(part);
        });
        cb();
    }
    _flush(cb) {
        this.push(this.data);
        this.data = '';
        cb();
    }
}
exports.RegexParser = RegexParser;
