"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SlipDecoder = void 0;
const stream_1 = require("stream");
/**
 * A transform stream that decodes slip encoded data.
 * @extends Transform
 *
 * Runs in O(n) time, stripping out slip encoding and emitting decoded data. Optionally custom slip escape and delimiters can be provided.
 */
class SlipDecoder extends stream_1.Transform {
    opts;
    buffer;
    escape;
    start;
    constructor(options = {}) {
        super(options);
        const { START, ESC = 0xdb, END = 0xc0, ESC_START, ESC_END = 0xdc, ESC_ESC = 0xdd } = options;
        this.opts = {
            START,
            ESC,
            END,
            ESC_START,
            ESC_END,
            ESC_ESC,
        };
        this.buffer = Buffer.alloc(0);
        this.escape = false;
        this.start = false;
    }
    _transform(chunk, encoding, cb) {
        for (let ndx = 0; ndx < chunk.length; ndx++) {
            let byte = chunk[ndx];
            if (byte === this.opts.START) {
                this.start = true;
                continue;
            }
            else if (undefined == this.opts.START) {
                this.start = true;
            }
            if (this.escape) {
                if (byte === this.opts.ESC_START && this.opts.START) {
                    byte = this.opts.START;
                }
                else if (byte === this.opts.ESC_ESC) {
                    byte = this.opts.ESC;
                }
                else if (byte === this.opts.ESC_END) {
                    byte = this.opts.END;
                }
                else {
                    this.escape = false;
                    this.push(this.buffer);
                    this.buffer = Buffer.alloc(0);
                }
            }
            else {
                if (byte === this.opts.ESC) {
                    this.escape = true;
                    continue;
                }
                if (byte === this.opts.END) {
                    this.push(this.buffer);
                    this.buffer = Buffer.alloc(0);
                    this.escape = false;
                    this.start = false;
                    continue;
                }
            }
            this.escape = false;
            if (this.start) {
                this.buffer = Buffer.concat([this.buffer, Buffer.from([byte])]);
            }
        }
        cb();
    }
    _flush(cb) {
        this.push(this.buffer);
        this.buffer = Buffer.alloc(0);
        cb();
    }
}
exports.SlipDecoder = SlipDecoder;
