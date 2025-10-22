"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ReadlineParser = void 0;
const parser_delimiter_1 = require("@serialport/parser-delimiter");
/**
 *  A transform stream that emits data after a newline delimiter is received.
 * @summary To use the `Readline` parser, provide a delimiter (defaults to `\n`). Data is emitted as string controllable by the `encoding` option (defaults to `utf8`).
 */
class ReadlineParser extends parser_delimiter_1.DelimiterParser {
    constructor(options) {
        const opts = {
            delimiter: Buffer.from('\n', 'utf8'),
            encoding: 'utf8',
            ...options,
        };
        if (typeof opts.delimiter === 'string') {
            opts.delimiter = Buffer.from(opts.delimiter, opts.encoding);
        }
        super(opts);
    }
}
exports.ReadlineParser = ReadlineParser;
