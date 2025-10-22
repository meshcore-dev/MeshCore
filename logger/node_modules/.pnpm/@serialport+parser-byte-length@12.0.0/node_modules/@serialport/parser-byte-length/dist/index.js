"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ByteLengthParser = void 0;
const stream_1 = require("stream");
/**
 * Emit data every number of bytes
 *
 * A transform stream that emits data as a buffer after a specific number of bytes are received. Runs in O(n) time.
 */
class ByteLengthParser extends stream_1.Transform {
    length;
    position;
    buffer;
    constructor(options) {
        super(options);
        if (typeof options.length !== 'number') {
            throw new TypeError('"length" is not a number');
        }
        if (options.length < 1) {
            throw new TypeError('"length" is not greater than 0');
        }
        this.length = options.length;
        this.position = 0;
        this.buffer = Buffer.alloc(this.length);
    }
    _transform(chunk, _encoding, cb) {
        let cursor = 0;
        while (cursor < chunk.length) {
            this.buffer[this.position] = chunk[cursor];
            cursor++;
            this.position++;
            if (this.position === this.length) {
                this.push(this.buffer);
                this.buffer = Buffer.alloc(this.length);
                this.position = 0;
            }
        }
        cb();
    }
    _flush(cb) {
        this.push(this.buffer.slice(0, this.position));
        this.buffer = Buffer.alloc(this.length);
        cb();
    }
}
exports.ByteLengthParser = ByteLengthParser;
